#pragma once

#include <rabbitbone/scheduler.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/console.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/spinlock.h>

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
static spinlock_t scheduler_lock;

