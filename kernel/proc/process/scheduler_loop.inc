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

static bool pick_ready_slot(usize *idx_out) {
    if (!async_slots || !idx_out) return false;
    wake_sleepers();
    for (usize scan = 0; scan < PROCESS_ASYNC_CAP; ++scan) {
        usize idx = (async_next_rr + scan) % PROCESS_ASYNC_CAP;
        if (async_slots[idx].active && async_slots[idx].state == PROCESS_STATE_READY) {
            *idx_out = idx;
            async_next_rr = (u32)((idx + 1u) % PROCESS_ASYNC_CAP);
            return true;
        }
    }
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

static void enter_ready_slot(usize idx) {
    load_slot_to_current(idx);
    vmm_switch_space(&current_proc.space);
    arch_user_resume(&current_proc.regs);
    cpu_sti();
}

AURORA_NORETURN void process_scheduler_loop(void) {
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
            enter_ready_slot(idx);
            continue;
        }
        if (!any_live_process()) {
            vmm_switch_kernel();
            clear_current_async();
            PANIC("persistent user scheduler has no live processes");
        }
        if (any_sleeping_process() || any_waiting_process()) {
            cpu_sti();
            cpu_hlt();
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
            if (!any_live_process()) break;
        } else if (!root) {
            break;
        }
        usize idx = 0;
        if (!pick_ready_slot(&idx)) {
            if (!any_live_process()) break;
            if (!any_sleeping_process() && !any_waiting_process()) {
                fault_wait_deadlock();
                continue;
            }
            cpu_sti();
            cpu_hlt();
            continue;
        }
        enter_ready_slot(idx);
        continue;
    }
    vmm_switch_kernel();
    syscall_reset_user_handles();
    active_process_t *root = slot_by_pid(root_pid, 0);
    if (root && process_state_terminal((u32)root->state)) found_root = true;
    if (root) {
        if (root_out) *root_out = root->result;
        last_result = root->result;
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
