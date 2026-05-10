#include <aurora/task.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/drivers.h>

typedef struct task {
    task_info_t info;
    task_entry_t entry;
    void *ctx;
} task_t;

static task_t tasks[TASK_MAX];
static task_t *current;
static u32 next_pid;
static u64 total_runs;
static bool initialized;

static u64 now_tick(void) {
#if defined(AURORA_HOST_TEST)
    return total_runs;
#else
    return pit_ticks();
#endif
}

const char *task_state_name(task_state_t state) {
    switch (state) {
        case TASK_UNUSED: return "unused";
        case TASK_READY: return "ready";
        case TASK_RUNNING: return "running";
        case TASK_BLOCKED: return "blocked";
        case TASK_EXITED: return "exited";
        default: return "unknown";
    }
}

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
    current = 0;
    next_pid = 1;
    total_runs = 0;
    initialized = true;
    KLOG(LOG_INFO, "task", "kernel task table initialized slots=%u", TASK_MAX);
}

static task_t *alloc_slot(void) {
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_UNUSED) return &tasks[i];
    }
    return 0;
}

static bool task_pid_in_use(u32 pid) {
    if (pid == 0) return true;
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state != TASK_UNUSED && tasks[i].info.pid == pid) return true;
    }
    return false;
}

static u32 allocate_task_pid(void) {
    for (u32 attempts = 0; attempts < 0xffffffffu; ++attempts) {
        u32 pid = next_pid++;
        if (next_pid == 0) next_pid = 1;
        if (!task_pid_in_use(pid)) return pid;
    }
    return 0;
}

i32 task_spawn_kernel(const char *name, task_entry_t entry, void *ctx) {
    if (!initialized) task_init();
    if (!entry || !name || !*name) return -1;
    task_t *t = alloc_slot();
    if (!t) return -2;
    u32 pid = allocate_task_pid();
    if (!pid) return -3;
    memset(t, 0, sizeof(*t));
    t->info.pid = pid;
    t->info.parent_pid = current ? current->info.pid : 0;
    t->info.state = TASK_READY;
    t->info.created_tick = now_tick();
    strncpy(t->info.name, name, sizeof(t->info.name) - 1u);
    t->entry = entry;
    t->ctx = ctx;
    KLOG(LOG_INFO, "task", "spawn pid=%u name=%s parent=%u", t->info.pid, t->info.name, t->info.parent_pid);
    return (i32)t->info.pid;
}

void task_exit_current(i32 code) {
    if (!current) return;
    current->info.exit_code = code;
    current->info.state = TASK_EXITED;
}

void task_run_ready(u32 max_tasks) {
    if (!initialized) task_init();
    if (max_tasks == 0) max_tasks = TASK_MAX;
    u32 ran = 0;
    for (usize i = 0; i < TASK_MAX && ran < max_tasks; ++i) {
        task_t *t = &tasks[i];
        if (t->info.state != TASK_READY || !t->entry) continue;
        current = t;
        t->info.state = TASK_RUNNING;
        ++t->info.run_count;
        ++total_runs;
        ++ran;
        t->entry(t->ctx);
        if (t->info.state == TASK_RUNNING) t->info.state = TASK_EXITED;
        current = 0;
    }
}

void task_reap_exited(void) {
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_EXITED) memset(&tasks[i], 0, sizeof(tasks[i]));
    }
}

bool task_get_info(u32 pid, task_info_t *out) {
    if (!pid || !out) return false;
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state != TASK_UNUSED && tasks[i].info.pid == pid) {
            *out = tasks[i].info;
            return true;
        }
    }
    return false;
}

void task_get_stats(task_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->next_pid = next_pid;
    out->total_runs = total_runs;
    for (usize i = 0; i < TASK_MAX; ++i) {
        switch (tasks[i].info.state) {
            case TASK_UNUSED: break;
            case TASK_READY: ++out->used_slots; ++out->ready; break;
            case TASK_RUNNING: ++out->used_slots; ++out->running; break;
            case TASK_BLOCKED: ++out->used_slots; ++out->blocked; break;
            case TASK_EXITED: ++out->used_slots; ++out->exited; break;
        }
    }
}

void task_dump(void) {
    task_stats_t st;
    task_get_stats(&st);
    kprintf("tasks: used=%u ready=%u running=%u blocked=%u exited=%u total_runs=%llu next_pid=%u\n",
            st.used_slots, st.ready, st.running, st.blocked, st.exited,
            (unsigned long long)st.total_runs, st.next_pid);
    kprintf("  pid   ppid  state    runs  name\n");
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_UNUSED) continue;
        kprintf("  %-5u %-5u %-8s %-5llu %s\n",
                tasks[i].info.pid,
                tasks[i].info.parent_pid,
                task_state_name(tasks[i].info.state),
                (unsigned long long)tasks[i].info.run_count,
                tasks[i].info.name);
    }
}

typedef struct task_test_ctx {
    u32 *counter;
    u32 value;
} task_test_ctx_t;

static void task_test_entry(void *ctx) {
    task_test_ctx_t *c = (task_test_ctx_t *)ctx;
    *c->counter += c->value;
    task_exit_current((i32)c->value);
}

bool task_selftest(void) {
    task_init();
    u32 counter = 0;
    task_test_ctx_t a = { &counter, 3 };
    task_test_ctx_t b = { &counter, 7 };
    i32 pa = task_spawn_kernel("ktest-a", task_test_entry, &a);
    i32 pb = task_spawn_kernel("ktest-b", task_test_entry, &b);
    if (pa <= 0 || pb <= 0 || pa == pb) return false;
    task_run_ready(16);
    task_info_t ia;
    task_info_t ib;
    if (!task_get_info((u32)pa, &ia) || !task_get_info((u32)pb, &ib)) return false;
    if (counter != 10u) return false;
    if (ia.state != TASK_EXITED || ib.state != TASK_EXITED) return false;
    if (ia.exit_code != 3 || ib.exit_code != 7) return false;
    task_reap_exited();
    task_stats_t st;
    task_get_stats(&st);
    return st.used_slots == 0;
}
