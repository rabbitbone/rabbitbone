syscall_result_t rabbitbone_sys_exit(u64 code) {
    if (process_user_active() && process_async_scheduler_active()) {
        process_request_exit((i32)code);
        return ok((i64)code);
    }
    if (process_user_active()) process_exit_from_interrupt((i32)code);
    return ok((i64)code);
}

syscall_result_t rabbitbone_sys_fork(void) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (!process_request_fork()) return err(VFS_ERR_NOMEM);
    return ok(0);
}

static bool user_log_permit(u32 pid, u32 *suppressed_out) {
    if (suppressed_out) *suppressed_out = 0;
    if (!pid) return true;
    u64 now = pit_ticks();
    u64 flags = spin_lock_irqsave(&user_log_lock);
    sys_user_log_state_t *slot = 0;
    sys_user_log_state_t *free_slot = 0;
    for (usize i = 0; i < SYSCALL_USER_LOG_SLOTS; ++i) {
        if (user_log_states[i].used && user_log_states[i].pid == pid) { slot = &user_log_states[i]; break; }
        if (!user_log_states[i].used && !free_slot) free_slot = &user_log_states[i];
    }
    if (!slot) {
        slot = free_slot ? free_slot : &user_log_states[pid % SYSCALL_USER_LOG_SLOTS];
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        slot->pid = pid;
        slot->window_start = now;
    }
    if (now - slot->window_start >= SYSCALL_USER_LOG_WINDOW_TICKS) {
        if (suppressed_out) *suppressed_out = slot->suppressed;
        slot->window_start = now;
        slot->count = 0;
        slot->suppressed = 0;
    }
    bool permit = false;
    if (slot->count < SYSCALL_USER_LOG_BURST) {
        ++slot->count;
        permit = true;
    } else {
        ++slot->suppressed;
    }
    spin_unlock_irqrestore(&user_log_lock, flags);
    return permit;
}

syscall_result_t rabbitbone_sys_log(u64 msg_ptr) {
    char msg[160];
    if (!copy_string_arg(msg_ptr, msg, sizeof(msg))) return err(VFS_ERR_INVAL);
    u32 pid = process_current_pid();
    u32 suppressed = 0;
    if (!user_log_permit(pid, &suppressed)) return ok(0);
    process_info_t info;
    const char *name = "kernel";
    if (process_current_info(&info) && info.name[0]) name = info.name;
    if (suppressed) KLOG(LOG_WARN, "usersys", "pid=%u name=%s suppressed=%u log messages", pid, name, suppressed);
    KLOG(LOG_INFO, "usersys", "pid=%u name=%s %s", pid, name, msg);
    return ok(0);
}


syscall_result_t rabbitbone_sys_theme(u64 op64, u64 value64) {
    if (op64 == RABBITBONE_THEME_OP_GET) return ok((i64)console_theme());
    if (op64 != RABBITBONE_THEME_OP_SET || value64 >= RABBITBONE_THEME_MAX) return err(VFS_ERR_INVAL);
    if (!console_set_theme((u32)value64)) return err(VFS_ERR_INVAL);
    KLOG(LOG_INFO, "theme", "active=%s", console_theme_name((u32)value64));
    return ok((i64)value64);
}

syscall_result_t rabbitbone_sys_tty_getinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    rabbitbone_ttyinfo_t info;
    if (!tty_getinfo(&info)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_setmode(u64 mode64) {
    if (mode64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    return tty_set_mode((u32)mode64) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_readkey(u64 out_ptr, u64 flags64) {
    if (!out_ptr || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    rabbitbone_key_event_t ev;
    if (!tty_read_key(&ev, (u32)flags64)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &ev, sizeof(ev)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_scroll(u64 lines64) {
    i64 signed_lines = (i64)lines64;
    if (signed_lines > 1000 || signed_lines < -1000) return err(VFS_ERR_INVAL);
    return tty_scroll((i32)signed_lines) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_setcursor(u64 row64, u64 col64) {
    if (row64 > 0xffffffffull || col64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    return tty_set_cursor((u32)row64, (u32)col64) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_clearline(void) {
    return tty_clear_line() ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_clear(void) {
    return tty_clear() ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_tty_cursor_visible(u64 visible64) {
    if (visible64 > 1u) return err(VFS_ERR_INVAL);
    return tty_set_cursor_visible(visible64 != 0u) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t syscall_dispatch(u64 no, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    if (!initialized) syscall_init();
    rabbitbone_rust_sysargs_t args = { a0, a1, a2, a3, a4, a5 };
    return rabbitbone_rust_syscall_dispatch(no, args);
}

