static void test_scheduler_contracts(void) {
    suite_begin("scheduler/runqueue/contracts");
    scheduler_init();
    sched_stats_t stats;
    scheduler_stats(&stats);
    check(stats.queue_capacity == SCHED_QUEUE_CAP && stats.queued == 0, "scheduler initializes empty process run queue");
    check(stats.preempt_enabled == 1u && stats.quantum_ticks == SCHED_DEFAULT_QUANTUM_TICKS, "scheduler initializes preemption quantum contract");
    const char *hello_argv[] = { "/bin/hello" };
    const char *iso_argv[] = { "/bin/isolate" };
    u32 j1 = 0;
    u32 j2 = 0;
    check(scheduler_enqueue("/bin/hello", 1, hello_argv, &j1), "scheduler enqueues first user job");
    check(scheduler_enqueue("/bin/isolate", 1, iso_argv, &j2) && j2 != j1, "scheduler enqueues second user job with distinct id");
    scheduler_stats(&stats);
    check(stats.queued == 2 && stats.total_enqueued == 2, "scheduler stats track queued jobs");
    check(scheduler_run_ready(1) == 1, "scheduler dispatches one queued process");
    sched_job_info_t info;
    check(scheduler_get_job(j1, &info) && info.state == SCHED_JOB_DONE && info.pid != 0 && info.exit_code == 7, "scheduler records completed first job");
    check(scheduler_get_job(j2, &info) && info.state == SCHED_JOB_QUEUED, "scheduler leaves later job queued after bounded dispatch");
    check(scheduler_wait_job(j2, &info) && info.state == SCHED_JOB_DONE && info.exit_code == 41, "scheduler wait drains requested queued job");
    u32 before_yields = stats.total_yields;
    scheduler_note_yield();
    scheduler_note_sleep(0);
    cpu_regs_t tick_regs;
    memset(&tick_regs, 0, sizeof(tick_regs));
    tick_regs.cs = 0x08;
    tick_regs.rip = 0xffffffff80002000ull;
    scheduler_tick(&tick_regs);
    aurora_preemptinfo_t pi;
    check(scheduler_preempt_info(&pi) && pi.enabled == 1u && pi.quantum_ticks == SCHED_DEFAULT_QUANTUM_TICKS, "scheduler exposes preemption telemetry");
    scheduler_stats(&stats);
    check(stats.total_yields >= before_yields + 1 && stats.total_sleeps >= 1 && stats.total_timer_ticks >= 1, "scheduler accounting tracks yield sleep and timer tick calls");
    check(!scheduler_wait_job(0xffffffffu, &info), "scheduler wait rejects unknown job id");
    bool reuse_ok = true;
    u32 last_job = 0;
    for (u32 i = 0; i < SCHED_QUEUE_CAP + 1u; ++i) {
        u32 jid = 0;
        if (!scheduler_enqueue("/bin/hello", 1, hello_argv, &jid) || scheduler_run_ready(1) != 1) { reuse_ok = false; break; }
        last_job = jid;
    }
    check(reuse_ok && scheduler_get_job(last_job, &info) && info.state == SCHED_JOB_DONE, "scheduler reuses terminal queue slots after 33 jobs");
    suite_end();
}

