#include <rabbitbone/task.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/spinlock.h>

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
static spinlock_t task_lock;

static u64 now_tick(void) {
#if defined(RABBITBONE_HOST_TEST)
    return total_runs;
#else
    return pit_ticks();
#endif
}

#define TASK_SNAPSHOT_MAGIC 0x52424f4e52415453ull

typedef struct task_snapshot {
    u64 magic;
    task_t tasks_copy[TASK_MAX];
    i32 current_index;
    u32 next_pid_copy;
    u64 total_runs_copy;
    bool initialized_copy;
} task_snapshot_t;

usize task_snapshot_size(void) { return sizeof(task_snapshot_t); }

bool task_snapshot_save(void *buffer, usize size) {
    if (!buffer || size < sizeof(task_snapshot_t)) return false;
    task_snapshot_t *snap = (task_snapshot_t *)buffer;
    memset(snap, 0, sizeof(*snap));
    u64 flags = spin_lock_irqsave(&task_lock);
    snap->magic = TASK_SNAPSHOT_MAGIC;
    memcpy(snap->tasks_copy, tasks, sizeof(tasks));
    snap->current_index = -1;
    if (current) {
        for (usize i = 0; i < TASK_MAX; ++i) {
            if (&tasks[i] == current) { snap->current_index = (i32)i; break; }
        }
    }
    snap->next_pid_copy = next_pid;
    snap->total_runs_copy = total_runs;
    snap->initialized_copy = initialized;
    spin_unlock_irqrestore(&task_lock, flags);
    return true;
}

bool task_snapshot_restore(const void *buffer, usize size) {
    if (!buffer || size < sizeof(task_snapshot_t)) return false;
    const task_snapshot_t *snap = (const task_snapshot_t *)buffer;
    if (snap->magic != TASK_SNAPSHOT_MAGIC) return false;
    u64 flags = spin_lock_irqsave(&task_lock);
    memcpy(tasks, snap->tasks_copy, sizeof(tasks));
    current = (snap->current_index >= 0 && (usize)snap->current_index < TASK_MAX) ? &tasks[snap->current_index] : 0;
    next_pid = snap->next_pid_copy;
    total_runs = snap->total_runs_copy;
    initialized = snap->initialized_copy;
    spin_unlock_irqrestore(&task_lock, flags);
    return true;
}

void task_reset_for_test(void) {
    u64 flags = spin_lock_irqsave(&task_lock);
    memset(tasks, 0, sizeof(tasks));
    current = 0;
    next_pid = 1;
    total_runs = 0;
    initialized = true;
    spin_unlock_irqrestore(&task_lock, flags);
}

static bool task_table_has_live_locked(void) {
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_READY || tasks[i].info.state == TASK_RUNNING || tasks[i].info.state == TASK_BLOCKED) return true;
    }
    return false;
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
    u64 flags = spin_lock_irqsave(&task_lock);
    if (initialized && task_table_has_live_locked()) {
        spin_unlock_irqrestore(&task_lock, flags);
        return;
    }
    memset(tasks, 0, sizeof(tasks));
    current = 0;
    next_pid = 1;
    total_runs = 0;
    initialized = true;
    spin_unlock_irqrestore(&task_lock, flags);
    KLOG(LOG_INFO, "task", "kernel task table initialized slots=%u", TASK_MAX);
}

static task_t *alloc_slot_locked(void) {
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_UNUSED) return &tasks[i];
    }
    return 0;
}

static bool task_pid_in_use_locked(u32 pid) {
    if (pid == 0) return true;
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state != TASK_UNUSED && tasks[i].info.pid == pid) return true;
    }
    return false;
}

static u32 allocate_task_pid_locked(void) {
    for (u32 attempts = 0; attempts < 0xffffffffu; ++attempts) {
        u32 pid = next_pid++;
        if (next_pid == 0) next_pid = 1;
        if (!task_pid_in_use_locked(pid)) return pid;
    }
    return 0;
}

i32 task_spawn_kernel(const char *name, task_entry_t entry, void *ctx) {
    if (!initialized) task_init();
    if (!entry || !name || !*name) return -1;
    u64 flags = spin_lock_irqsave(&task_lock);
    task_t *t = alloc_slot_locked();
    if (!t) { spin_unlock_irqrestore(&task_lock, flags); return -2; }
    u32 pid = allocate_task_pid_locked();
    if (!pid) { spin_unlock_irqrestore(&task_lock, flags); return -3; }
    memset(t, 0, sizeof(*t));
    t->info.pid = pid;
    t->info.parent_pid = current ? current->info.pid : 0;
    t->info.state = TASK_READY;
    t->info.created_tick = now_tick();
    strncpy(t->info.name, name, sizeof(t->info.name) - 1u);
    t->entry = entry;
    t->ctx = ctx;
    task_info_t info = t->info;
    spin_unlock_irqrestore(&task_lock, flags);
    KLOG(LOG_INFO, "task", "spawn pid=%u name=%s parent=%u", info.pid, info.name, info.parent_pid);
    return (i32)info.pid;
}

void task_exit_current(i32 code) {
    u64 flags = spin_lock_irqsave(&task_lock);
    if (current) {
        current->info.exit_code = code;
        current->info.state = TASK_EXITED;
    }
    spin_unlock_irqrestore(&task_lock, flags);
}

void task_run_ready(u32 max_tasks) {
    if (!initialized) task_init();
    if (max_tasks == 0) max_tasks = TASK_MAX;
    u32 ran = 0;
    for (usize i = 0; i < TASK_MAX && ran < max_tasks; ++i) {
        task_entry_t entry = 0;
        void *ctx = 0;
        task_t *t = 0;
        u64 flags = spin_lock_irqsave(&task_lock);
        if (tasks[i].info.state == TASK_READY && tasks[i].entry) {
            t = &tasks[i];
            current = t;
            t->info.state = TASK_RUNNING;
            ++t->info.run_count;
            ++total_runs;
            ++ran;
            entry = t->entry;
            ctx = t->ctx;
        }
        spin_unlock_irqrestore(&task_lock, flags);
        if (!entry) continue;
        entry(ctx);
        flags = spin_lock_irqsave(&task_lock);
        if (t && current == t) {
            if (t->info.state == TASK_RUNNING) t->info.state = TASK_EXITED;
            current = 0;
        }
        spin_unlock_irqrestore(&task_lock, flags);
    }
}

void task_reap_exited(void) {
    u64 flags = spin_lock_irqsave(&task_lock);
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_EXITED) memset(&tasks[i], 0, sizeof(tasks[i]));
    }
    spin_unlock_irqrestore(&task_lock, flags);
}

bool task_get_info(u32 pid, task_info_t *out) {
    if (!pid || !out) return false;
    u64 flags = spin_lock_irqsave(&task_lock);
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state != TASK_UNUSED && tasks[i].info.pid == pid) {
            *out = tasks[i].info;
            spin_unlock_irqrestore(&task_lock, flags);
            return true;
        }
    }
    spin_unlock_irqrestore(&task_lock, flags);
    return false;
}

void task_get_stats(task_stats_t *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    u64 flags = spin_lock_irqsave(&task_lock);
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
    spin_unlock_irqrestore(&task_lock, flags);
}

void task_dump(void) {
    task_stats_t st;
    task_get_stats(&st);
    kprintf("tasks: used=%u ready=%u running=%u blocked=%u exited=%u total_runs=%llu next_pid=%u\n",
            st.used_slots, st.ready, st.running, st.blocked, st.exited,
            (unsigned long long)st.total_runs, st.next_pid);
    kprintf("  pid   ppid  state    runs  name\n");
    u64 flags = spin_lock_irqsave(&task_lock);
    for (usize i = 0; i < TASK_MAX; ++i) {
        if (tasks[i].info.state == TASK_UNUSED) continue;
        kprintf("  %-5u %-5u %-8s %-5llu %s\n",
                tasks[i].info.pid,
                tasks[i].info.parent_pid,
                task_state_name(tasks[i].info.state),
                (unsigned long long)tasks[i].info.run_count,
                tasks[i].info.name);
    }
    spin_unlock_irqrestore(&task_lock, flags);
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
