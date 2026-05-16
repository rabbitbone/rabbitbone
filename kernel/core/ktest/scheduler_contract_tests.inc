
static void scheduler_ktest_consume_resched_call(u32 cpu_id, void *raw) {
    (void)cpu_id;
    volatile u32 *done = (volatile u32 *)raw;
    cpu_regs_t regs;
    memset(&regs, 0, sizeof(regs));
    regs.cs = 0x08;
    (void)scheduler_tick(&regs);
    if (done) __atomic_store_n(done, 1u, __ATOMIC_RELEASE);
}

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
    rabbitbone_preemptinfo_t pi;
    check(scheduler_preempt_info(&pi) && pi.enabled == 1u && pi.quantum_ticks == SCHED_DEFAULT_QUANTUM_TICKS, "scheduler exposes preemption telemetry");
    u32 current_cpu = smp_current_cpu_id();
    if (current_cpu >= SMP_MAX_CPUS) current_cpu = 0;
    sched_cpu_runtime_info_t rt0;
    memset(&rt0, 0, sizeof(rt0));
    check(scheduler_cpu_runtime_info(current_cpu, &rt0) && rt0.cpu_id == current_cpu, "scheduler exposes per-cpu runtime state");
    check(scheduler_request_reschedule(current_cpu) && scheduler_need_resched(), "scheduler records local reschedule requests per cpu");
    cpu_regs_t resched_regs;
    memset(&resched_regs, 0, sizeof(resched_regs));
    resched_regs.cs = 0x08;
    check(scheduler_tick(&resched_regs) && !scheduler_need_resched(), "scheduler tick consumes pending reschedule request");
    const smp_info_t *smp_info_snapshot = smp_get_info();
    u32 remote_cpu = 0xffffffffu;
    smp_cpumask_t targetable = smp_targetable_cpu_mask();
    for (u32 i = 0; i < SMP_MAX_CPUS; ++i) {
        if (i != current_cpu && smp_cpumask_test_cpu(targetable, i) && smp_cpu_is_targetable(i)) { remote_cpu = i; break; }
    }
    if (remote_cpu != 0xffffffffu) {
        sched_cpu_runtime_info_t remote_before;
        sched_cpu_runtime_info_t remote_after;
        memset(&remote_before, 0, sizeof(remote_before));
        memset(&remote_after, 0, sizeof(remote_after));
        bool before_ok = scheduler_cpu_runtime_info(remote_cpu, &remote_before);
        u64 sent_before = smp_info_snapshot ? smp_info_snapshot->reschedule_ipi_sent : 0u;
        u64 wake_before = smp_info_snapshot ? smp_info_snapshot->cpus[remote_cpu].reschedule_ipi_wakeups : 0u;
        bool requested_remote = scheduler_request_reschedule(remote_cpu);
        bool ipi_observed = false;
        for (u32 spin = 0; spin < 250000u; ++spin) {
            if (scheduler_cpu_runtime_info(remote_cpu, &remote_after) &&
                remote_after.need_resched &&
                smp_info_snapshot &&
                smp_info_snapshot->reschedule_ipi_sent > sent_before &&
                smp_info_snapshot->cpus[remote_cpu].reschedule_ipi_wakeups > wake_before &&
                remote_after.reschedule_ipis >= remote_before.reschedule_ipis + 1u) {
                ipi_observed = true;
                break;
            }
            __asm__ volatile("pause");
        }
        volatile u32 consumed = 0;
        bool consumed_ok = smp_run_on_cpu_ex(remote_cpu, scheduler_ktest_consume_resched_call, (void *)&consumed, 100u, SMP_XCALL_F_ALLOW_IRQS_DISABLED | SMP_XCALL_F_ALLOW_IRQ_CONTEXT | SMP_XCALL_F_ALLOW_NESTED);
        check(before_ok && requested_remote && ipi_observed, "scheduler remote reschedule uses dedicated IPI path");
        check(consumed_ok && consumed == 1u, "scheduler remote reschedule request can be consumed on target cpu");
    } else {
        skip("scheduler remote reschedule IPI path requires more than one started CPU");
        skip("scheduler remote reschedule consume path requires more than one started CPU");
    }
    scheduler_idle_enter();
    scheduler_idle_exit();
    check(scheduler_cpu_runtime_info(current_cpu, &rt0) && rt0.idle_entries >= 1 && rt0.idle_ticks >= 1, "scheduler tracks per-cpu idle task accounting");
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

