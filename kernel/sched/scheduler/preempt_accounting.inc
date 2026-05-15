void scheduler_note_yield(void) {
    ensure_init();
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    inc_u32_sat(&total_yields);
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

void scheduler_note_sleep(u64 ticks) {
    ensure_init();
    (void)ticks;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    inc_u32_sat(&total_sleeps);
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_set_quantum(u32 q) {
    ensure_init();
    if (q == 0 || q > SCHED_MAX_QUANTUM_TICKS) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    quantum_ticks = q;
    current_slice_ticks = 0;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

bool scheduler_tick(const cpu_regs_t *regs) {
    if (!initialized || !regs) return false;
    bool from_user = (regs->cs & 3u) == 3u;
    bool user_active = from_user && process_user_active();
    u32 pid = user_active ? process_current_pid() : 0;

    u64 flags = spin_lock_irqsave(&scheduler_lock);
    inc_u64_sat(&total_timer_ticks);
    if (!user_active) {
        inc_u64_sat(&kernel_ticks);
        current_slice_pid = 0;
        current_slice_ticks = 0;
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    inc_u64_sat(&user_ticks);
    if (pid == 0 || pid != current_slice_pid) {
        current_slice_pid = pid;
        current_slice_ticks = 0;
    }
    if (current_slice_ticks != 0xffffffffu) ++current_slice_ticks;
    bool preempt = preempt_enabled && quantum_ticks && current_slice_ticks >= quantum_ticks;
    if (preempt) {
        inc_u64_sat(&total_preemptions);
        last_preempt_ticks = sched_now();
        last_preempt_rip = regs->rip;
        current_slice_ticks = 0;
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return preempt;
}

bool scheduler_preempt_info(rabbitbone_preemptinfo_t *out) {
    ensure_init();
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    out->enabled = preempt_enabled ? 1u : 0u;
    out->quantum_ticks = quantum_ticks;
    out->current_pid = current_slice_pid;
    out->current_slice_ticks = current_slice_ticks;
    out->total_timer_ticks = total_timer_ticks;
    out->user_ticks = user_ticks;
    out->kernel_ticks = kernel_ticks;
    out->total_preemptions = total_preemptions;
    out->last_preempt_ticks = last_preempt_ticks;
    out->last_preempt_rip = last_preempt_rip;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

