#ifndef AURORA_SCHEDULER_H
#define AURORA_SCHEDULER_H
#include <aurora/types.h>
#include <aurora/process.h>
#include <aurora/abi.h>
#include <aurora/arch/cpu.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define SCHED_QUEUE_CAP 32u
#define SCHED_PATH_MAX PROCESS_NAME_MAX
#define SCHED_ARG_MAX PROCESS_ARG_MAX
#define SCHED_RESULT_RING 32u
#define SCHED_DEFAULT_QUANTUM_TICKS 4u
#define SCHED_MAX_QUANTUM_TICKS 1000u

typedef enum sched_job_state {
    SCHED_JOB_EMPTY = 0,
    SCHED_JOB_QUEUED = 1,
    SCHED_JOB_RUNNING = 2,
    SCHED_JOB_DONE = 3,
    SCHED_JOB_FAILED = 4,
} sched_job_state_t;

typedef aurora_schedinfo_t sched_stats_t;

typedef struct sched_job_info {
    u32 job_id;
    u32 pid;
    u32 state;
    i32 exit_code;
    i32 proc_status;
    u64 enqueued_ticks;
    u64 started_ticks;
    u64 finished_ticks;
    char path[SCHED_PATH_MAX];
} sched_job_info_t;

void scheduler_init(void);
const char *scheduler_job_state_name(u32 state);
bool scheduler_enqueue(const char *path, int argc, const char *const *argv, u32 *job_id_out);
u32 scheduler_run_ready(u32 max_jobs);
bool scheduler_wait_job(u32 job_id, sched_job_info_t *out);
bool scheduler_get_job(u32 job_id, sched_job_info_t *out);
void scheduler_stats(sched_stats_t *out);
void scheduler_note_yield(void);
void scheduler_note_sleep(u64 ticks);
bool scheduler_tick(const cpu_regs_t *regs);
bool scheduler_preempt_info(aurora_preemptinfo_t *out);
bool scheduler_set_quantum(u32 quantum_ticks);
void scheduler_dump(void);
bool scheduler_selftest(void);
usize scheduler_snapshot_size(void);
bool scheduler_snapshot_save(void *buffer, usize size);
bool scheduler_snapshot_restore(const void *buffer, usize size);
void scheduler_reset_for_test(void);

#if defined(__cplusplus)
}
#endif
#endif
