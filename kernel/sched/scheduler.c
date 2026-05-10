#include <aurora/scheduler.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/console.h>
#include <aurora/timer.h>
#include <aurora/drivers.h>

typedef struct sched_job {
    sched_job_info_t info;
    int argc;
    char argv[SCHED_ARG_MAX][SCHED_PATH_MAX];
} sched_job_t;

static sched_job_t *queue;
static sched_job_info_t *history;
static usize history_next;
static usize history_len;
static u32 next_job_id;
static u32 total_enqueued;
static u32 total_dispatched;
static u32 total_yields;
static u32 total_sleeps;
static bool preempt_enabled;
static u32 quantum_ticks;
static u32 current_slice_ticks;
static u32 current_slice_pid;
static u64 total_timer_ticks;
static u64 user_ticks;
static u64 kernel_ticks;
static u64 total_preemptions;
static u64 last_preempt_ticks;
static u64 last_preempt_rip;
static bool initialized;

static u64 sched_now(void) { return pit_ticks(); }

static void inc_u32_sat(u32 *v) {
    if (!v) return;
    u32 old = __atomic_load_n(v, __ATOMIC_RELAXED);
    while (old != 0xffffffffu) {
        u32 next = old + 1u;
        if (__atomic_compare_exchange_n(v, &old, next, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
    }
}
static void inc_u64_sat(u64 *v) {
    if (!v) return;
    u64 old = __atomic_load_n(v, __ATOMIC_RELAXED);
    while (old != 0xffffffffffffffffull) {
        u64 next = old + 1u;
        if (__atomic_compare_exchange_n(v, &old, next, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return;
    }
}

static bool scheduler_has_live_jobs(void) {
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
    if (initialized && scheduler_has_live_jobs()) return;
    if (!queue) queue = (sched_job_t *)kmalloc(sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    if (!history) history = (sched_job_info_t *)kmalloc(sizeof(sched_job_info_t) * SCHED_RESULT_RING);
    if (queue) memset(queue, 0, sizeof(sched_job_t) * SCHED_QUEUE_CAP);
    if (history) memset(history, 0, sizeof(sched_job_info_t) * SCHED_RESULT_RING);
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
    KLOG(LOG_INFO, "sched", "process scheduler initialized queue=%u history=%u", SCHED_QUEUE_CAP, SCHED_RESULT_RING);
}

static sched_job_t *alloc_job_slot(void) {
    if (!queue) return 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_EMPTY || queue[i].info.state == SCHED_JOB_DONE || queue[i].info.state == SCHED_JOB_FAILED) return &queue[i];
    }
    return 0;
}

static sched_job_t *find_job(u32 job_id) {
    if (!queue || !job_id) return 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state != SCHED_JOB_EMPTY && queue[i].info.job_id == job_id) return &queue[i];
    }
    return 0;
}

static void push_history(const sched_job_info_t *info) {
    if (!history || !info || info->job_id == 0) return;
    history[history_next] = *info;
    history_next = (history_next + 1u) % SCHED_RESULT_RING;
    if (history_len < SCHED_RESULT_RING) ++history_len;
}

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
    if (!queue || process_user_active()) return false;
    sched_job_t *job = alloc_job_slot();
    if (!job) return false;
    memset(job, 0, sizeof(*job));
    if (!copy_args(job, path, argc, argv)) {
        memset(job, 0, sizeof(*job));
        return false;
    }
    job->info.job_id = next_job_id++;
    if (next_job_id == 0) next_job_id = 1;
    job->info.state = SCHED_JOB_QUEUED;
    job->info.enqueued_ticks = sched_now();
    inc_u32_sat(&total_enqueued);
    if (job_id_out) *job_id_out = job->info.job_id;
    KLOG(LOG_INFO, "sched", "enqueue job=%u path=%s", job->info.job_id, job->info.path);
    return true;
}

static const char *const *argv_view(sched_job_t *job, const char **tmp) {
    for (int i = 0; i < job->argc; ++i) tmp[i] = job->argv[i];
    return tmp;
}

static void run_one(sched_job_t *job) {
    if (!job || job->info.state != SCHED_JOB_QUEUED) return;
    job->info.state = SCHED_JOB_RUNNING;
    job->info.started_ticks = sched_now();
    const char *argv[SCHED_ARG_MAX];
    process_result_t result;
    memset(&result, 0, sizeof(result));
    process_status_t st = process_exec(job->info.path, job->argc, argv_view(job, argv), &result);
    job->info.finished_ticks = sched_now();
    job->info.pid = result.pid;
    job->info.exit_code = result.exit_code;
    job->info.proc_status = (i32)st;
    job->info.state = (st == PROC_OK && !result.faulted) ? SCHED_JOB_DONE : SCHED_JOB_FAILED;
    inc_u32_sat(&total_dispatched);
    sched_job_info_t finished = job->info;
    push_history(&finished);
    KLOG(LOG_INFO, "sched", "finish job=%u pid=%u state=%s exit=%d", finished.job_id, finished.pid,
         scheduler_job_state_name(finished.state), finished.exit_code);
    memset(job, 0, sizeof(*job));
}

u32 scheduler_run_ready(u32 max_jobs) {
    ensure_init();
    if (!queue || process_user_active()) return 0;
    if (max_jobs == 0) max_jobs = SCHED_QUEUE_CAP;
    u32 ran = 0;
    for (usize i = 0; i < SCHED_QUEUE_CAP && ran < max_jobs; ++i) {
        if (queue[i].info.state == SCHED_JOB_QUEUED) {
            run_one(&queue[i]);
            ++ran;
        }
    }
    return ran;
}

bool scheduler_get_job(u32 job_id, sched_job_info_t *out) {
    ensure_init();
    if (!job_id || !out) return false;
    sched_job_t *job = find_job(job_id);
    if (job) {
        *out = job->info;
        return true;
    }
    if (!history) return false;
    for (usize i = 0; i < history_len; ++i) {
        usize idx = (history_next + SCHED_RESULT_RING - 1u - i) % SCHED_RESULT_RING;
        if (history[idx].job_id == job_id) {
            *out = history[idx];
            return true;
        }
    }
    return false;
}

bool scheduler_wait_job(u32 job_id, sched_job_info_t *out) {
    ensure_init();
    sched_job_info_t info;
    if (!scheduler_get_job(job_id, &info)) return false;
    if (info.state == SCHED_JOB_QUEUED) {
        sched_job_t *job = find_job(job_id);
        if (!job || job->info.state != SCHED_JOB_QUEUED) return false;
        run_one(job);
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
    if (!queue) return;
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

void scheduler_note_yield(void) {
    ensure_init();
    inc_u32_sat(&total_yields);
}

void scheduler_note_sleep(u64 ticks) {
    ensure_init();
    (void)ticks;
    inc_u32_sat(&total_sleeps);
}

bool scheduler_set_quantum(u32 q) {
    ensure_init();
    if (q == 0 || q > SCHED_MAX_QUANTUM_TICKS) return false;
    quantum_ticks = q;
    current_slice_ticks = 0;
    return true;
}

bool scheduler_tick(const cpu_regs_t *regs) {
    if (!initialized || !regs) return false;
    inc_u64_sat(&total_timer_ticks);
    bool from_user = (regs->cs & 3u) == 3u;
    if (!from_user || !process_user_active()) {
        inc_u64_sat(&kernel_ticks);
        current_slice_pid = 0;
        current_slice_ticks = 0;
        return false;
    }
    inc_u64_sat(&user_ticks);
    u32 pid = process_current_pid();
    if (pid == 0 || pid != current_slice_pid) {
        current_slice_pid = pid;
        current_slice_ticks = 0;
    }
    ++current_slice_ticks;
    if (preempt_enabled && quantum_ticks && current_slice_ticks >= quantum_ticks) {
        inc_u64_sat(&total_preemptions);
        last_preempt_ticks = sched_now();
        last_preempt_rip = regs->rip;
        current_slice_ticks = 0;
        return true;
    }
    return false;
}

bool scheduler_preempt_info(aurora_preemptinfo_t *out) {
    ensure_init();
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->enabled = preempt_enabled ? 1u : 0u;
    out->quantum_ticks = quantum_ticks;
    out->current_pid = current_slice_pid;
    out->current_slice_ticks = current_slice_ticks;
    out->total_timer_ticks = total_timer_ticks;
    out->user_ticks = user_ticks;
    out->kernel_ticks = kernel_ticks;
    out->total_preemptions = total_preemptions;
    out->last_preempt_ticks = last_preempt_ticks;
    out->last_preempt_rip = last_preempt_rip;
    return true;
}

void scheduler_dump(void) {
    ensure_init();
    sched_stats_t st;
    scheduler_stats(&st);
    kprintf("scheduler: queued=%u running=%u done=%u failed=%u enq=%u dispatch=%u yields=%u sleeps=%u next_job=%u\n",
            st.queued, st.running, st.completed, st.failed, st.total_enqueued, st.total_dispatched,
            st.total_yields, st.total_sleeps, st.next_job_id);
    kprintf("preempt: enabled=%u quantum=%u slice=%u timer=%llu user=%llu kernel=%llu preemptions=%llu last_tick=%llu rip=%p\n",
            st.preempt_enabled, st.quantum_ticks, st.current_slice_ticks,
            (unsigned long long)st.total_timer_ticks, (unsigned long long)st.user_ticks,
            (unsigned long long)st.kernel_ticks, (unsigned long long)st.total_preemptions,
            (unsigned long long)last_preempt_ticks, (void *)(uptr)last_preempt_rip);
    if (!queue) return;
    kprintf("  job   pid   state    exit  path\n");
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_EMPTY) continue;
        kprintf("  %-5u %-5u %-8s %-5d %s\n", queue[i].info.job_id, queue[i].info.pid,
                scheduler_job_state_name(queue[i].info.state), queue[i].info.exit_code, queue[i].info.path);
    }
}

bool scheduler_selftest(void) {
    scheduler_init();
    sched_stats_t stats;
    scheduler_stats(&stats);
    if (stats.queued || stats.completed || stats.total_enqueued) return false;
    const char *hello_argv[] = { "/bin/hello" };
    const char *iso_argv[] = { "/bin/isolate" };
    u32 j1 = 0;
    u32 j2 = 0;
    if (!scheduler_enqueue("/bin/hello", 1, hello_argv, &j1) || !scheduler_enqueue("/bin/isolate", 1, iso_argv, &j2)) return false;
    if (!j1 || !j2 || j1 == j2) return false;
    scheduler_stats(&stats);
    if (stats.queued != 2 || stats.total_enqueued != 2) return false;
    if (scheduler_run_ready(1) != 1) return false;
    sched_job_info_t info1;
    sched_job_info_t info2;
    if (!scheduler_get_job(j1, &info1) || info1.state != SCHED_JOB_DONE || info1.exit_code != 7 || info1.pid == 0) return false;
    if (!scheduler_get_job(j2, &info2) || info2.state != SCHED_JOB_QUEUED) return false;
    if (!scheduler_wait_job(j2, &info2) || info2.state != SCHED_JOB_DONE || info2.exit_code != 41 || info2.pid == 0) return false;
    scheduler_note_yield();
    scheduler_note_sleep(1);
    cpu_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x08;
    regs.rip = 0xffffffff80001000ull;
    scheduler_tick(&regs);
    aurora_preemptinfo_t pi;
    if (!scheduler_preempt_info(&pi) || pi.quantum_ticks != quantum_ticks || pi.total_timer_ticks == 0) return false;
    scheduler_stats(&stats);
    if (stats.total_dispatched != 2 || stats.total_yields == 0 || stats.total_sleeps == 0 || stats.total_timer_ticks == 0) return false;
    if (scheduler_wait_job(0xffffffffu, &info2)) return false;
    bool reuse_ok = true;
    u32 last_job = 0;
    for (u32 i = 0; i < SCHED_QUEUE_CAP + 1u; ++i) {
        u32 jid = 0;
        if (!scheduler_enqueue("/bin/hello", 1, hello_argv, &jid) || scheduler_run_ready(1) != 1) { reuse_ok = false; break; }
        last_job = jid;
    }
    if (!reuse_ok || !scheduler_get_job(last_job, &info2) || info2.state != SCHED_JOB_DONE) return false;
    return true;
}
