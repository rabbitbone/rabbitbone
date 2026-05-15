syscall_result_t rabbitbone_sys_signal(u64 sig64, u64 handler64) {
    if (sig64 >= RABBITBONE_NSIG) return err(VFS_ERR_INVAL);
    rabbitbone_sigaction_t act;
    rabbitbone_sigaction_t old;
    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));
    act.handler = handler64;
    act.restorer = 0;
    process_status_t st = process_signal_set_action((u32)sig64, &act, &old);
    return st == PROC_OK ? ok((i64)old.handler) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_sigaction(u64 sig64, u64 act_ptr, u64 old_ptr) {
    if (sig64 >= RABBITBONE_NSIG) return err(VFS_ERR_INVAL);
    rabbitbone_sigaction_t act;
    rabbitbone_sigaction_t old;
    rabbitbone_sigaction_t *actp = 0;
    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));
    if (act_ptr) {
        if (!copy_in_buf(&act, act_ptr, sizeof(act))) return err(VFS_ERR_INVAL);
        actp = &act;
    }
    process_status_t st = process_signal_set_action((u32)sig64, actp, old_ptr ? &old : 0);
    if (st != PROC_OK) return err(process_status_to_vfs_error(st));
    if (old_ptr && !copy_out_buf(old_ptr, &old, sizeof(old))) return err(VFS_ERR_INVAL);
    return ok(0);
}

syscall_result_t rabbitbone_sys_sigprocmask(u64 how64, u64 set_ptr, u64 old_ptr) {
    if (how64 > RABBITBONE_SIG_SETMASK) return err(VFS_ERR_INVAL);
    u64 set = 0;
    u64 old = 0;
    u64 *setp = 0;
    if (set_ptr) {
        if (!copy_in_buf(&set, set_ptr, sizeof(set))) return err(VFS_ERR_INVAL);
        setp = &set;
    }
    process_status_t st = process_signal_mask((u32)how64, setp, old_ptr ? &old : 0);
    if (st != PROC_OK) return err(process_status_to_vfs_error(st));
    if (old_ptr && !copy_out_buf(old_ptr, &old, sizeof(old))) return err(VFS_ERR_INVAL);
    return ok(0);
}

syscall_result_t rabbitbone_sys_sigpending(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    u64 pending = 0;
    process_status_t st = process_signal_pending(&pending);
    if (st != PROC_OK) return err(process_status_to_vfs_error(st));
    return copy_out_buf(out_ptr, &pending, sizeof(pending)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_kill(u64 pid64, u64 sig64) {
    if (pid64 > 0xffffffffull || sig64 >= RABBITBONE_NSIG) return err(VFS_ERR_INVAL);
    process_status_t st = process_signal_send((u32)pid64, (u32)sig64);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_raise(u64 sig64) {
    if (sig64 >= RABBITBONE_NSIG) return err(VFS_ERR_INVAL);
    process_status_t st = process_signal_raise((u32)sig64);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_getpgrp(void) {
    u32 pg = 0;
    process_status_t st = process_get_pgrp(0, &pg);
    return st == PROC_OK ? ok((i64)pg) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_setpgid(u64 pid64, u64 pgid64) {
    if (pid64 > 0xffffffffull || pgid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    process_status_t st = process_set_pgrp((u32)pid64, (u32)pgid64);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_getpgid(u64 pid64) {
    if (pid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 pg = 0;
    process_status_t st = process_get_pgrp((u32)pid64, &pg);
    return st == PROC_OK ? ok((i64)pg) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_setsid(void) {
    u32 sid = 0;
    process_status_t st = process_set_sid(&sid);
    return st == PROC_OK ? ok((i64)sid) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_getsid(u64 pid64) {
    if (pid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 sid = 0;
    process_status_t st = process_get_sid((u32)pid64, &sid);
    return st == PROC_OK ? ok((i64)sid) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_tcgetpgrp(void) {
    u32 pg = 0;
    process_status_t st = process_tcgetpgrp(&pg);
    return st == PROC_OK ? ok((i64)pg) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_tcsetpgrp(u64 pgid64) {
    if (pgid64 == 0 || pgid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    process_status_t st = process_tcsetpgrp((u32)pgid64);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_sigreturn(u64 frame) {
    (void)frame;
    return err(VFS_ERR_UNSUPPORTED);
}
