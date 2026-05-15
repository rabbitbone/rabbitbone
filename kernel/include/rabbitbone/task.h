#ifndef RABBITBONE_TASK_H
#define RABBITBONE_TASK_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define TASK_MAX 64u
#define TASK_NAME_MAX 32u

typedef enum task_state {
    TASK_UNUSED = 0,
    TASK_READY = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_EXITED = 4,
} task_state_t;

typedef void (*task_entry_t)(void *ctx);

typedef struct task_info {
    u32 pid;
    u32 parent_pid;
    task_state_t state;
    char name[TASK_NAME_MAX];
    u64 created_tick;
    u64 run_count;
    i32 exit_code;
} task_info_t;

typedef struct task_stats {
    u32 used_slots;
    u32 ready;
    u32 running;
    u32 blocked;
    u32 exited;
    u32 next_pid;
    u64 total_runs;
} task_stats_t;

void task_init(void);
i32 task_spawn_kernel(const char *name, task_entry_t entry, void *ctx);
void task_run_ready(u32 max_tasks);
void task_exit_current(i32 code);
void task_reap_exited(void);
bool task_get_info(u32 pid, task_info_t *out);
void task_get_stats(task_stats_t *out);
void task_dump(void);
const char *task_state_name(task_state_t state);
bool task_selftest(void);
usize task_snapshot_size(void);
bool task_snapshot_save(void *buffer, usize size);
bool task_snapshot_restore(const void *buffer, usize size);
void task_reset_for_test(void);

#if defined(__cplusplus)
}
#endif
#endif
