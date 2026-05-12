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
    out->current_slice_ticks = current_slice_ticks;
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

