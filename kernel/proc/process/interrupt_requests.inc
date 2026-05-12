static AURORA_NORETURN void leave_user_scheduler(void) {
    vmm_switch_kernel();
    arch_user_return_from_interrupt();
}

void process_preempt_from_interrupt(cpu_regs_t *regs) {
    if (!async_scheduler_active || !current_proc.active || !regs) return;
    if (!resolve_current_async_slot()) {
        current_proc.result.exit_code = -128;
        current_proc.result.faulted = true;
        current_proc.result.fault_vector = 0xffu;
        current_proc.result.fault_rip = regs->rip;
        current_proc.result.fault_addr = 0;
        finish_current(PROCESS_STATE_FAULTED, PROC_ERR_FAULT, regs);
        clear_current_async();
        leave_user_scheduler();
    }
    current_proc.regs = *regs;
    if (current_proc.state == PROCESS_STATE_RUNNING) current_proc.state = PROCESS_STATE_READY;
    sync_current_to_slot();
    clear_current_async();
    leave_user_scheduler();
}

void process_fault_current_from_interrupt(cpu_regs_t *regs, u64 vector, u64 cr2) {
    if (!async_scheduler_active || !current_proc.active || !resolve_current_async_slot()) {
        process_fault_from_interrupt(vector, regs ? regs->rip : 0, cr2);
    }
    current_proc.result.exit_code = -128 - (i32)vector;
    current_proc.result.faulted = true;
    current_proc.result.fault_vector = vector;
    current_proc.result.fault_rip = regs ? regs->rip : 0;
    current_proc.result.fault_addr = cr2;
    finish_current(PROCESS_STATE_FAULTED, PROC_ERR_FAULT, regs);
    wake_waiters(current_proc.result.pid);
    clear_current_async();
    leave_user_scheduler();
}

bool process_after_syscall(cpu_regs_t *regs) {
    if (!async_scheduler_active || !current_proc.active || !regs) return false;
    if (!resolve_current_async_slot()) return false;
    if (fork_requested) {
        u32 child_pid = 0;
        if (fork_current_from_regs(regs, &child_pid)) {
            regs->rax = child_pid;
            regs->rdx = 0;
        } else {
            regs->rax = (u64)-1ll;
            regs->rdx = (u64)-4ll;
        }
        fork_requested = false;
    }
    if (exec_requested) {
        commit_exec_replacement(regs);
        return false;
    }
    if (exit_requested) {
        current_proc.result.exit_code = requested_exit_code;
        finish_current(PROCESS_STATE_EXITED, PROC_OK, regs);
        wake_waiters(current_proc.result.pid);
        exit_requested = false;
        requested_exit_code = 0;
        clear_current_async();
        leave_user_scheduler();
    }
    if (reschedule_requested || current_proc.state == PROCESS_STATE_SLEEPING || current_proc.state == PROCESS_STATE_WAITING) {
        if (current_proc.state == PROCESS_STATE_RUNNING) current_proc.state = PROCESS_STATE_READY;
        reschedule_requested = false;
        process_preempt_from_interrupt(regs);
        return true;
    }
    return false;
}


AURORA_NORETURN void process_exit_current_from_syscall(cpu_regs_t *regs, i32 code) {
    if (current_proc.active && async_scheduler_active) {
        (void)resolve_current_async_slot();
        current_proc.result.exit_code = code;
        finish_current(PROCESS_STATE_EXITED, PROC_OK, regs);
        wake_waiters(current_proc.result.pid);
        exit_requested = false;
        requested_exit_code = 0;
        fork_requested = false;
        clear_current_async();
        leave_user_scheduler();
    }
    if (current_proc.active) {
        current_proc.result.exit_code = code;
        current_proc.returned = true;
    }
    arch_user_return_from_interrupt();
}

AURORA_NORETURN void process_exit_from_interrupt(i32 code) {
    if (current_proc.active) {
        current_proc.result.exit_code = code;
        current_proc.returned = true;
        KLOG(LOG_INFO, "process", "exit pid=%u code=%d", current_proc.result.pid, code);
    }
    arch_user_return_from_interrupt();
}

AURORA_NORETURN void process_fault_from_interrupt(u64 vector, u64 rip, u64 cr2) {
    if (current_proc.active) {
        current_proc.result.exit_code = -128 - (i32)vector;
        current_proc.result.faulted = true;
        current_proc.result.fault_vector = vector;
        current_proc.result.fault_rip = rip;
        current_proc.result.fault_addr = cr2;
        KLOG(LOG_ERROR, "process", "fault pid=%u vector=%llu rip=%p cr2=%p", current_proc.result.pid,
             (unsigned long long)vector, (void *)(uptr)rip, (void *)(uptr)cr2);
    }
    arch_user_return_from_interrupt();
}

