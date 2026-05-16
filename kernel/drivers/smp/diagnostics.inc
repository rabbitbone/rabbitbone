const smp_info_t *smp_get_info(void) { return &smp_info; }

void smp_emit_diagnostic_snapshot(const char *reason) {
    __atomic_add_fetch(&smp_info.monitor_anomalies, 0u, __ATOMIC_RELAXED);
    KLOG(LOG_WARN, "smp", "diag reason=%s cpus=%u active=0x%x targetable=0x%x calls=%llu fail=%llu timeouts=%llu qmax=%llu wake_ipi=%llu/%llu resched_ipi=%llu/%llu last_timeout_cpu=%llu seq=%llu fn=%p ctx=%p flags=0x%x age_ns=%llu",
         reason ? reason : "manual", smp_info.cpu_count, smp_info.active_cpu_mask, smp_info.targetable_cpu_mask,
         (unsigned long long)smp_info.total_cross_calls,
         (unsigned long long)smp_info.total_cross_call_failures,
         (unsigned long long)smp_info.xcall_timeouts,
         (unsigned long long)smp_info.queue_max_depth,
         (unsigned long long)smp_info.wake_ipi_sent, (unsigned long long)smp_info.wake_ipi_failures,
         (unsigned long long)smp_info.reschedule_ipi_sent, (unsigned long long)smp_info.reschedule_ipi_failures,
         (unsigned long long)smp_info.xcall_last_timeout_cpu,
         (unsigned long long)smp_info.xcall_last_timeout_seq,
         (void *)smp_info.xcall_last_timeout_fn, (void *)smp_info.xcall_last_timeout_ctx,
         smp_info.xcall_last_timeout_flags,
         (unsigned long long)smp_info.xcall_last_timeout_age_ns);
    KLOG(LOG_WARN, "smp", "diag queue sync_exec=%llu async_exec=%llu sync_bypass=%llu ipi_exec=%llu tlb_last target=0x%x completed=0x%x skipped=0x%x gen=%llu pages=%u full=%u cr3=%p",
         (unsigned long long)smp_info.queue_sync_executed,
         (unsigned long long)smp_info.queue_async_executed,
         (unsigned long long)smp_info.queue_sync_bypasses,
         (unsigned long long)smp_info.queue_ipi_executed,
         smp_info.tlb_last_target_mask, smp_info.tlb_last_completed_mask, smp_info.tlb_last_skipped_mask,
         (unsigned long long)smp_info.tlb_last_generation, smp_info.tlb_last_pages, smp_info.tlb_last_full_flush,
         (void *)smp_info.tlb_last_target_cr3);
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        const smp_cpu_info_t *c = &smp_info.cpus[i];
        if (!c->started && c->boot_error == SMP_BOOT_ERROR_NONE && c->queue_depth == 0u) continue;
        KLOG(LOG_WARN, "smp", "diag cpu%u state=%s boot=%s/%s hb=%llu idle=%u q=%u/%u last_ipi=%u wake=0x%x last_xcall_seq=%llu src=%u fn=%p ctx=%p flags=0x%x start=%llu finish=%llu anomalies=%llu",
             i, smp_cpu_state_name(c->state), smp_boot_stage_name(c->boot_stage), smp_boot_error_name(c->boot_error),
             (unsigned long long)c->heartbeat, c->idle, c->queue_depth, c->queue_high_water,
             c->last_ipi_vector, c->last_wake_reason,
             (unsigned long long)c->last_xcall_seq, c->last_xcall_src_cpu,
             (void *)c->last_xcall_fn, (void *)c->last_xcall_ctx, c->last_xcall_flags,
             (unsigned long long)c->last_xcall_start_ns, (unsigned long long)c->last_xcall_finish_ns,
             (unsigned long long)c->anomalies);
    }
}

void smp_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    u64 calls = __atomic_load_n(&smp_info.total_cross_calls, __ATOMIC_RELAXED);
    u64 total_ns = __atomic_load_n(&smp_info.total_cross_call_ns, __ATOMIC_RELAXED);
    u64 avg_ns = calls ? total_ns / calls : 0;
    rabbitbone_buf_appendf(&bo, "smp: initialized=%u bootstrap_only=%u startup_attempted=%u background=%u cpus=%u enabled=%u started=%u failed=%u bsp_apic_id=%u masks possible=0x%x present=0x%x enabled=0x%x online=0x%x active=0x%x targetable=0x%x offline=0x%x booting=0x%x dying=0x%x dead=0x%x failed=0x%x bsp=0x%x boot_prepare=%llu boot_lock=%llu boot_timeouts=%llu boot_ipi_fail=%llu boot_last_failed=%llu calls=%llu call_fail=%llu async=%llu async_done=%llu reject=%llu reject_irq=%llu reject_if=%llu reject_nested=%llu reject_inactive=%llu local_inline=%llu remote_enq=%llu items=%llu item_fail=%llu requeues=%llu enq_fail=%llu qmax=%llu qipi=%llu tlb=%llu tlb_fail=%llu tlb_pages=%llu tlb_max_ns=%llu tlb_targeted=%llu tlb_broadcast=%llu tlb_skips=%llu tlb_gen=%llu xcall_avg_ns=%llu xcall_max_ns=%llu xcall_slow=%llu monitor=%llu anomalies=%llu ap_tasks=%u idle_policy=%llu lifecycle=%llu lifecycle_bad=%llu resched_ipi=%llu resched_fail=%llu resched_reject=%llu arch_init=%llu arch_fail=%llu arch_xsave=%llu arch_invariant_tsc=%llu\n",
            smp_info.initialized ? 1u : 0u, smp_info.bootstrap_only ? 1u : 0u,
            smp_info.startup_attempted ? 1u : 0u, smp_info.background_service ? 1u : 0u,
            smp_info.cpu_count, smp_info.enabled_cpu_count, smp_info.started_cpu_count,
            smp_info.failed_cpu_count, smp_info.bsp_apic_id,
            smp_info.possible_cpu_mask, smp_info.present_cpu_mask, smp_info.enabled_cpu_mask,
            smp_info.online_cpu_mask, smp_info.active_cpu_mask, smp_info.targetable_cpu_mask,
            smp_info.offline_cpu_mask, smp_info.booting_cpu_mask, smp_info.dying_cpu_mask,
            smp_info.dead_cpu_mask, smp_info.failed_cpu_mask, smp_info.bsp_cpu_mask,
            (unsigned long long)smp_info.boot_prepare_count,
            (unsigned long long)smp_info.boot_trampoline_lock_count,
            (unsigned long long)smp_info.boot_timeouts,
            (unsigned long long)smp_info.boot_ipi_failures,
            (unsigned long long)smp_info.boot_last_failed_cpu,
            (unsigned long long)calls,
            (unsigned long long)smp_info.total_cross_call_failures,
            (unsigned long long)smp_info.async_cross_calls,
            (unsigned long long)smp_info.async_call_completions,
            (unsigned long long)smp_info.xcall_rejections,
            (unsigned long long)smp_info.xcall_reject_irq_context,
            (unsigned long long)smp_info.xcall_reject_irq_disabled,
            (unsigned long long)smp_info.xcall_reject_nested,
            (unsigned long long)smp_info.xcall_reject_inactive_cpu,
            (unsigned long long)smp_info.xcall_local_inline,
            (unsigned long long)smp_info.xcall_remote_enqueued,
            (unsigned long long)smp_info.total_call_items,
            (unsigned long long)smp_info.total_call_item_failures,
            (unsigned long long)smp_info.queue_requeues,
            (unsigned long long)smp_info.queue_enqueue_failures,
            (unsigned long long)smp_info.queue_max_depth,
            (unsigned long long)smp_info.queue_ipi_executed,
            (unsigned long long)smp_info.tlb_shootdowns,
            (unsigned long long)smp_info.tlb_shootdown_failures,
            (unsigned long long)smp_info.tlb_pages,
            (unsigned long long)smp_info.tlb_max_ns,
            (unsigned long long)smp_info.tlb_targeted_shootdowns,
            (unsigned long long)smp_info.tlb_broadcast_shootdowns,
            (unsigned long long)smp_info.tlb_cpu_skips,
            (unsigned long long)smp_info.tlb_generation_updates,
            (unsigned long long)avg_ns,
            (unsigned long long)smp_info.max_cross_call_ns,
            (unsigned long long)smp_info.slow_cross_calls,
            (unsigned long long)smp_info.monitor_passes,
            (unsigned long long)smp_info.monitor_anomalies,
            smp_info.ap_kernel_tasks_enabled ? 1u : 0u,
            (unsigned long long)smp_info.idle_policy_version,
            (unsigned long long)smp_info.lifecycle_transitions,
            (unsigned long long)smp_info.lifecycle_invalid_transitions,
            (unsigned long long)smp_info.reschedule_ipi_sent,
            (unsigned long long)smp_info.reschedule_ipi_failures,
            (unsigned long long)smp_info.reschedule_reject_inactive_cpu,
            (unsigned long long)smp_info.arch_init_cpus,
            (unsigned long long)smp_info.arch_init_failures,
            (unsigned long long)smp_info.arch_xsave_cpus,
            (unsigned long long)smp_info.arch_invariant_tsc_cpus);
    rabbitbone_buf_appendf(&bo, "smp-observe: wake_ipi=%llu wake_fail=%llu xcall_timeouts=%llu timeout_reports=%llu last_timeout_cpu=%llu seq=%llu fn=%p ctx=%p flags=0x%x age_ns=%llu latency[%s]=%llu latency[%s]=%llu latency[%s]=%llu latency[%s]=%llu latency[%s]=%llu latency[%s]=%llu\n",
            (unsigned long long)smp_info.wake_ipi_sent,
            (unsigned long long)smp_info.wake_ipi_failures,
            (unsigned long long)smp_info.xcall_timeouts,
            (unsigned long long)smp_info.xcall_timeout_reports,
            (unsigned long long)smp_info.xcall_last_timeout_cpu,
            (unsigned long long)smp_info.xcall_last_timeout_seq,
            (void *)smp_info.xcall_last_timeout_fn,
            (void *)smp_info.xcall_last_timeout_ctx,
            smp_info.xcall_last_timeout_flags,
            (unsigned long long)smp_info.xcall_last_timeout_age_ns,
            smp_xcall_latency_bucket_name(0), (unsigned long long)smp_info.xcall_latency_buckets[0],
            smp_xcall_latency_bucket_name(1), (unsigned long long)smp_info.xcall_latency_buckets[1],
            smp_xcall_latency_bucket_name(2), (unsigned long long)smp_info.xcall_latency_buckets[2],
            smp_xcall_latency_bucket_name(3), (unsigned long long)smp_info.xcall_latency_buckets[3],
            smp_xcall_latency_bucket_name(4), (unsigned long long)smp_info.xcall_latency_buckets[4],
            smp_xcall_latency_bucket_name(5), (unsigned long long)smp_info.xcall_latency_buckets[5]);
    rabbitbone_buf_appendf(&bo, "smp-queue: sync_exec=%llu async_exec=%llu sync_bypass=%llu ipi_exec=%llu qmax=%llu enq_fail=%llu requeues=%llu\n",
            (unsigned long long)smp_info.queue_sync_executed,
            (unsigned long long)smp_info.queue_async_executed,
            (unsigned long long)smp_info.queue_sync_bypasses,
            (unsigned long long)smp_info.queue_ipi_executed,
            (unsigned long long)smp_info.queue_max_depth,
            (unsigned long long)smp_info.queue_enqueue_failures,
            (unsigned long long)smp_info.queue_requeues);
    rabbitbone_buf_appendf(&bo, "smp-tlb: last_target=0x%x last_completed=0x%x last_skipped=0x%x last_gen=%llu last_pages=%u last_full=%u last_virt=%p last_len=%llu last_cr3=%p\n",
            smp_info.tlb_last_target_mask, smp_info.tlb_last_completed_mask, smp_info.tlb_last_skipped_mask,
            (unsigned long long)smp_info.tlb_last_generation, smp_info.tlb_last_pages, smp_info.tlb_last_full_flush,
            (void *)smp_info.tlb_last_virt, (unsigned long long)smp_info.tlb_last_length,
            (void *)smp_info.tlb_last_target_cr3);
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        const smp_cpu_info_t *c = &smp_info.cpus[i];
        const smp_percpu_state_t *pc = &smp_info.percpu[i];
        rabbitbone_buf_appendf(&bo, "  cpu%u acpi=%u apic=%u package=%u core=%u thread=%u bsp=%u enabled=%u started=%u state=%s boot_stage=%s boot_error=%s gen=%u sipi=%u wait_rounds=%llu tramp=%p tramp_size=%llu boot_cr3=%p boot_entry=%p attempts=%u failures=%u stack=%p heartbeat=%llu work=%llu idle=%u idle_polling=%u idle_entries=%llu idle_wakeups=%llu hlt_entries=%llu hlt_wakeups=%llu ipi=%llu resched_ipi=%llu empty_ipi=%llu poll_spins=%llu wake_mask=0x%x wake_xcall=%llu wake_resched=%llu wake_timer=%llu wake_spurious=%llu q_depth=%u q_high=%u q_enq=%llu q_exec=%llu q_ipi=%llu q_wait_max_ns=%llu q_exec_max_ns=%llu last_ipi=%u last_wake=0x%x last_ipi_ticks=%llu last_xcall_seq=%llu last_xcall_src=%u last_xcall_fn=%p last_xcall_ctx=%p last_xcall_flags=0x%x last_xcall_start=%llu last_xcall_finish=%llu slow_xcall_items=%llu percpu=%p pcpu_installed=%u pcpu_xcall_depth=%u pcpu_fast=%llu pcpu_fallback=%llu cur_proc=%p cur_space=%p cur_cr3=%p tlb_seen=%llu arch_init=%u arch_features=0x%llx family=%u model=%u stepping=%u cr0=0x%llx cr4=0x%llx efer=0x%llx xcr0=0x%llx xsave=%u/%u fpu=%u sse=%u xsave_ready=%u nxe=%u pat_wc=%u invtsc=%u anomalies=%llu\n",
                c->logical_id, c->acpi_id, c->apic_id, c->package_id, c->core_id, c->thread_id, c->bsp ? 1u : 0u, c->enabled ? 1u : 0u,
                c->started ? 1u : 0u, smp_cpu_state_name(c->state), smp_boot_stage_name(c->boot_stage), smp_boot_error_name(c->boot_error),
                c->boot_generation_seen, c->boot_sipi_count, (unsigned long long)c->boot_wait_rounds,
                (void *)(uptr)c->boot_trampoline_base, (unsigned long long)c->boot_trampoline_size,
                (void *)(uptr)c->boot_cr3, (void *)(uptr)c->boot_entry,
                c->boot_attempts, c->boot_failures,
                (void *)(uptr)c->stack_top, (unsigned long long)c->heartbeat,
                (unsigned long long)c->work_executed, c->idle, c->idle_polling,
                (unsigned long long)c->idle_entries, (unsigned long long)c->idle_wakeups,
                (unsigned long long)c->idle_hlt_entries, (unsigned long long)c->idle_hlt_wakeups,
                (unsigned long long)c->ipi_wakeups, (unsigned long long)c->reschedule_ipi_wakeups,
                (unsigned long long)c->empty_ipis, (unsigned long long)c->idle_poll_spins,
                c->wake_reason_mask, (unsigned long long)c->wake_reason_xcall,
                (unsigned long long)c->wake_reason_reschedule, (unsigned long long)c->wake_reason_timer,
                (unsigned long long)c->wake_reason_spurious, c->queue_depth, c->queue_high_water,
                (unsigned long long)c->queue_enqueued, (unsigned long long)c->queue_executed,
                (unsigned long long)c->queue_ipi_executed,
                (unsigned long long)c->queue_max_wait_ns, (unsigned long long)c->queue_max_exec_ns,
                c->last_ipi_vector, c->last_wake_reason, (unsigned long long)c->last_ipi_ticks,
                (unsigned long long)c->last_xcall_seq, c->last_xcall_src_cpu,
                (void *)c->last_xcall_fn, (void *)c->last_xcall_ctx, c->last_xcall_flags,
                (unsigned long long)c->last_xcall_start_ns, (unsigned long long)c->last_xcall_finish_ns,
                (unsigned long long)c->slow_xcall_items,
                (void *)pc, pc->installed, pc->xcall_depth, (unsigned long long)pc->fast_id_hits,
                (unsigned long long)pc->apic_fallbacks, pc->current_process, pc->current_address_space,
                (void *)(uptr)pc->current_cr3, (unsigned long long)pc->tlb_generation_seen,
                c->arch_state.initialized, (unsigned long long)c->arch_state.features,
                c->arch_state.family, c->arch_state.model, c->arch_state.stepping,
                (unsigned long long)c->arch_state.cr0, (unsigned long long)c->arch_state.cr4,
                (unsigned long long)c->arch_state.efer, (unsigned long long)c->arch_state.xcr0,
                c->arch_state.xsave_area_size, c->arch_state.xsave_area_max_size,
                c->arch_state.fpu_ready, c->arch_state.sse_ready, c->arch_state.xsave_ready,
                c->arch_state.nxe_ready, c->arch_state.pat_wc_ready, c->arch_state.invariant_tsc_ready,
                (unsigned long long)c->anomalies);
        rabbitbone_buf_appendf(&bo, "  cpu%u-queue-tlb: q_sync=%llu q_async=%llu q_bypass=%llu q_ipi=%llu tlb_flushes=%llu tlb_skips=%llu tlb_last_gen=%llu\n",
                i,
                (unsigned long long)c->queue_sync_executed,
                (unsigned long long)c->queue_async_executed,
                (unsigned long long)c->queue_sync_bypasses,
                (unsigned long long)c->queue_ipi_executed,
                (unsigned long long)c->tlb_flushes,
                (unsigned long long)c->tlb_skips,
                (unsigned long long)c->tlb_last_generation);
    }
}


typedef struct smp_selftest_booting_identity_ctx {
    volatile u32 calls;
    volatile u32 mismatch;
} smp_selftest_booting_identity_ctx_t;

static void smp_selftest_booting_identity_call(u32 cpu_id, void *raw) {
    smp_selftest_booting_identity_ctx_t *ctx = (smp_selftest_booting_identity_ctx_t *)raw;
    if (!ctx) return;
    u32 observed = smp_current_cpu_id();
    if (observed != cpu_id) __atomic_add_fetch(&ctx->mismatch, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&ctx->calls, 1u, __ATOMIC_RELEASE);
}

bool smp_booting_identity_selftest(void) {
    if (smp_info.started_cpu_count <= 1u) return true;
    bool saved_booting = __atomic_load_n(&smp_booting_cpu, __ATOMIC_ACQUIRE);
    u32 saved_target = __atomic_load_n(&smp_boot_target_id, __ATOMIC_ACQUIRE);
    u32 saved_ack = __atomic_load_n(&smp_boot_ack_id, __ATOMIC_ACQUIRE);
    u64 saved_ack_generation = __atomic_load_n(&smp_boot_ack_generation, __ATOMIC_ACQUIRE);

    smp_selftest_booting_identity_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    __atomic_store_n(&smp_boot_target_id, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_id, 0xffffffffu, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_generation, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_booting_cpu, true, __ATOMIC_RELEASE);

    u32 mask = smp_started_cpu_mask();
    u32 completed = smp_run_on_mask_ex(mask, smp_selftest_booting_identity_call, &ctx, 100u,
                                         SMP_XCALL_F_FORCE_UNICAST | SMP_XCALL_F_ALLOW_IRQS_DISABLED);

    __atomic_store_n(&smp_booting_cpu, saved_booting, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_target_id, saved_target, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_id, saved_ack, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_generation, saved_ack_generation, __ATOMIC_RELEASE);

    return completed == smp_info.started_cpu_count &&
           __atomic_load_n(&ctx.calls, __ATOMIC_ACQUIRE) == smp_info.started_cpu_count &&
           __atomic_load_n(&ctx.mismatch, __ATOMIC_ACQUIRE) == 0u;
}

static void smp_selftest_call(u32 cpu_id, void *ctx) {
    volatile u32 *seen = (volatile u32 *)ctx;
    if (cpu_id < SMP_MAX_CPUS) __atomic_or_fetch(seen, 1u << cpu_id, __ATOMIC_ACQ_REL);
}

bool smp_selftest(void) {
    if (!smp_info.initialized) return false;
    if (smp_info.cpu_count == 0 || smp_info.cpu_count > SMP_MAX_CPUS) return false;
    u32 bsp_count = 0;
    u32 started = 0;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        const smp_cpu_info_t *c = &smp_info.cpus[i];
        if (c->logical_id != i) return false;
        if (c->bsp) ++bsp_count;
        if (c->started) ++started;
        if (c->started && !c->enabled) return false;
        if (c->started && !smp_cpu_state_onlineish(c->state)) return false;
        if (c->started && c->boot_error != SMP_BOOT_ERROR_NONE) return false;
        if (c->started) {
            const smp_percpu_state_t *pc = &smp_info.percpu[i];
            if (pc->self != pc || pc->logical_id != i || pc->installed == 0u || pc->online == 0u) return false;
            if (__atomic_load_n(&pc->xcall_depth, __ATOMIC_ACQUIRE) != 0u) return false;
            if (pc->current_address_space == 0 || pc->current_cr3 == 0u) return false;
            const cpu_arch_state_t *arch = &c->arch_state;
            if (arch->initialized == 0u || arch->fpu_ready == 0u || arch->sse_ready == 0u || arch->nxe_ready == 0u) return false;
            if ((arch->features & (CPU_ARCH_FLAG_FPU | CPU_ARCH_FLAG_FXSR | CPU_ARCH_FLAG_SSE | CPU_ARCH_FLAG_SSE2 | CPU_ARCH_FLAG_NX)) !=
                (CPU_ARCH_FLAG_FPU | CPU_ARCH_FLAG_FXSR | CPU_ARCH_FLAG_SSE | CPU_ARCH_FLAG_SSE2 | CPU_ARCH_FLAG_NX)) return false;
            if ((arch->features & CPU_ARCH_FLAG_PAT) != 0u && arch->pat_wc_ready == 0u) return false;
            if ((arch->features & CPU_ARCH_FLAG_XSAVE) != 0u) {
                if (arch->xsave_ready == 0u || (arch->xcr0 & 0x3u) != 0x3u || arch->xsave_area_size == 0u) return false;
            }
            if (!c->bsp) {
                if (c->boot_stage != SMP_BOOT_STAGE_ONLINE) return false;
                if (c->boot_attempts == 0u || c->boot_sipi_count < 2u) return false;
                if (c->boot_cr3 == 0u || (c->boot_cr3 >> 32u) != 0u || c->boot_entry == 0u) return false;
                if (c->boot_trampoline_base != SMP_TRAMPOLINE_BASE || c->boot_trampoline_size == 0u || c->boot_trampoline_size > SMP_TRAMPOLINE_MAX_SIZE) return false;
            }
        }
    }
    if (bsp_count != 1) return false;
    if (started != smp_info.started_cpu_count) return false;
    if (smp_info.arch_init_failures != 0u || smp_info.arch_init_cpus < smp_info.started_cpu_count) return false;
    if (smp_cpumask_weight(smp_info.possible_cpu_mask) != smp_info.cpu_count) return false;
    if ((smp_info.present_cpu_mask & ~smp_info.possible_cpu_mask) != 0u) return false;
    if ((smp_info.enabled_cpu_mask & ~smp_info.present_cpu_mask) != 0u) return false;
    if ((smp_info.online_cpu_mask & ~smp_info.enabled_cpu_mask) != 0u) return false;
    if ((smp_info.active_cpu_mask & ~smp_info.online_cpu_mask) != 0u) return false;
    if ((smp_info.targetable_cpu_mask & ~smp_info.active_cpu_mask) != 0u) return false;
    if ((smp_info.failed_cpu_mask & smp_info.online_cpu_mask) != 0u) return false;
    if ((smp_info.dead_cpu_mask & smp_info.online_cpu_mask) != 0u) return false;
    if ((smp_info.dying_cpu_mask & smp_info.active_cpu_mask) != 0u) return false;
    if (smp_cpumask_weight(smp_info.bsp_cpu_mask) != 1u) return false;
    if (smp_info.queue_sync_executed + smp_info.queue_async_executed > smp_info.total_call_items) return false;
    if (smp_info.queue_ipi_executed > smp_info.queue_sync_executed + smp_info.queue_async_executed) return false;
    if ((smp_info.tlb_last_completed_mask & ~smp_info.tlb_last_target_mask) != 0u) return false;
    if ((smp_info.tlb_last_skipped_mask & ~smp_info.tlb_last_target_mask) != 0u) return false;
    volatile u32 seen = 0;
    u32 selftest_mask = smp_started_cpu_mask();
    u32 selftest_done = smp_run_on_mask_ex(selftest_mask, smp_selftest_call, (void *)&seen, 50u,
                                            SMP_XCALL_F_ALLOW_IRQ_CONTEXT | SMP_XCALL_F_ALLOW_IRQS_DISABLED | SMP_XCALL_F_ALLOW_NESTED);
    u32 selftest_targets = smp_cpumask_weight(selftest_mask);
    if (selftest_done != selftest_targets) return selftest_targets == 1u;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if ((selftest_mask & (1u << i)) != 0u && ((seen & (1u << i)) == 0)) return false;
    }
    return true;
}
