static void wake_sleepers(void) {
    if (!async_slots) return;
    u64 now = pit_ticks();
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_SLEEPING && p->wake_tick <= now) {
            p->state = PROCESS_STATE_READY;
            p->wake_tick = 0;
        }
    }
}

static bool any_live_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && process_state_live((u32)async_slots[i].state)) return true;
    }
    return false;
}

static bool any_sleeping_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && async_slots[i].state == PROCESS_STATE_SLEEPING) return true;
    }
    return false;
}

static bool any_waiting_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && async_slots[i].state == PROCESS_STATE_WAITING) return true;
    }
    return false;
}

static bool any_stopped_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && async_slots[i].state == PROCESS_STATE_STOPPED) return true;
    }
    return false;
}

static void fault_wait_deadlock(void) {
    if (!async_slots) return;
    u64 now = pit_ticks();
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_WAITING) {
            p->state = PROCESS_STATE_FAULTED;
            p->result.exit_code = -127;
            p->result.faulted = true;
            p->result.fault_vector = 0xfeu;
            p->result.fault_rip = p->regs.rip;
            p->result.fault_addr = p->wait_pid;
            p->result.finished_ticks = now;
            p->wait_pid = 0;
            p->wait_out_ptr = 0;
            reparent_children(p->result.pid, p->result.pid == 1u ? 0u : 1u);
            release_process_fd_snapshot(p);
            record_process_result(&p->result, PROC_ERR_FAULT, p->result.started_ticks, now);
            wake_waiters(p->result.pid);
        }
    }
}


static bool ready_slot_has_valid_user_context(const active_process_t *p) {
    return p && p->active && p->state == PROCESS_STATE_READY && p->space.pml4_physical != 0u && p->regs.rip != 0u && p->regs.rsp != 0u;
}

static void fault_invalid_ready_slot_locked(active_process_t *p, u64 now) {
    if (!p || !p->active || p->state != PROCESS_STATE_READY) return;
    if (p->space.pml4_physical != 0u && p->regs.rip != 0u && p->regs.rsp != 0u) return;
    p->state = PROCESS_STATE_FAULTED;
    p->result.exit_code = -127;
    p->result.faulted = true;
    p->result.fault_vector = 0xfdu;
    p->result.fault_rip = p->regs.rip;
    p->result.fault_addr = p->space.pml4_physical;
    p->result.finished_ticks = now;
}

static void sanitize_invalid_ready_slots_locked(void) {
    if (!async_slots) return;
    u64 now = pit_ticks();
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) fault_invalid_ready_slot_locked(&async_slots[i], now);
}

static bool pick_ready_slot(usize *idx_out) {
    if (idx_out) *idx_out = PROCESS_ASYNC_CAP;
    if (!async_slots || !idx_out) return false;
    u32 cpu = smp_current_cpu_id();
    if (cpu >= SMP_MAX_CPUS) cpu = 0;
    u32 cpu_bit = 1u << cpu;
    u64 flags = spin_lock_irqsave(&process_sched_lock);
    wake_sleepers();
    sanitize_invalid_ready_slots_locked();

    for (usize scan = 0; scan < PROCESS_ASYNC_CAP; ++scan) {
        usize idx = (async_next_rr + scan) % PROCESS_ASYNC_CAP;
        active_process_t *p = &async_slots[idx];
        if (ready_slot_has_valid_user_context(p) && p->home_cpu == cpu &&
            ((p->affinity_mask == 0u) || (p->affinity_mask & cpu_bit))) {
            p->state = PROCESS_STATE_RUNNING;
            *idx_out = idx;
            async_next_rr = (u32)((idx + 1u) % PROCESS_ASYNC_CAP);
            spin_unlock_irqrestore(&process_sched_lock, flags);
            return true;
        }
    }

    for (usize scan = 0; scan < PROCESS_ASYNC_CAP; ++scan) {
        usize idx = (async_next_rr + scan) % PROCESS_ASYNC_CAP;
        active_process_t *p = &async_slots[idx];
        if (ready_slot_has_valid_user_context(p) &&
            ((p->affinity_mask == 0u) || (p->affinity_mask & cpu_bit))) {
            p->state = PROCESS_STATE_RUNNING;
            *idx_out = idx;
            async_next_rr = (u32)((idx + 1u) % PROCESS_ASYNC_CAP);
            spin_unlock_irqrestore(&process_sched_lock, flags);
            return true;
        }
    }

    for (usize scan = 0; scan < PROCESS_ASYNC_CAP; ++scan) {
        usize idx = (async_next_rr + scan) % PROCESS_ASYNC_CAP;
        active_process_t *p = &async_slots[idx];
        if (ready_slot_has_valid_user_context(p)) {
            p->state = PROCESS_STATE_RUNNING;
            *idx_out = idx;
            async_next_rr = (u32)((idx + 1u) % PROCESS_ASYNC_CAP);
            spin_unlock_irqrestore(&process_sched_lock, flags);
            return true;
        }
    }
    spin_unlock_irqrestore(&process_sched_lock, flags);
    return false;
}

static void dispose_terminal_slots_except(u32 keep_pid) {
    if (!async_slots) return;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->result.pid != keep_pid && process_state_terminal((u32)p->state)) {
            release_process_slot(p);
        }
    }
}

static void dispose_run_until_idle_children(u32 root_pid) {
    if (!async_slots || !root_pid) return;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (!p->active || p->result.pid == root_pid) continue;
        if (p->parent_pid == root_pid || p->parent_pid == 1u || process_state_live((u32)p->state)) {
            if (process_state_live((u32)p->state)) {
                p->state = PROCESS_STATE_EXITED;
                p->result.exit_code = 128 + (i32)RABBITBONE_SIGKILL;
                p->result.finished_ticks = pit_ticks();
                p->result.faulted = false;
                record_process_result(&p->result, PROC_OK, p->result.started_ticks, p->result.finished_ticks);
            }
            release_process_slot(p);
        }
    }
}

static bool enter_ready_slot(usize idx) {
    if (!async_slots || idx >= PROCESS_ASYNC_CAP) {
        KLOG(LOG_ERROR, "process", "invalid ready slot index=%llu cap=%u", (unsigned long long)idx, PROCESS_ASYNC_CAP);
        clear_current_async();
        vmm_switch_kernel();
        return false;
    }
    load_slot_to_current(idx);
    if (!current_proc.space.pml4_physical || !current_proc.regs.rip || !current_proc.regs.rsp) {
        KLOG(LOG_ERROR, "process", "invalid runnable context pid=%u slot=%llu pml4=%p rip=%p rsp=%p",
             current_proc.result.pid, (unsigned long long)idx, (void *)current_proc.space.pml4_physical,
             (void *)(uptr)current_proc.regs.rip, (void *)(uptr)current_proc.regs.rsp);
        fault_invalid_ready_slot_locked(&current_proc, pit_ticks());
        if (current_proc.state == PROCESS_STATE_RUNNING) current_proc.state = PROCESS_STATE_FAULTED;
        current_proc.result.exit_code = -127;
        current_proc.result.faulted = true;
        current_proc.result.fault_vector = 0xfdu;
        current_proc.result.fault_rip = current_proc.regs.rip;
        current_proc.result.fault_addr = current_proc.space.pml4_physical;
        current_proc.result.finished_ticks = pit_ticks();
        (void)sync_current_to_slot();
        clear_current_async();
        vmm_switch_kernel();
        return false;
    }
    int sig_result = process_signal_deliver_pending(&current_proc, &current_proc.regs);
    if (sig_result == 2 || sig_result == 3) {
        (void)sync_current_to_slot();
        clear_current_async();
        vmm_switch_kernel();
        return false;
    }
    if (sig_result == 1) (void)sync_current_to_slot();
    vmm_switch_space(&current_proc.space);
    arch_user_resume(&current_proc.regs);
    cpu_sti();
    return true;
}

RABBITBONE_NORETURN void process_scheduler_loop(void) {
    if (!async_slots) PANIC("user scheduler loop started before process_init");
    async_scheduler_active = true;
    reschedule_requested = false;
    exit_requested = false;
    fork_requested = false;
    requested_exit_code = 0;
    KLOG(LOG_INFO, "process", "persistent user scheduler loop started");

    for (;;) {
        usize idx = 0;
        if (pick_ready_slot(&idx)) {
            (void)enter_ready_slot(idx);
            continue;
        }
        if (!any_live_process()) {
            vmm_switch_kernel();
            clear_current_async();
            PANIC("persistent user scheduler has no live processes");
        }
        if (any_sleeping_process() || any_waiting_process() || any_stopped_process()) {
            smp_monitor_tick();
            task_run_ready(1);
            scheduler_idle_enter();
            cpu_sti();
            cpu_hlt();
            scheduler_idle_exit();
            continue;
        }
        fault_wait_deadlock();
    }
}

bool process_run_until_idle(u32 root_pid, process_result_t *root_out) {
    if (!async_slots || !root_pid) return false;
    arch_user_return_context_t caller_arch_ctx;
    arch_user_context_save(&caller_arch_ctx);
    bool found_root = false;
    async_scheduler_active = true;
    for (;;) {
        active_process_t *root = slot_by_pid(root_pid, 0);
        if (root && process_state_terminal((u32)root->state)) {
            found_root = true;
            break;
        } else if (!root) {
            break;
        }
        usize idx = 0;
        if (!pick_ready_slot(&idx)) {
            if (!any_live_process()) break;
            if (!any_sleeping_process() && !any_waiting_process() && !any_stopped_process()) {
                fault_wait_deadlock();
                continue;
            }
            smp_monitor_tick();
            task_run_ready(1);
            scheduler_idle_enter();
            cpu_sti();
            cpu_hlt();
            scheduler_idle_exit();
            continue;
        }
        (void)enter_ready_slot(idx);
        continue;
    }
    vmm_switch_kernel();
    syscall_reset_user_handles();
    active_process_t *root = slot_by_pid(root_pid, 0);
    if (root && process_state_terminal((u32)root->state)) found_root = true;
    if (root) {
        if (root_out) *root_out = root->result;
        last_result = root->result;
        dispose_run_until_idle_children(root_pid);
        release_process_slot(root);
    }
    dispose_terminal_slots_except(root_pid);
    clear_current_async();
    async_scheduler_active = false;
    reschedule_requested = false;
    exit_requested = false;
    fork_requested = false;
    discard_exec_replacement();
    requested_exit_code = 0;
    arch_user_context_restore(&caller_arch_ctx);
    return found_root;
}
