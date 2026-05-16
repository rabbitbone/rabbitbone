void scheduler_note_yield(void) {
    ensure_init();
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    inc_u32_sat(&total_yields);
    scheduler_runtime_for_current_locked()->need_resched = 1u;
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

void scheduler_note_sleep(u64 ticks) {
    ensure_init();
    (void)ticks;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    inc_u32_sat(&total_sleeps);
    scheduler_runtime_for_current_locked()->need_resched = 1u;
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_set_quantum(u32 q) {
    ensure_init();
    if (q == 0 || q > SCHED_MAX_QUANTUM_TICKS) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    quantum_ticks = q;
    for (u32 i = 0; i < SMP_MAX_CPUS; ++i) sched_runtime[i].current_slice_ticks = 0;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

bool scheduler_tick(const cpu_regs_t *regs) {
    if (!initialized || !regs) return false;
    bool from_user = (regs->cs & 3u) == 3u;
    bool user_active = from_user && process_user_active();
    u32 pid = user_active ? process_current_pid() : 0;
    u32 cpu = scheduler_current_cpu_index();

    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(cpu);
    inc_u64_sat(&total_timer_ticks);
    if (!user_active) {
        inc_u64_sat(&kernel_ticks);
        rt->current_slice_pid = 0;
        rt->current_slice_ticks = 0;
        bool requested = __atomic_exchange_n(&rt->need_resched, 0u, __ATOMIC_ACQ_REL) != 0u;
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return requested;
    }
    inc_u64_sat(&user_ticks);
    if (pid == 0 || pid != rt->current_slice_pid) {
        rt->current_slice_pid = pid;
        rt->current_slice_ticks = 0;
    }
    if (rt->current_slice_ticks != 0xffffffffu) ++rt->current_slice_ticks;
    bool requested = __atomic_exchange_n(&rt->need_resched, 0u, __ATOMIC_ACQ_REL) != 0u;
    bool preempt = preempt_enabled && quantum_ticks && rt->current_slice_ticks >= quantum_ticks;
    if (preempt) {
        inc_u64_sat(&total_preemptions);
        last_preempt_ticks = sched_now();
        last_preempt_rip = regs->rip;
        rt->current_slice_ticks = 0;
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return preempt || requested;
}

bool scheduler_preempt_info(rabbitbone_preemptinfo_t *out) {
    ensure_init();
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_current_locked();
    out->enabled = preempt_enabled ? 1u : 0u;
    out->quantum_ticks = quantum_ticks;
    out->current_pid = rt->current_slice_pid;
    out->current_slice_ticks = rt->current_slice_ticks;
    out->total_timer_ticks = total_timer_ticks;
    out->user_ticks = user_ticks;
    out->kernel_ticks = kernel_ticks;
    out->total_preemptions = total_preemptions;
    out->last_preempt_ticks = last_preempt_ticks;
    out->last_preempt_rip = last_preempt_rip;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

void scheduler_idle_enter(void) {
    if (!initialized) return;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_current_locked();
    if (!rt->idle) {
        rt->idle = 1u;
        ++rt->idle_entries;
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

void scheduler_idle_exit(void) {
    if (!initialized) return;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_current_locked();
    if (rt->idle) {
        rt->idle = 0u;
        ++rt->idle_ticks;
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_need_resched(void) {
    if (!initialized) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    bool need = scheduler_runtime_for_current_locked()->need_resched != 0u;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return need;
}

void scheduler_note_reschedule_ipi(u32 cpu_id) {
    if (!initialized || cpu_id >= SMP_MAX_CPUS) return;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(cpu_id);
    __atomic_store_n(&rt->need_resched, 1u, __ATOMIC_RELEASE);
    ++rt->reschedule_ipis;
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_request_reschedule(u32 cpu_id) {
    ensure_init();
    if (cpu_id >= SMP_MAX_CPUS) return false;
    u32 current = scheduler_current_cpu_index();
    if (cpu_id == current) {
        u64 flags = spin_lock_irqsave(&scheduler_lock);
        sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(cpu_id);
        __atomic_store_n(&rt->need_resched, 1u, __ATOMIC_RELEASE);
        ++rt->local_reschedules;
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return true;
    }
    if (!smp_cpu_is_targetable(cpu_id)) return false;

    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(cpu_id);
    __atomic_store_n(&rt->need_resched, 1u, __ATOMIC_RELEASE);
    spin_unlock_irqrestore(&scheduler_lock, flags);

    return smp_send_reschedule_ipi(cpu_id);
}

u32 scheduler_request_reschedule_mask(u32 cpu_mask) {
    ensure_init();
    u32 delivered = 0;
    u32 started = smp_started_cpu_mask();
    cpu_mask &= started ? started : 1u;
    for (u32 cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
        if ((cpu_mask & (1u << cpu)) && scheduler_request_reschedule(cpu)) ++delivered;
    }
    return delivered;
}


void scheduler_note_kernel_task_dispatch(void) {
    if (!initialized) return;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    ++scheduler_runtime_for_current_locked()->kernel_task_dispatches;
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_cpu_runtime_info(u32 cpu_id, sched_cpu_runtime_info_t *out) {
    ensure_init();
    if (!out || cpu_id >= SMP_MAX_CPUS) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    if (cpu_id >= sched_cpu_count_cached) {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(cpu_id);
    memset(out, 0, sizeof(*out));
    out->cpu_id = cpu_id;
    out->current_pid = rt->current_slice_pid;
    out->current_slice_ticks = rt->current_slice_ticks;
    out->need_resched = rt->need_resched;
    out->idle = rt->idle;
    out->idle_entries = rt->idle_entries;
    out->idle_ticks = rt->idle_ticks;
    out->kernel_task_dispatches = rt->kernel_task_dispatches;
    out->process_dispatches = rt->process_dispatches;
    out->reschedule_ipis = rt->reschedule_ipis;
    out->local_reschedules = rt->local_reschedules;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}
