static u64 sched_now(void) { return pit_ticks(); }

#define SCHED_SNAPSHOT_MAGIC 0x52424f4e52415343ull

typedef struct scheduler_snapshot {
    u64 magic;
    sched_job_t queue_copy[SCHED_QUEUE_CAP];
    sched_job_info_t history_copy[SCHED_RESULT_RING];
    bool had_queue;
    bool had_history;
    usize history_next_copy;
    usize history_len_copy;
    u32 next_job_id_copy;
    u32 total_enqueued_copy;
    u32 total_dispatched_copy;
    u32 total_yields_copy;
    u32 total_sleeps_copy;
    u32 sched_cpu_count_cached_copy;
    sched_cpu_queue_state_t sched_cpus_copy[SMP_MAX_CPUS];
    bool preempt_enabled_copy;
    u32 quantum_ticks_copy;
    sched_cpu_runtime_t sched_runtime_copy[SMP_MAX_CPUS];
    u64 total_timer_ticks_copy;
    u64 user_ticks_copy;
    u64 kernel_ticks_copy;
    u64 total_preemptions_copy;
    u64 last_preempt_ticks_copy;
    u64 last_preempt_rip_copy;
    bool initialized_copy;
} scheduler_snapshot_t;

static u32 scheduler_effective_cpu_count(void);
static void scheduler_reset_cpu_states_locked(void);
static u32 scheduler_queue_depth_for_cpu_locked(u32 cpu_id);
static u32 scheduler_pick_target_cpu_locked(void);
static void scheduler_recount_cpu_states_locked(void);
static bool scheduler_migrate_job_locked(sched_job_t *job, u32 dst_cpu);

usize scheduler_snapshot_size(void) { return sizeof(scheduler_snapshot_t); }

bool scheduler_snapshot_save(void *buffer, usize size) {
    if (!buffer || size < sizeof(scheduler_snapshot_t)) return false;
    scheduler_snapshot_t *snap = (scheduler_snapshot_t *)buffer;
    memset(snap, 0, sizeof(*snap));
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    snap->magic = SCHED_SNAPSHOT_MAGIC;
    snap->had_queue = queue != 0;
    snap->had_history = history != 0;
    if (queue) memcpy(snap->queue_copy, queue, sizeof(snap->queue_copy));
    if (history) memcpy(snap->history_copy, history, sizeof(snap->history_copy));
    snap->history_next_copy = history_next;
    snap->history_len_copy = history_len;
    snap->next_job_id_copy = next_job_id;
    snap->total_enqueued_copy = total_enqueued;
    snap->total_dispatched_copy = total_dispatched;
    snap->total_yields_copy = total_yields;
    snap->total_sleeps_copy = total_sleeps;
    snap->sched_cpu_count_cached_copy = sched_cpu_count_cached;
    memcpy(snap->sched_cpus_copy, sched_cpus, sizeof(snap->sched_cpus_copy));
    snap->preempt_enabled_copy = preempt_enabled;
    snap->quantum_ticks_copy = quantum_ticks;
    memcpy(snap->sched_runtime_copy, sched_runtime, sizeof(snap->sched_runtime_copy));
    snap->total_timer_ticks_copy = total_timer_ticks;
    snap->user_ticks_copy = user_ticks;
    snap->kernel_ticks_copy = kernel_ticks;
    snap->total_preemptions_copy = total_preemptions;
    snap->last_preempt_ticks_copy = last_preempt_ticks;
    snap->last_preempt_rip_copy = last_preempt_rip;
    snap->initialized_copy = initialized;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    return true;
}

bool scheduler_snapshot_restore(const void *buffer, usize size) {
    if (!buffer || size < sizeof(scheduler_snapshot_t)) return false;
    const scheduler_snapshot_t *snap = (const scheduler_snapshot_t *)buffer;
    if (snap->magic != SCHED_SNAPSHOT_MAGIC) return false;

    sched_job_t *prepared_queue = queue;
    sched_job_info_t *prepared_history = history;
    if (snap->had_queue && !prepared_queue) prepared_queue = (sched_job_t *)kmalloc(sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    if (snap->had_history && !prepared_history) prepared_history = (sched_job_info_t *)kmalloc(sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    if ((snap->had_queue && !prepared_queue) || (snap->had_history && !prepared_history)) return false;

    sched_job_t *queue_to_free = 0;
    sched_job_info_t *history_to_free = 0;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (snap->had_queue) {
        queue = prepared_queue;
        memcpy(queue, snap->queue_copy, sizeof(snap->queue_copy));
    } else {
        queue_to_free = queue;
        queue = 0;
    }
    if (snap->had_history) {
        history = prepared_history;
        memcpy(history, snap->history_copy, sizeof(snap->history_copy));
    } else {
        history_to_free = history;
        history = 0;
    }
    history_next = snap->history_next_copy;
    history_len = snap->history_len_copy;
    next_job_id = snap->next_job_id_copy;
    total_enqueued = snap->total_enqueued_copy;
    total_dispatched = snap->total_dispatched_copy;
    total_yields = snap->total_yields_copy;
    total_sleeps = snap->total_sleeps_copy;
    sched_cpu_count_cached = snap->sched_cpu_count_cached_copy;
    memcpy(sched_cpus, snap->sched_cpus_copy, sizeof(sched_cpus));
    preempt_enabled = snap->preempt_enabled_copy;
    quantum_ticks = snap->quantum_ticks_copy;
    memcpy(sched_runtime, snap->sched_runtime_copy, sizeof(sched_runtime));
    total_timer_ticks = snap->total_timer_ticks_copy;
    user_ticks = snap->user_ticks_copy;
    kernel_ticks = snap->kernel_ticks_copy;
    total_preemptions = snap->total_preemptions_copy;
    last_preempt_ticks = snap->last_preempt_ticks_copy;
    last_preempt_rip = snap->last_preempt_rip_copy;
    initialized = snap->initialized_copy;
    spin_unlock_irqrestore(&scheduler_lock, flags);

    if (queue_to_free) kfree(queue_to_free);
    if (history_to_free) kfree(history_to_free);
    return true;
}

void scheduler_reset_for_test(void) {
    sched_job_t *new_queue = queue ? queue : (sched_job_t *)kmalloc(sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    sched_job_info_t *new_history = history ? history : (sched_job_info_t *)kmalloc(sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    if (!new_queue || !new_history) return;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    queue = new_queue;
    history = new_history;
    memset(queue, 0, sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    memset(history, 0, sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    history_next = 0;
    history_len = 0;
    next_job_id = 1;
    total_enqueued = 0;
    total_dispatched = 0;
    total_yields = 0;
    total_sleeps = 0;
    scheduler_reset_cpu_states_locked();
    preempt_enabled = true;
    quantum_ticks = SCHED_DEFAULT_QUANTUM_TICKS;
    scheduler_reset_runtime_locked();
    total_timer_ticks = 0;
    user_ticks = 0;
    kernel_ticks = 0;
    total_preemptions = 0;
    last_preempt_ticks = 0;
    last_preempt_rip = 0;
    initialized = true;
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

static void inc_u32_sat(u32 *v) {
    if (!v || *v == 0xffffffffu) return;
    ++*v;
}
static void inc_u64_sat(u64 *v) {
    if (!v || *v == 0xffffffffffffffffull) return;
    ++*v;
}

static u32 scheduler_effective_cpu_count(void) {
    u32 n = smp_started_cpu_count();
    if (n == 0 || n > SMP_MAX_CPUS) n = 1;
    return n;
}

static u32 scheduler_current_cpu_index(void) {
#if defined(RABBITBONE_HOST_TEST)
    return 0;
#else
    u32 cpu = smp_current_cpu_id();
    if (cpu >= SMP_MAX_CPUS) return 0;
    return cpu;
#endif
}

static sched_cpu_runtime_t *scheduler_runtime_for_cpu_locked(u32 cpu_id) {
    if (cpu_id >= SMP_MAX_CPUS) cpu_id = 0;
    sched_runtime[cpu_id].cpu_id = cpu_id;
    return &sched_runtime[cpu_id];
}

static sched_cpu_runtime_t *scheduler_runtime_for_current_locked(void) {
    return scheduler_runtime_for_cpu_locked(scheduler_current_cpu_index());
}

static void scheduler_reset_runtime_locked(void) {
    memset(sched_runtime, 0, sizeof(sched_runtime));
    for (u32 i = 0; i < SMP_MAX_CPUS; ++i) sched_runtime[i].cpu_id = i;
}

static void scheduler_reset_cpu_states_locked(void) {
    sched_cpu_count_cached = scheduler_effective_cpu_count();
    memset(sched_cpus, 0, sizeof(sched_cpus));
    for (u32 i = 0; i < sched_cpu_count_cached; ++i) sched_cpus[i].cpu_id = i;
}

static u32 scheduler_queue_depth_for_cpu_locked(u32 cpu_id) {
    if (!queue || cpu_id >= sched_cpu_count_cached) return 0;
    u32 depth = 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_QUEUED && queue[i].target_cpu == cpu_id) ++depth;
    }
    return depth;
}

static u32 scheduler_pick_target_cpu_locked(void) {
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    u32 best = 0;
    u32 best_depth = 0xffffffffu;
    for (u32 i = 0; i < sched_cpu_count_cached; ++i) {
        u32 d = scheduler_queue_depth_for_cpu_locked(i);
        if (d < best_depth) { best = i; best_depth = d; }
    }
    return best;
}

static void scheduler_recount_cpu_states_locked(void) {
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    u32 migrations_in[SMP_MAX_CPUS];
    u32 migrations_out[SMP_MAX_CPUS];
    u64 dispatched[SMP_MAX_CPUS];
    u64 last_dispatch[SMP_MAX_CPUS];
    memset(migrations_in, 0, sizeof(migrations_in));
    memset(migrations_out, 0, sizeof(migrations_out));
    memset(dispatched, 0, sizeof(dispatched));
    memset(last_dispatch, 0, sizeof(last_dispatch));
    for (u32 i = 0; i < sched_cpu_count_cached; ++i) {
        migrations_in[i] = sched_cpus[i].migrations_in;
        migrations_out[i] = sched_cpus[i].migrations_out;
        dispatched[i] = sched_cpus[i].dispatched;
        last_dispatch[i] = sched_cpus[i].last_dispatch_ticks;
        memset(&sched_cpus[i], 0, sizeof(sched_cpus[i]));
        sched_cpus[i].cpu_id = i;
        sched_cpus[i].migrations_in = migrations_in[i];
        sched_cpus[i].migrations_out = migrations_out[i];
        sched_cpus[i].dispatched = dispatched[i];
        sched_cpus[i].last_dispatch_ticks = last_dispatch[i];
    }
    if (!queue) return;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        sched_job_t *j = &queue[i];
        if (j->info.state == SCHED_JOB_EMPTY) continue;
        u32 cpu = j->target_cpu < sched_cpu_count_cached ? j->target_cpu : 0;
        if (j->target_cpu != cpu) j->target_cpu = cpu;
        if (j->info.state == SCHED_JOB_QUEUED) ++sched_cpus[cpu].queued;
        else if (j->info.state == SCHED_JOB_RUNNING) ++sched_cpus[cpu].running;
        else if (j->info.state == SCHED_JOB_DONE) ++sched_cpus[cpu].completed;
        else if (j->info.state == SCHED_JOB_FAILED) ++sched_cpus[cpu].failed;
    }
}

static bool scheduler_migrate_job_locked(sched_job_t *job, u32 dst_cpu) {
    if (!job || job->info.state != SCHED_JOB_QUEUED) return false;
    if (sched_cpu_count_cached == 0) scheduler_reset_cpu_states_locked();
    if (dst_cpu >= sched_cpu_count_cached) return false;
    u32 src = job->target_cpu < sched_cpu_count_cached ? job->target_cpu : 0;
    if (src == dst_cpu) return true;
    job->target_cpu = dst_cpu;
    ++job->migration_count;
    ++sched_cpus[src].migrations_out;
    ++sched_cpus[dst_cpu].migrations_in;
    scheduler_recount_cpu_states_locked();
    return true;
}

static bool scheduler_has_live_jobs_locked(void) {
    if (!queue) return false;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_QUEUED || queue[i].info.state == SCHED_JOB_RUNNING) return true;
    }
    return false;
}

static void ensure_init(void) {
    if (initialized) return;
    scheduler_init();
}

const char *scheduler_job_state_name(u32 state) {
    switch (state) {
        case SCHED_JOB_EMPTY: return "empty";
        case SCHED_JOB_QUEUED: return "queued";
        case SCHED_JOB_RUNNING: return "running";
        case SCHED_JOB_DONE: return "done";
        case SCHED_JOB_FAILED: return "failed";
        default: return "unknown";
    }
}

void scheduler_init(void) {
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (initialized && scheduler_has_live_jobs_locked()) {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return;
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);

    sched_job_t *new_queue = queue ? queue : (sched_job_t *)kmalloc(sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    sched_job_info_t *new_history = history ? history : (sched_job_info_t *)kmalloc(sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    if (!new_queue || !new_history) {
        KLOG(LOG_ERROR, "sched", "process scheduler allocation failed queue=%p history=%p", new_queue, new_history);
        return;
    }

    flags = spin_lock_irqsave(&scheduler_lock);
    queue = new_queue;
    history = new_history;
    memset(queue, 0, sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    memset(history, 0, sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    history_next = 0;
    history_len = 0;
    next_job_id = 1;
    total_enqueued = 0;
    total_dispatched = 0;
    total_yields = 0;
    total_sleeps = 0;
    scheduler_reset_cpu_states_locked();
    preempt_enabled = true;
    quantum_ticks = SCHED_DEFAULT_QUANTUM_TICKS;
    scheduler_reset_runtime_locked();
    total_timer_ticks = 0;
    user_ticks = 0;
    kernel_ticks = 0;
    total_preemptions = 0;
    last_preempt_ticks = 0;
    last_preempt_rip = 0;
    initialized = true;
    spin_unlock_irqrestore(&scheduler_lock, flags);
    KLOG(LOG_INFO, "sched", "process scheduler initialized queue=%u history=%u", SCHED_QUEUE_CAP, SCHED_RESULT_RING);
}

static sched_job_t *alloc_job_slot_locked(void) {
    if (!queue) return 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_EMPTY || queue[i].info.state == SCHED_JOB_DONE || queue[i].info.state == SCHED_JOB_FAILED) return &queue[i];
    }
    return 0;
}

static sched_job_t *find_job_locked(u32 job_id) {
    if (!queue || !job_id) return 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state != SCHED_JOB_EMPTY && queue[i].info.job_id == job_id) return &queue[i];
    }
    return 0;
}

static void push_history_locked(const sched_job_info_t *info) {
    if (!history || !info || info->job_id == 0) return;
    history[history_next] = *info;
    history_next = (history_next + 1u) % SCHED_RESULT_RING;
    if (history_len < SCHED_RESULT_RING) ++history_len;
}

