static bool copy_args(sched_job_t *job, const char *path, int argc, const char *const *argv) {
    if (!job || !path || !*path || argc <= 0 || argc > (int)SCHED_ARG_MAX) return false;
    usize path_len = strnlen(path, SCHED_PATH_MAX);
    if (path_len == 0 || path_len >= SCHED_PATH_MAX) return false;
    strncpy(job->info.path, path, sizeof(job->info.path) - 1u);
    job->argc = argc;
    for (int i = 0; i < argc; ++i) {
        const char *src = argv && argv[i] ? argv[i] : (i == 0 ? path : "");
        usize len = strnlen(src, SCHED_PATH_MAX);
        if (len >= SCHED_PATH_MAX) return false;
        strncpy(job->argv[i], src, SCHED_PATH_MAX - 1u);
    }
    return true;
}

bool scheduler_enqueue(const char *path, int argc, const char *const *argv, u32 *job_id_out) {
    ensure_init();
    if (!initialized || process_user_active()) return false;
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (!queue) { spin_unlock_irqrestore(&scheduler_lock, flags); return false; }
    sched_job_t *job = alloc_job_slot_locked();
    if (!job) { spin_unlock_irqrestore(&scheduler_lock, flags); return false; }
    memset(job, 0, sizeof(*job));
    if (!copy_args(job, path, argc, argv)) {
        memset(job, 0, sizeof(*job));
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    job->info.job_id = next_job_id++;
    if (next_job_id == 0) next_job_id = 1;
    job->info.state = SCHED_JOB_QUEUED;
    job->info.enqueued_ticks = sched_now();
    inc_u32_sat(&total_enqueued);
    u32 jid = job->info.job_id;
    char log_path[SCHED_PATH_MAX];
    strncpy(log_path, job->info.path, sizeof(log_path) - 1u);
    spin_unlock_irqrestore(&scheduler_lock, flags);
    if (job_id_out) *job_id_out = jid;
    KLOG(LOG_INFO, "sched", "enqueue job=%u path=%s", jid, log_path);
    return true;
}

static const char *const *argv_view_local(char argv_storage[SCHED_ARG_MAX][SCHED_PATH_MAX], int argc, const char **tmp) {
    for (int i = 0; i < argc; ++i) tmp[i] = argv_storage[i];
    return tmp;
}

static bool run_one(sched_job_t *job) {
    if (!job) return false;
    char path[SCHED_PATH_MAX];
    char argv_storage[SCHED_ARG_MAX][SCHED_PATH_MAX];
    int argc = 0;
    u32 job_id = 0;

    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (job->info.state != SCHED_JOB_QUEUED) {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    job->info.state = SCHED_JOB_RUNNING;
    job->info.started_ticks = sched_now();
    job_id = job->info.job_id;
    argc = job->argc;
    strncpy(path, job->info.path, sizeof(path) - 1u);
    for (int i = 0; i < argc; ++i) strncpy(argv_storage[i], job->argv[i], SCHED_PATH_MAX - 1u);
    spin_unlock_irqrestore(&scheduler_lock, flags);

    const char *argv[SCHED_ARG_MAX];
    process_result_t result;
    memset(&result, 0, sizeof(result));
    process_status_t st = process_exec(path, argc, argv_view_local(argv_storage, argc, argv), &result);

    flags = spin_lock_irqsave(&scheduler_lock);
    sched_job_t *finished_job = find_job_locked(job_id);
    if (!finished_job || finished_job->info.state != SCHED_JOB_RUNNING) {
        spin_unlock_irqrestore(&scheduler_lock, flags);
        return false;
    }
    finished_job->info.finished_ticks = sched_now();
    finished_job->info.pid = result.pid;
    finished_job->info.exit_code = result.exit_code;
    finished_job->info.proc_status = (i32)st;
    finished_job->info.state = (st == PROC_OK && !result.faulted) ? SCHED_JOB_DONE : SCHED_JOB_FAILED;
    inc_u32_sat(&total_dispatched);
    sched_job_info_t finished = finished_job->info;
    push_history_locked(&finished);
    memset(finished_job, 0, sizeof(*finished_job));
    spin_unlock_irqrestore(&scheduler_lock, flags);

    KLOG(LOG_INFO, "sched", "finish job=%u pid=%u state=%s exit=%d", finished.job_id, finished.pid,
         scheduler_job_state_name(finished.state), finished.exit_code);
    return true;
}

u32 scheduler_run_ready(u32 max_jobs) {
    ensure_init();
    if (!initialized || !queue || process_user_active()) return 0;
    if (max_jobs == 0) max_jobs = SCHED_QUEUE_CAP;
    u32 ran = 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP && ran < max_jobs; ++i) {
        bool should_run = false;
        u64 flags = spin_lock_irqsave(&scheduler_lock);
        should_run = queue && queue[i].info.state == SCHED_JOB_QUEUED;
        spin_unlock_irqrestore(&scheduler_lock, flags);
        if (should_run && run_one(&queue[i])) ++ran;
    }
    return ran;
}

