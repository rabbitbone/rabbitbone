static bool signal_valid(u32 sig) { return sig > 0 && sig < RABBITBONE_NSIG; }
static u64 signal_bit(u32 sig) { return 1ull << sig; }
static bool signal_unmaskable(u32 sig) { return sig == RABBITBONE_SIGKILL || sig == RABBITBONE_SIGSTOP; }
static bool signal_stop_default(u32 sig) { return sig == RABBITBONE_SIGSTOP || sig == RABBITBONE_SIGTSTP || sig == RABBITBONE_SIGTTIN || sig == RABBITBONE_SIGTTOU; }
static bool signal_ignore_default(u32 sig) { return sig == RABBITBONE_SIGCHLD || sig == RABBITBONE_SIGCONT; }
static bool signal_terminates_default(u32 sig) { return !signal_ignore_default(sig) && !signal_stop_default(sig); }

static void process_signal_init(active_process_t *p) {
    if (!p) return;
    p->signal_pending = 0;
    p->signal_blocked = 0;
    p->signal_in_handler = false;
    memset(p->signal_actions, 0, sizeof(p->signal_actions));
}

static u64 signal_sanitized_mask(u64 m) {
    m &= ~1ull;
    m &= ~(signal_bit(RABBITBONE_SIGKILL) | signal_bit(RABBITBONE_SIGSTOP));
    return m;
}

static u32 process_next_deliverable_signal(const active_process_t *p) {
    if (!p) return 0;
    u64 pending = p->signal_pending;
    u64 blocked = p->signal_blocked & ~(signal_bit(RABBITBONE_SIGKILL) | signal_bit(RABBITBONE_SIGSTOP));
    pending &= ~blocked;
    for (u32 sig = 1; sig < RABBITBONE_NSIG; ++sig) {
        if (pending & signal_bit(sig)) return sig;
    }
    return 0;
}

static active_process_t *process_find_any(u32 pid) {
    if (!pid) return 0;
    if (current_proc.active && current_proc.result.pid == pid) return &current_proc;
    return slot_by_pid(pid, 0);
}

static bool process_group_exists(u32 pgid) {
    if (!pgid) return false;
    if (current_proc.active && current_proc.process_group == pgid) return true;
    if (async_slots) {
        for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) if (async_slots[i].active && async_slots[i].process_group == pgid) return true;
    }
    return false;
}

static bool process_signal_queue_one(active_process_t *p, u32 sig) {
    if (!p || !p->active || !signal_valid(sig)) return false;
    p->signal_pending |= signal_bit(sig);
    if (sig == RABBITBONE_SIGCONT && p->state == PROCESS_STATE_STOPPED) {
        p->state = PROCESS_STATE_READY;
        p->signal_flags &= ~RABBITBONE_PROCESS_FLAG_STOPPED;
    }
    if (current_proc.active && p == &current_proc) return sync_current_to_slot();
    return true;
}

process_status_t process_signal_send(u32 pid, u32 sig) {
    if (!signal_valid(sig)) return PROC_ERR_INVAL;
    bool any = false;
    if (pid == 0) {
        u32 pgid = current_proc.active ? current_proc.process_group : 0;
        if (!pgid) return PROC_ERR_INVAL;
        if (current_proc.active && current_proc.process_group == pgid) any |= process_signal_queue_one(&current_proc, sig);
        if (async_slots) for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) if (async_slots[i].active && async_slots[i].process_group == pgid) any |= process_signal_queue_one(&async_slots[i], sig);
        return any ? PROC_OK : PROC_ERR_INVAL;
    }
    if ((i32)pid < 0) {
        u32 pgid = (u32)(-(i32)pid);
        if (!pgid) return PROC_ERR_INVAL;
        if (current_proc.active && current_proc.process_group == pgid) any |= process_signal_queue_one(&current_proc, sig);
        if (async_slots) for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) if (async_slots[i].active && async_slots[i].process_group == pgid) any |= process_signal_queue_one(&async_slots[i], sig);
        return any ? PROC_OK : PROC_ERR_INVAL;
    }
    active_process_t *p = process_find_any(pid);
    if (!p) return PROC_ERR_INVAL;
    return process_signal_queue_one(p, sig) ? PROC_OK : PROC_ERR_INVAL;
}

process_status_t process_signal_raise(u32 sig) { return current_proc.active ? process_signal_send(current_proc.result.pid, sig) : PROC_ERR_INVAL; }

process_status_t process_signal_set_action(u32 sig, const rabbitbone_sigaction_t *act, rabbitbone_sigaction_t *oldact) {
    if (!current_proc.active || !signal_valid(sig)) return PROC_ERR_INVAL;
    if ((sig == RABBITBONE_SIGKILL || sig == RABBITBONE_SIGSTOP) && act) return PROC_ERR_INVAL;
    if (act && act->handler > 1) {
        if (!act->restorer) return PROC_ERR_INVAL;
        if (!user_addr_range_valid((uptr)act->handler, 1u) || !user_addr_range_valid((uptr)act->restorer, 1u)) return PROC_ERR_RANGE;
    }
    process_signal_action_t *a = &current_proc.signal_actions[sig];
    if (oldact) {
        memset(oldact, 0, sizeof(*oldact));
        oldact->handler = a->handler;
        oldact->mask = a->mask;
        oldact->flags = a->flags;
        oldact->restorer = a->restorer;
    }
    if (act) {
        a->handler = (uptr)act->handler;
        a->mask = signal_sanitized_mask(act->mask);
        a->flags = act->flags & RABBITBONE_SA_RESTART;
        a->restorer = (uptr)act->restorer;
        if (a->handler > 1 && !a->restorer) return PROC_ERR_INVAL;
    }
    return sync_current_to_slot() ? PROC_OK : PROC_ERR_INVAL;
}

process_status_t process_signal_mask(u32 how, const u64 *set, u64 *oldset) {
    if (!current_proc.active) return PROC_ERR_INVAL;
    if (oldset) *oldset = current_proc.signal_blocked;
    if (set) {
        u64 m = signal_sanitized_mask(*set);
        if (how == RABBITBONE_SIG_BLOCK) current_proc.signal_blocked |= m;
        else if (how == RABBITBONE_SIG_UNBLOCK) current_proc.signal_blocked &= ~m;
        else if (how == RABBITBONE_SIG_SETMASK) current_proc.signal_blocked = m;
        else return PROC_ERR_INVAL;
    }
    return sync_current_to_slot() ? PROC_OK : PROC_ERR_INVAL;
}

process_status_t process_signal_pending(u64 *out) {
    if (!current_proc.active || !out) return PROC_ERR_INVAL;
    *out = current_proc.signal_pending;
    return PROC_OK;
}

static bool process_signal_make_frame(active_process_t *p, cpu_regs_t *regs, u32 sig, const process_signal_action_t *a) {
    if (!p || !regs || !a || a->handler <= 1 || !a->restorer || p->signal_in_handler) return false;
    process_signal_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.restorer = a->restorer;
    frame.magic = PROCESS_SIGNAL_FRAME_MAGIC;
    frame.old_mask = p->signal_blocked;
    frame.signal = sig;
    frame.saved_regs = *regs;
    uptr frame_addr = ((regs->rsp - sizeof(frame) - 8u) & ~(uptr)0xfull) + 8u;
    if (!user_addr_range_valid(frame_addr, sizeof(frame))) return false;
    if (!copy_to_process_user_space(p, frame_addr, &frame, sizeof(frame))) return false;
    regs->rip = a->handler;
    regs->rdi = sig;
    regs->rsp = frame_addr;
    p->signal_blocked |= a->mask | signal_bit(sig);
    p->signal_blocked = signal_sanitized_mask(p->signal_blocked);
    p->signal_in_handler = true;
    return true;
}

/* returns 0 none, 1 handler/default-ignore processed, 2 terminated, 3 stopped */
static int process_signal_deliver_pending(active_process_t *p, cpu_regs_t *regs) {
    if (!p || !p->active || !regs) return 0;
    u32 sig = process_next_deliverable_signal(p);
    if (!sig) return 0;
    p->signal_pending &= ~signal_bit(sig);
    if (sig == RABBITBONE_SIGCONT) {
        if (p->state == PROCESS_STATE_STOPPED) p->state = PROCESS_STATE_READY;
        p->signal_flags &= ~RABBITBONE_PROCESS_FLAG_STOPPED;
        return 1;
    }
    process_signal_action_t *a = &p->signal_actions[sig];
    if (!signal_unmaskable(sig) && a->handler == RABBITBONE_SIG_IGN) return 1;
    if (!signal_unmaskable(sig) && a->handler > 1) {
        return process_signal_make_frame(p, regs, sig, a) ? 1 : 0;
    }
    if (signal_stop_default(sig)) {
        p->state = PROCESS_STATE_STOPPED;
        p->signal_flags |= RABBITBONE_PROCESS_FLAG_STOPPED;
        return 3;
    }
    if (signal_terminates_default(sig) || sig == RABBITBONE_SIGKILL) {
        p->result.exit_code = 128 + (i32)sig;
        p->result.faulted = false;
        finish_current(PROCESS_STATE_EXITED, PROC_OK, regs);
        wake_waiters(p->result.pid);
        return 2;
    }
    return 1;
}

static u64 signal_sanitize_user_rflags(u64 rflags) {
    /* Preserve ordinary arithmetic/direction flags, force IF and reserved bit 1,
     * clear IOPL/NT/VM/RF/AC/VIF/VIP/ID and all privileged/high bits. */
    const u64 user_flags = 0x0000000000000cd5ull;
    return (rflags & user_flags) | 0x202ull;
}

bool process_signal_return(uptr frame_addr, cpu_regs_t *regs) {
    if (!current_proc.active || !regs || !frame_addr) return false;
    process_signal_frame_t frame;
    if (!process_copy_from_user(&frame, frame_addr, sizeof(frame))) return false;
    if (frame.magic != PROCESS_SIGNAL_FRAME_MAGIC || frame.restorer == 0) return false;
    if (!user_addr_range_valid((uptr)frame.saved_regs.rip, 1u) || !user_addr_range_valid((uptr)frame.saved_regs.rsp, 1u)) return false;
    current_proc.signal_blocked = signal_sanitized_mask(frame.old_mask);
    current_proc.signal_in_handler = false;
    cpu_regs_t restored = frame.saved_regs;
    restored.cs = RABBITBONE_UCODE_SEL;
    restored.ss = RABBITBONE_UDATA_SEL;
    restored.rflags = signal_sanitize_user_rflags(restored.rflags);
    restored.vector = regs->vector;
    restored.error = regs->error;
    *regs = restored;
    current_proc.regs = restored;
    return sync_current_to_slot();
}

process_status_t process_get_pgrp(u32 pid, u32 *out) {
    if (!out) return PROC_ERR_INVAL;
    active_process_t *p = pid ? process_find_any(pid) : (current_proc.active ? &current_proc : 0);
    if (!p) return PROC_ERR_INVAL;
    *out = p->process_group;
    return PROC_OK;
}

process_status_t process_get_sid(u32 pid, u32 *out) {
    if (!out) return PROC_ERR_INVAL;
    active_process_t *p = pid ? process_find_any(pid) : (current_proc.active ? &current_proc : 0);
    if (!p) return PROC_ERR_INVAL;
    *out = p->session_id;
    return PROC_OK;
}

process_status_t process_set_pgrp(u32 pid, u32 pgid) {
    if (!current_proc.active) return PROC_ERR_INVAL;
    active_process_t *p = pid ? process_find_any(pid) : &current_proc;
    if (!p) return PROC_ERR_INVAL;
    if (p->result.pid != current_proc.result.pid && p->parent_pid != current_proc.result.pid) return PROC_ERR_INVAL;
    if (p->session_id != current_proc.session_id) return PROC_ERR_INVAL;
    if (p->result.pid == p->session_id) return PROC_ERR_INVAL;
    if (pgid == 0) pgid = p->result.pid;
    if (pgid != p->result.pid && !process_group_exists(pgid)) return PROC_ERR_INVAL;
    p->process_group = pgid;
    if (p == &current_proc) return sync_current_to_slot() ? PROC_OK : PROC_ERR_INVAL;
    return PROC_OK;
}

process_status_t process_set_sid(u32 *out) {
    if (!current_proc.active) return PROC_ERR_INVAL;
    if (process_group_exists(current_proc.result.pid)) return PROC_ERR_BUSY;
    current_proc.session_id = current_proc.result.pid;
    current_proc.process_group = current_proc.result.pid;
    current_proc.signal_flags |= RABBITBONE_PROCESS_FLAG_SESSION_LEADER;
    if (!tty_session_id) tty_session_id = current_proc.session_id;
    if (!tty_foreground_pgrp) tty_foreground_pgrp = current_proc.process_group;
    if (out) *out = current_proc.session_id;
    return sync_current_to_slot() ? PROC_OK : PROC_ERR_INVAL;
}

process_status_t process_tcgetpgrp(u32 *out) {
    if (!out || !current_proc.active) return PROC_ERR_INVAL;
    *out = tty_foreground_pgrp ? tty_foreground_pgrp : current_proc.process_group;
    return PROC_OK;
}

process_status_t process_tcsetpgrp(u32 pgid) {
    if (!current_proc.active || !pgid || !process_group_exists(pgid)) return PROC_ERR_INVAL;
    if (!process_cred_is_root(&current_proc.cred) && current_proc.session_id != tty_session_id && tty_session_id != 0) return PROC_ERR_INVAL;
    tty_session_id = current_proc.session_id;
    tty_foreground_pgrp = pgid;
    return PROC_OK;
}

void process_format_signals(char *out, usize cap) {
    if (!out || cap == 0) return;
    out[0] = 0;
    ksnprintf(out, cap, "signals: nsig=%u pending=0x%llx blocked=0x%llx current_pid=%u pgrp=%u sid=%u fg=%u\n",
              RABBITBONE_NSIG, (unsigned long long)(current_proc.active ? current_proc.signal_pending : 0),
              (unsigned long long)(current_proc.active ? current_proc.signal_blocked : 0),
              current_proc.active ? current_proc.result.pid : 0,
              current_proc.active ? current_proc.process_group : 0,
              current_proc.active ? current_proc.session_id : 0,
              tty_foreground_pgrp);
}

void process_format_jobs(char *out, usize cap) {
    if (!out || cap == 0) return;
    usize len = 0;
    len += (usize)ksnprintf(out + len, len < cap ? cap - len : 0, "jobs: fg=%u session=%u\n", tty_foreground_pgrp, tty_session_id);
    if (!async_slots) return;
    for (usize i = 0; i < PROCESS_ASYNC_CAP && len + 1u < cap; ++i) {
        active_process_t *p = &async_slots[i];
        if (!p->active) continue;
        len += (usize)ksnprintf(out + len, len < cap ? cap - len : 0, "  pid=%u ppid=%u pgrp=%u sid=%u state=%s flags=0x%x pending=0x%llx name=%s\n",
                                p->result.pid, p->parent_pid, p->process_group, p->session_id,
                                process_state_name((u32)p->state), p->signal_flags,
                                (unsigned long long)p->signal_pending, p->result.name);
    }
}
