#pragma once

#include <rabbitbone/scheduler.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/console.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/smp.h>

typedef struct sched_job {
    sched_job_info_t info;
    int argc;
    u32 target_cpu;
    u32 last_cpu;
    u32 migration_count;
    char argv[SCHED_ARG_MAX][SCHED_PATH_MAX];
} sched_job_t;

typedef struct sched_cpu_queue_state {
    u32 cpu_id;
    u32 queued;
    u32 running;
    u32 completed;
    u32 failed;
    u32 migrations_in;
    u32 migrations_out;
    u64 dispatched;
    u64 last_dispatch_ticks;
} sched_cpu_queue_state_t;

static sched_job_t *queue;
static sched_job_info_t *history;
static usize history_next;
static usize history_len;
static u32 next_job_id;
static u32 total_enqueued;
static u32 total_dispatched;
static u32 total_yields;
static u32 total_sleeps;
static u32 sched_cpu_count_cached;
static sched_cpu_queue_state_t sched_cpus[SMP_MAX_CPUS];
static bool preempt_enabled;
static u32 quantum_ticks;
typedef struct sched_cpu_runtime {
    u32 cpu_id;
    u32 current_slice_ticks;
    u32 current_slice_pid;
    volatile u32 need_resched;
    volatile u32 idle;
    u32 reserved;
    u64 idle_entries;
    u64 idle_ticks;
    u64 kernel_task_dispatches;
    u64 process_dispatches;
    u64 reschedule_ipis;
    u64 local_reschedules;
} sched_cpu_runtime_t;

static sched_cpu_runtime_t sched_runtime[SMP_MAX_CPUS];
static u64 total_timer_ticks;
static u64 user_ticks;
static u64 kernel_ticks;
static u64 total_preemptions;
static u64 last_preempt_ticks;
static u64 last_preempt_rip;
static bool initialized;
static spinlock_t scheduler_lock;

static u32 scheduler_effective_cpu_count(void);
static void scheduler_reset_cpu_states_locked(void);
static u32 scheduler_queue_depth_for_cpu_locked(u32 cpu_id);
static u32 scheduler_pick_target_cpu_locked(void);
static void scheduler_recount_cpu_states_locked(void);
static bool scheduler_migrate_job_locked(sched_job_t *job, u32 dst_cpu);
static u32 scheduler_current_cpu_index(void);
static sched_cpu_runtime_t *scheduler_runtime_for_cpu_locked(u32 cpu_id);
static sched_cpu_runtime_t *scheduler_runtime_for_current_locked(void);
static void scheduler_reset_runtime_locked(void);

