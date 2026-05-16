process_status_t process_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp, process_result_t *out) {
    if (!path || !*path || argc < 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    if (async_scheduler_active || current_proc.active) return PROC_ERR_INVAL;
    if (!async_slots) return PROC_ERR_NOMEM;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active) return PROC_ERR_BUSY;
    }
    memset(async_slots, 0, sizeof(active_process_t) * PROCESS_ASYNC_CAP);
    async_next_rr = 0;
    u32 pid = 0;
    active_process_t *slot = alloc_async_slot();
    process_status_t st = slot ? prepare_process_env(slot, path, argc, argv, envc, envp, 0) : PROC_ERR_NOMEM;
    if (st == PROC_OK) pid = slot->result.pid;
    if (st != PROC_OK) {
        if (out) memset(out, 0, sizeof(*out));
        return st;
    }
    process_result_t result;
    memset(&result, 0, sizeof(result));
    bool ran = process_run_until_idle(pid, &result);
    if (!ran && result.pid != 0) {
        process_info_t info;
        if (process_lookup(result.pid, &info) && process_state_terminal(info.state)) ran = true;
    }
    if (out) *out = result;
    if (!ran) return PROC_ERR_FAULT;
    return result.faulted ? PROC_ERR_FAULT : PROC_OK;
}

process_status_t process_exec(const char *path, int argc, const char *const *argv, process_result_t *out) {
    return process_execve(path, argc, argv, 0, 0, out);
}

process_status_t process_spawn(const char *path, int argc, const char *const *argv, u32 *pid_out, process_result_t *out) {
    process_result_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    process_status_t st = process_exec(path, argc, argv, &tmp);
    if (st == PROC_OK) {
        if (pid_out) *pid_out = tmp.pid;
        if (out) *out = tmp;
    } else {
        if (pid_out) *pid_out = 0;
        if (out) memset(out, 0, sizeof(*out));
    }
    return st;
}

bool process_wait(u32 pid, process_info_t *out) {
    if (!pid || !out) return false;
    process_info_t info;
    if (!process_lookup(pid, &info)) return false;
    if (process_state_live(info.state)) return false;
    *out = info;
    return true;
}


void process_request_reschedule(void) {
    if (!process_async_scheduler_active()) return;
    reschedule_requested = true;
}

void process_request_sleep(u64 ticks) {
    if (!process_async_scheduler_active()) return;
    reschedule_requested = true;
    current_proc.state = PROCESS_STATE_SLEEPING;
    u64 now = pit_ticks();
    current_proc.wake_tick = ticks > 0xffffffffffffffffull - now ? 0xffffffffffffffffull : now + ticks;
    if (ticks == 0) current_proc.state = PROCESS_STATE_READY;
}

void process_request_exit(i32 code) {
    if (!process_async_scheduler_active()) return;
    requested_exit_code = code;
    exit_requested = true;
}

bool process_request_wait(u32 pid, uptr out_ptr) {
    if (!process_async_scheduler_active() || !pid || pid == current_proc.result.pid || !out_ptr) return false;
    if (!process_validate_user_range(out_ptr, sizeof(process_info_t), true)) return false;
    active_process_t *target = slot_by_pid(pid, 0);
    if (!target || target->parent_pid != current_proc.result.pid) return false;
    process_info_t info;
    fill_info_from_active(&info, target);
    if (process_state_terminal((u32)target->state)) {
        if (!copy_to_process_user_space(&current_proc, out_ptr, &info, sizeof(info))) return false;
        release_process_slot(target);
        return true;
    }
    if (!process_state_live((u32)target->state)) return false;
    current_proc.state = PROCESS_STATE_WAITING;
    current_proc.wait_pid = pid;
    current_proc.wait_out_ptr = out_ptr;
    reschedule_requested = true;
    return true;
}

bool process_request_fork(void) {
    if (!process_async_scheduler_active()) return false;
    fork_requested = true;
    return true;
}

process_status_t process_request_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp) {
    if (!process_async_scheduler_active() || !current_proc.active) return PROC_ERR_INVAL;
    if (exec_requested) discard_exec_replacement();
    if (!exec_replacement) return PROC_ERR_NOMEM;
    process_status_t st = prepare_exec_replacement_env(exec_replacement, path, argc, argv, envc, envp);
    if (st != PROC_OK) return st;
    exec_requested = true;
    return PROC_OK;
}

process_status_t process_request_exec(const char *path, int argc, const char *const *argv) {
    return process_request_execve(path, argc, argv, 0, 0);
}


bool process_migrate(u32 pid, u32 dst_cpu) {
    if (!pid || dst_cpu >= SMP_MAX_CPUS) return false;
    u32 mask = smp_started_cpu_mask();
    if (mask == 0) mask = 1u;
    if ((mask & (1u << dst_cpu)) == 0) return false;
    active_process_t *p = slot_by_pid(pid, 0);
    if (!p || !p->active || process_state_terminal((u32)p->state)) return false;
    u32 old = p->home_cpu;
    p->home_cpu = dst_cpu;
    p->last_cpu = dst_cpu;
    p->affinity_mask = mask;
    if (old != dst_cpu) ++p->migration_count;
    if (current_proc.active && current_proc.result.pid == pid) {
        current_proc.home_cpu = p->home_cpu;
        current_proc.last_cpu = p->last_cpu;
        current_proc.affinity_mask = p->affinity_mask;
        current_proc.migration_count = p->migration_count;
    }
    return true;
}


bool process_set_affinity(u32 pid, u32 cpu_mask) {
    if (!pid) return false;
    u32 started = smp_started_cpu_mask();
    if (started == 0u) started = 1u;
    cpu_mask &= started;
    if (cpu_mask == 0u) return false;
    active_process_t *p = slot_by_pid(pid, 0);
    if (!p || !p->active || process_state_terminal((u32)p->state)) return false;
    u32 old_home = p->home_cpu;
    if ((cpu_mask & (1u << p->home_cpu)) == 0u) {
        for (u32 cpu = 0; cpu < SMP_MAX_CPUS; ++cpu) {
            if (cpu_mask & (1u << cpu)) { p->home_cpu = cpu; break; }
        }
    }
    p->affinity_mask = cpu_mask;
    if ((cpu_mask & (1u << p->last_cpu)) == 0u) p->last_cpu = p->home_cpu;
    if (old_home != p->home_cpu) ++p->migration_count;
    if (current_proc.active && current_proc.result.pid == pid) {
        current_proc.home_cpu = p->home_cpu;
        current_proc.last_cpu = p->last_cpu;
        current_proc.affinity_mask = p->affinity_mask;
        current_proc.migration_count = p->migration_count;
    }
    return true;
}

bool process_cpu_info(u32 pid, process_cpu_info_t *out) {
    if (!pid || !out) return false;
    active_process_t *p = slot_by_pid(pid, 0);
    if (!p || !p->active) return false;
    memset(out, 0, sizeof(*out));
    out->pid = p->result.pid;
    out->home_cpu = p->home_cpu;
    out->last_cpu = p->last_cpu;
    out->affinity_mask = p->affinity_mask;
    out->migrations = p->migration_count;
    out->state = (u32)p->state;
    return true;
}
