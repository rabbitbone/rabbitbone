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
    u64 flags = spin_lock_irqsave(&scheduler_lock);
    if (!queue) { spin_unlock_irqrestore(&scheduler_lock, flags); return; }
    scheduler_recount_cpu_states_locked();
    kprintf("  cpu   queued running done failed migrated_in migrated_out dispatched idle need_resched ktask proc last_tick\n");
    for (u32 c = 0; c < sched_cpu_count_cached; ++c) {
        sched_cpu_runtime_t *rt = scheduler_runtime_for_cpu_locked(c);
        kprintf("  %-5u %-6u %-7u %-4u %-6u %-11u %-12u %-10llu %-4u %-12u %-5llu %-4llu %-9llu\n",
                sched_cpus[c].cpu_id, sched_cpus[c].queued, sched_cpus[c].running,
                sched_cpus[c].completed, sched_cpus[c].failed, sched_cpus[c].migrations_in,
                sched_cpus[c].migrations_out, (unsigned long long)sched_cpus[c].dispatched,
                rt->idle, rt->need_resched, (unsigned long long)rt->kernel_task_dispatches,
                (unsigned long long)rt->process_dispatches, (unsigned long long)sched_cpus[c].last_dispatch_ticks);
    }
    kprintf("  job   pid   cpu state    exit  path\n");
    for (usize i = 0; i < SCHED_QUEUE_CAP; ++i) {
        if (queue[i].info.state == SCHED_JOB_EMPTY) continue;
        kprintf("  %-5u %-5u %-3u %-8s %-5d %s\n", queue[i].info.job_id, queue[i].info.pid, queue[i].target_cpu,
                scheduler_job_state_name(queue[i].info.state), queue[i].info.exit_code, queue[i].info.path);
    }
    spin_unlock_irqrestore(&scheduler_lock, flags);
}

bool scheduler_selftest(void) {
    scheduler_init();
    scheduler_snapshot_t empty_snap;
    memset(&empty_snap, 0, sizeof(empty_snap));
    empty_snap.magic = SCHED_SNAPSHOT_MAGIC;
    empty_snap.next_job_id_copy = 1u;
    empty_snap.preempt_enabled_copy = true;
    empty_snap.quantum_ticks_copy = SCHED_DEFAULT_QUANTUM_TICKS;
    empty_snap.initialized_copy = false;
    if (!scheduler_snapshot_restore(&empty_snap, sizeof(empty_snap))) return false;
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
    u32 scpus = scheduler_cpu_count();
    if (scpus == 0 || scpus > SMP_MAX_CPUS) return false;
    sched_cpu_runtime_info_t rtinfo;
    if (!scheduler_cpu_runtime_info(0, &rtinfo) || rtinfo.cpu_id != 0) return false;
    if (!scheduler_request_reschedule(0) || !scheduler_need_resched()) return false;
    cpu_regs_t kernel_regs_for_request;
    memset(&kernel_regs_for_request, 0, sizeof(kernel_regs_for_request));
    kernel_regs_for_request.cs = 0x08;
    if (!scheduler_tick(&kernel_regs_for_request) || scheduler_need_resched()) return false;
    scheduler_idle_enter();
    scheduler_idle_exit();
    if (!scheduler_cpu_runtime_info(0, &rtinfo) || rtinfo.idle_entries == 0 || rtinfo.idle_ticks == 0) return false;
    sched_cpu_info_t cinfo;
    u32 qsum = 0;
    for (u32 c = 0; c < scpus; ++c) {
        if (!scheduler_cpu_info(c, &cinfo) || cinfo.cpu_id != c) return false;
        qsum += cinfo.queued;
    }
    if (qsum != 2u) return false;
    if (scpus > 1u && !scheduler_migrate_job(j2, scpus - 1u)) return false;
    if (scheduler_balance_queues() > SCHED_QUEUE_CAP) return false;
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
    rabbitbone_preemptinfo_t pi;
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
