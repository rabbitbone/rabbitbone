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
    bool preempt_enabled_copy;
    u32 quantum_ticks_copy;
    u32 current_slice_ticks_copy;
    u32 current_slice_pid_copy;
    u64 total_timer_ticks_copy;
    u64 user_ticks_copy;
    u64 kernel_ticks_copy;
    u64 total_preemptions_copy;
    u64 last_preempt_ticks_copy;
    u64 last_preempt_rip_copy;
    bool initialized_copy;
} scheduler_snapshot_t;

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
    snap->preempt_enabled_copy = preempt_enabled;
    snap->quantum_ticks_copy = quantum_ticks;
    snap->current_slice_ticks_copy = current_slice_ticks;
    snap->current_slice_pid_copy = current_slice_pid;
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
    preempt_enabled = snap->preempt_enabled_copy;
    quantum_ticks = snap->quantum_ticks_copy;
    current_slice_ticks = snap->current_slice_ticks_copy;
    current_slice_pid = snap->current_slice_pid_copy;
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
    preempt_enabled = true;
    quantum_ticks = SCHED_DEFAULT_QUANTUM_TICKS;
    current_slice_ticks = 0;
    current_slice_pid = 0;
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
    preempt_enabled = true;
    quantum_ticks = SCHED_DEFAULT_QUANTUM_TICKS;
    current_slice_ticks = 0;
    current_slice_pid = 0;
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

