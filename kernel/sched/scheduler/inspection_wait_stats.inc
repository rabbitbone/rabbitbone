bool scheduler_get_job(u32 job_id, sched_job_info_t *out) {
    ensure_init();
    if (!job_id || !out || !initialized) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_job_t *job = find_job_locked(job_id);
    if (job) {
        *out = job->info;
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return true;
    }
    if (!history) { spin_unlock_irqrestore(&scheduler_lock, flags); return false; }
    for (usize i = 0; i < history_len; ++i) {
        usize idx = (history_next + SCHED_RESULT_RING - 1u - i) % SCHED_RESULT_RING;
        if (history[idx].job_id == job_id) {
            *out = history[idx];
            spin_unlock_irqrestore(&scheduler_lock, flags);
            return true;
        }
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return false;
}

bool scheduler_wait_job(u32 job_id, sched_job_info_t *out) {
    ensure_init();
    sched_job_info_t info;
    if (!scheduler_get_job(job_id, &info)) return false;
    if (info.state == SCHED_JOB_QUEUED) {
        sched_job_t *job = 0;
        u64 flags = spin_lock_irqsave(&scheduler_lock);
        job = find_job_locked(job_id);
        spin_unlock_irqrestore(&scheduler_lock, flags);
        if (!job) return false;
        (void)run_one(job);
        if (!scheduler_get_job(job_id, &info)) return false;
    } else if (info.state == SCHED_JOB_RUNNING) {
        return false;
    }
    if (info.state != SCHED_JOB_DONE && info.state != SCHED_JOB_FAILED) return false;
    if (out) *out = info;
    return true;
}

void scheduler_stats(sched_stats_t *out) {
    ensure_init();
    if (!out) return;
    memset(out, 0, sizeof(*out));
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    out->queue_capacity = SCHED_QUEUE_CAP;
    out->next_job_id = next_job_id;
    out->total_enqueued = total_enqueued;
    out->total_dispatched = total_dispatched;
    out->total_yields = total_yields;
    out->total_sleeps = total_sleeps;
    out->last_dispatch_ticks = sched_now();
    out->preempt_enabled = preempt_enabled ? 1u : 0u;
    out->quantum_ticks = quantum_ticks;
    out->current_slice_ticks = scheduler_runtime_for_current_locked()->current_slice_ticks;
    out->total_timer_ticks = total_timer_ticks;
    out->user_ticks = user_ticks;
    out->kernel_ticks = kernel_ticks;
    out->total_preemptions = total_preemptions;
    if (queue) {
        for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
            switch (queue[i].info.state) {
                case SCHED_JOB_EMPTY: break;
                case SCHED_JOB_QUEUED: ++out->queued; break;
                case SCHED_JOB_RUNNING: ++out->running; break;
                case SCHED_JOB_DONE: ++out->completed; break;
                case SCHED_JOB_FAILED: ++out->failed; break;
                default: break;
            }
        }
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
}


bool scheduler_migrate_job(u32 job_id, u32 dst_cpu) {
    ensure_init();
    if (!job_id || !initialized) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    sched_job_t *job = find_job_locked(job_id);
    bool ok = scheduler_migrate_job_locked(job, dst_cpu);
    u32 wake_cpu = ok && job ? job->target_cpu : dst_cpu;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    if (ok) (void)scheduler_request_reschedule(wake_cpu);
    return ok;
}

u32 scheduler_balance_queues(void) {
    ensure_init();
    if (!initialized) return 0;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    scheduler_recount_cpu_states_locked();
    u32 moved = 0;
    for (;;) {
        u32 busiest = 0, emptiest = 0;
        u32 maxq = 0, minq = 0xffffffffu;
        for (u32 i = 0; i < sched_cpu_count_cached; ++i) {
            u32 q = scheduler_queue_depth_for_cpu_locked(i);
            if (q > maxq) { maxq = q; busiest = i; }
            if (q < minq) { minq = q; emptiest = i; }
        }
        if (maxq <= minq + 1u || busiest == emptiest) break;
        sched_job_t *candidate = 0;
        for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
            if (queue[i].info.state == SCHED_JOB_QUEUED && queue[i].target_cpu == busiest) {
                candidate = &queue[i];
                break;
            }
        }
        if (!candidate || !scheduler_migrate_job_locked(candidate, emptiest)) break;
        ++moved;
    }
    scheduler_recount_cpu_states_locked();
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return moved;
}

u32 scheduler_cpu_count(void) {
    ensure_init();
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    u32 n = sched_cpu_count_cached;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return n;
}

bool scheduler_cpu_info(u32 cpu_id, sched_cpu_info_t *out) {
    ensure_init();
    if (!out) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    if (cpu_id >= sched_cpu_count_cached) {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    scheduler_recount_cpu_states_locked();
    out->cpu_id = sched_cpus[cpu_id].cpu_id;
    out->queued = sched_cpus[cpu_id].queued;
    out->running = sched_cpus[cpu_id].running;
    out->completed = sched_cpus[cpu_id].completed;
    out->failed = sched_cpus[cpu_id].failed;
    out->migrations_in = sched_cpus[cpu_id].migrations_in;
    out->migrations_out = sched_cpus[cpu_id].migrations_out;
    out->dispatched = sched_cpus[cpu_id].dispatched;
    out->last_dispatch_ticks = sched_cpus[cpu_id].last_dispatch_ticks;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}
