static void smp_update_max_u64(volatile u64 *field, u64 value) {
    u64 old = __atomic_load_n(field, __ATOMIC_RELAXED);
    while (value > old && !__atomic_compare_exchange_n(field, &old, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

static void smp_update_max_u32(volatile u32 *field, u32 value) {
    u32 old = __atomic_load_n(field, __ATOMIC_RELAXED);
    while (value > old && !__atomic_compare_exchange_n(field, &old, value, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

static void smp_call_queue_init(void) {
    memset((void *)smp_call_queues, 0, sizeof(smp_call_queues));
    __atomic_store_n(&smp_call_seq, 0u, __ATOMIC_RELAXED);
    for (u32 i = 0; i < SMP_MAX_CPUS; ++i) spinlock_init(&smp_call_queues[i].lock);
}

static bool smp_queue_pending(u32 cpu_id) {
    if (!smp_cpu_targetable(cpu_id)) return false;
    return __atomic_load_n(&smp_call_queues[cpu_id].depth, __ATOMIC_ACQUIRE) != 0u;
}

static bool smp_enqueue_call_item(u32 dst_cpu, smp_call_item_t *item) {
    if (!smp_cpu_targetable(dst_cpu) || !item || !item->fn) {
        if (dst_cpu < SMP_MAX_CPUS) __atomic_add_fetch(&smp_info.xcall_reject_inactive_cpu, 1u, __ATOMIC_RELAXED);
        return false;
    }
    smp_call_queue_t *q = &smp_call_queues[dst_cpu];
    item->next = 0;
    item->dst_cpu = dst_cpu;
    item->queued_ns = timer_now_ns();
    item->started_ns = 0;
    item->finished_ns = 0;
    __atomic_store_n(&item->running, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&item->failed, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&item->queued_remote, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&item->done, 0u, __ATOMIC_RELEASE);

    u64 flags = spin_lock_irqsave(&q->lock);
    if (q->tail) q->tail->next = item;
    else q->head = item;
    q->tail = item;
    u32 depth = __atomic_add_fetch(&q->depth, 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&q->enqueued, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&q->last_seq, item->seq, __ATOMIC_RELAXED);
    smp_update_max_u32(&q->high_water, depth);
    spin_unlock_irqrestore(&q->lock, flags);

    smp_cpu_info_t *cpu = &smp_info.cpus[dst_cpu];
    __atomic_add_fetch(&cpu->queue_enqueued, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&cpu->queue_depth, depth, __ATOMIC_RELAXED);
    smp_update_max_u32(&cpu->queue_high_water, depth);
    smp_update_max_u64(&smp_info.queue_max_depth, (u64)depth);
    __atomic_add_fetch(&smp_info.total_call_items, 1u, __ATOMIC_RELAXED);
    return true;
}

static smp_call_item_t *smp_unlink_call_item_locked(u32 cpu_id, smp_call_queue_t *q, smp_call_item_t *prev, smp_call_item_t *item) {
    if (!q || !item) return 0;
    if (prev) prev->next = item->next;
    else q->head = item->next;
    if (q->tail == item) q->tail = prev;
    item->next = 0;
    u32 depth = __atomic_load_n(&q->depth, __ATOMIC_ACQUIRE);
    if (depth != 0u) depth = __atomic_sub_fetch(&q->depth, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&smp_info.cpus[cpu_id].queue_depth, depth, __ATOMIC_RELAXED);
    return item;
}

static bool smp_call_item_matches_drain(const smp_call_item_t *item, u32 required_flags) {
    if (!item) return false;
    return required_flags == 0u || (item->flags & required_flags) == required_flags;
}

static smp_call_item_t *smp_dequeue_call_item_matching(u32 cpu_id, u32 required_flags) {
    if (!smp_cpu_targetable(cpu_id)) return 0;
    smp_call_queue_t *q = &smp_call_queues[cpu_id];
    u64 flags = spin_lock_irqsave(&q->lock);

    smp_call_item_t *prev = 0;
    smp_call_item_t *cur = q->head;
    smp_call_item_t *first_prev = 0;
    smp_call_item_t *first_item = 0;
    smp_call_item_t *sync_prev = 0;
    smp_call_item_t *sync_item = 0;
    while (cur) {
        if (smp_call_item_matches_drain(cur, required_flags)) {
            if (!first_item) {
                first_prev = prev;
                first_item = cur;
            }
            if ((cur->flags & SMP_XCALL_F_ASYNC) == 0u) {
                sync_prev = prev;
                sync_item = cur;
                break;
            }
        }
        prev = cur;
        cur = cur->next;
    }

    smp_call_item_t *item = sync_item ? sync_item : first_item;
    smp_call_item_t *item_prev = sync_item ? sync_prev : first_prev;
    if (item) {
        if (sync_item && sync_prev) {
            __atomic_add_fetch(&q->sync_bypasses, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.cpus[cpu_id].queue_sync_bypasses, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.queue_sync_bypasses, 1u, __ATOMIC_RELAXED);
        }
        item = smp_unlink_call_item_locked(cpu_id, q, item_prev, item);
    }

    spin_unlock_irqrestore(&q->lock, flags);
    return item;
}


static bool smp_cancel_queued_call_item(u32 cpu_id, smp_call_item_t *target) {
    if (!smp_cpu_id_valid(cpu_id) || !target) return false;
    smp_call_queue_t *q = &smp_call_queues[cpu_id];
    u64 flags = spin_lock_irqsave(&q->lock);
    smp_call_item_t *prev = 0;
    smp_call_item_t *cur = q->head;
    while (cur) {
        if (cur == target) {
            if (__atomic_load_n(&cur->running, __ATOMIC_ACQUIRE) != 0u || __atomic_load_n(&cur->done, __ATOMIC_ACQUIRE) != 0u) break;
            if (prev) prev->next = cur->next;
            else q->head = cur->next;
            if (q->tail == cur) q->tail = prev;
            cur->next = 0;
            u32 depth = __atomic_load_n(&q->depth, __ATOMIC_ACQUIRE);
            if (depth) depth = __atomic_sub_fetch(&q->depth, 1u, __ATOMIC_ACQ_REL);
            __atomic_store_n(&smp_info.cpus[cpu_id].queue_depth, depth, __ATOMIC_RELAXED);
            __atomic_store_n(&cur->failed, 1u, __ATOMIC_RELEASE);
            __atomic_store_n(&cur->done, 1u, __ATOMIC_RELEASE);
            spin_unlock_irqrestore(&q->lock, flags);
            return true;
        }
        prev = cur;
        cur = cur->next;
    }
    spin_unlock_irqrestore(&q->lock, flags);
    return false;
}

static void smp_wait_stack_item_safe_or_panic(smp_call_item_t *item, u32 cpu_id) {
    if (!item || __atomic_load_n(&item->done, __ATOMIC_ACQUIRE) != 0u) return;
    for (u32 spin = 0; spin < SMP_CALL_CANCEL_SPIN_LIMIT; ++spin) {
        if (__atomic_load_n(&item->done, __ATOMIC_ACQUIRE) != 0u) return;
        if ((spin & 0x3ffu) == 0u) smp_wake_cpu(cpu_id);
        smp_cpu_pause();
    }
    smp_note_anomaly(cpu_id, "SMP queued call outlived synchronous stack frame");
    PANIC("SMP queued call cpu%u did not quiesce after timeout", cpu_id);
}

static void smp_execute_call_item(u32 cpu_id, smp_call_item_t *item) {
    if (!item || !item->fn) return;
    if (__atomic_load_n(&item->failed, __ATOMIC_ACQUIRE) != 0u) {
        __atomic_store_n(&item->done, 1u, __ATOMIC_RELEASE);
        if (__atomic_load_n(&item->heap_owned, __ATOMIC_ACQUIRE) != 0u) kfree(item);
        return;
    }
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    smp_call_queue_t *q = &smp_call_queues[cpu_id];
    smp_percpu_state_t *pcpu = smp_current_percpu();
    u64 start_ns = timer_now_ns();
    item->started_ns = start_ns;
    __atomic_store_n(&cpu->last_xcall_seq, item->seq, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_src_cpu, item->src_cpu, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_flags, item->flags, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_fn, (uptr)item->fn, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_ctx, (uptr)item->ctx, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_queued_ns, item->queued_ns, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_xcall_start_ns, start_ns, __ATOMIC_RELEASE);
    if (__atomic_load_n(&item->failed, __ATOMIC_ACQUIRE) != 0u) {
        __atomic_store_n(&item->done, 1u, __ATOMIC_RELEASE);
        if (__atomic_load_n(&item->heap_owned, __ATOMIC_ACQUIRE) != 0u) kfree(item);
        return;
    }
    if (item->queued_ns && start_ns >= item->queued_ns) {
        u64 wait_ns = start_ns - item->queued_ns;
        smp_update_max_u64(&q->max_wait_ns, wait_ns);
        smp_update_max_u64(&cpu->queue_max_wait_ns, wait_ns);
    }
    __atomic_store_n(&item->running, 1u, __ATOMIC_RELEASE);
    if (pcpu) __atomic_add_fetch(&pcpu->xcall_depth, 1u, __ATOMIC_ACQ_REL);
    item->fn(cpu_id, item->ctx);
    if (pcpu && __atomic_load_n(&pcpu->xcall_depth, __ATOMIC_ACQUIRE) != 0u) __atomic_sub_fetch(&pcpu->xcall_depth, 1u, __ATOMIC_ACQ_REL);
    u64 finish_ns = timer_now_ns();
    item->finished_ns = finish_ns;
    __atomic_store_n(&cpu->last_xcall_finish_ns, finish_ns, __ATOMIC_RELEASE);
    u64 exec_ns = finish_ns >= start_ns ? finish_ns - start_ns : 0;
    smp_update_max_u64(&q->max_exec_ns, exec_ns);
    smp_update_max_u64(&cpu->queue_max_exec_ns, exec_ns);
    if (exec_ns >= SMP_CALL_ITEM_SLOW_NS) {
        __atomic_add_fetch(&q->slow_items, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&cpu->slow_xcall_items, 1u, __ATOMIC_RELAXED);
    }
    if (__atomic_load_n(&item->queued_remote, __ATOMIC_ACQUIRE) != 0u) {
        __atomic_add_fetch(&q->executed, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&cpu->queue_executed, 1u, __ATOMIC_RELAXED);
        if ((item->flags & SMP_XCALL_F_ASYNC) != 0u) {
            __atomic_add_fetch(&q->async_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&cpu->queue_async_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.queue_async_executed, 1u, __ATOMIC_RELAXED);
        } else {
            __atomic_add_fetch(&q->sync_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&cpu->queue_sync_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.queue_sync_executed, 1u, __ATOMIC_RELAXED);
        }
        if ((item->flags & SMP_XCALL_F_IPI_IMMEDIATE) != 0u) {
            __atomic_add_fetch(&q->ipi_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&cpu->queue_ipi_executed, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.queue_ipi_executed, 1u, __ATOMIC_RELAXED);
        }
    }
    if ((item->flags & SMP_XCALL_F_ASYNC) != 0u) __atomic_add_fetch(&smp_info.async_call_completions, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&cpu->work_executed, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&item->running, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&item->done, 1u, __ATOMIC_RELEASE);
    if (__atomic_load_n(&item->heap_owned, __ATOMIC_ACQUIRE) != 0u) kfree(item);
}

static u32 smp_drain_cpu_queue_matching(u32 cpu_id, u32 budget, u32 required_flags) {
    if (!smp_cpu_targetable(cpu_id)) return 0;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    bool guarded = required_flags == 0u;
    if (guarded && __atomic_exchange_n(&cpu->draining, 1u, __ATOMIC_ACQ_REL) != 0u) return 0;
    u32 drained = 0;
    while (budget == 0u || drained < budget) {
        smp_call_item_t *item = smp_dequeue_call_item_matching(cpu_id, required_flags);
        if (!item) break;
        smp_execute_call_item(cpu_id, item);
        ++drained;
    }
    if (drained != 0u) smp_arm_active_poll(cpu);
    if (guarded) __atomic_store_n(&cpu->draining, 0u, __ATOMIC_RELEASE);
    return drained;
}

static u32 smp_drain_cpu_queue(u32 cpu_id, u32 budget) {
    return smp_drain_cpu_queue_matching(cpu_id, budget, 0u);
}

static u32 smp_drain_ipi_immediate_queue(u32 cpu_id, u32 budget) {
    return smp_drain_cpu_queue_matching(cpu_id, budget, SMP_XCALL_F_IPI_IMMEDIATE);
}

void smp_note_cpu_wake_reason(u32 cpu_id, u32 reason) {
    if (!smp_cpu_id_valid(cpu_id) || reason == 0u) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    __atomic_or_fetch(&cpu->wake_reason_mask, reason, __ATOMIC_ACQ_REL);
    if (reason & SMP_WAKE_REASON_XCALL) __atomic_add_fetch(&cpu->wake_reason_xcall, 1u, __ATOMIC_RELAXED);
    if (reason & SMP_WAKE_REASON_RESCHEDULE) __atomic_add_fetch(&cpu->wake_reason_reschedule, 1u, __ATOMIC_RELAXED);
    if (reason & SMP_WAKE_REASON_TIMER) __atomic_add_fetch(&cpu->wake_reason_timer, 1u, __ATOMIC_RELAXED);
    if (reason & SMP_WAKE_REASON_SPURIOUS) __atomic_add_fetch(&cpu->wake_reason_spurious, 1u, __ATOMIC_RELAXED);
}

static void smp_wake_cpu(u32 cpu_id) {
    if (!smp_cpu_targetable(cpu_id)) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    if (apic_send_fixed_ipi(cpu->apic_id, (u8)SMP_IPI_WAKE_VECTOR)) {
        __atomic_add_fetch(&smp_call_queues[cpu_id].kicks, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.wake_ipi_sent, 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&smp_info.wake_ipi_failures, 1u, __ATOMIC_RELAXED);
    }
}

bool smp_send_reschedule_ipi(u32 cpu_id) {
    if (!smp_cpu_targetable(cpu_id)) {
        __atomic_add_fetch(&smp_info.reschedule_reject_inactive_cpu, 1u, __ATOMIC_RELAXED);
        return false;
    }
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    if (apic_send_fixed_ipi(cpu->apic_id, (u8)SMP_IPI_RESCHEDULE_VECTOR)) {
        __atomic_add_fetch(&smp_info.reschedule_ipi_sent, 1u, __ATOMIC_RELAXED);
        return true;
    }
    __atomic_add_fetch(&smp_info.reschedule_ipi_failures, 1u, __ATOMIC_RELAXED);
    return false;
}

u32 smp_send_reschedule_ipi_mask(u32 cpu_mask) {
    u32 sent = 0;
    u32 online = smp_targetable_cpu_mask();
    cpu_mask &= online;
    for (u32 cpu = 0; cpu < smp_info.cpu_count && cpu < 32u; ++cpu) {
        if ((cpu_mask & (1u << cpu)) && smp_send_reschedule_ipi(cpu)) ++sent;
    }
    return sent;
}

const char *smp_xcall_latency_bucket_name(u32 bucket) {
    switch (bucket) {
        case 0: return "<10us";
        case 1: return "10-100us";
        case 2: return "100us-1ms";
        case 3: return "1-10ms";
        case 4: return "10-100ms";
        case 5: return ">=100ms";
        default: return "unknown";
    }
}

static u32 smp_xcall_latency_bucket(u64 elapsed_ns) {
    if (elapsed_ns < 10000ull) return 0u;
    if (elapsed_ns < 100000ull) return 1u;
    if (elapsed_ns < 1000000ull) return 2u;
    if (elapsed_ns < 10000000ull) return 3u;
    if (elapsed_ns < 100000000ull) return 4u;
    return 5u;
}

static void smp_record_ipi_event(u32 cpu_id, u32 vector, u32 reason) {
    if (!smp_cpu_id_valid(cpu_id)) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    __atomic_store_n(&cpu->last_ipi_vector, vector, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_wake_reason, reason, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_ipi_ticks, pit_ticks(), __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->last_ipi_tsc, cpu_read_tsc(), __ATOMIC_RELEASE);
}

static void smp_record_xcall_duration(u64 elapsed_ns, bool ok) {
    __atomic_add_fetch(&smp_info.total_cross_call_ns, elapsed_ns, __ATOMIC_RELAXED);
    if (!ok) __atomic_add_fetch(&smp_info.total_cross_call_failures, 1u, __ATOMIC_RELAXED);
    if (elapsed_ns >= SMP_XCALL_SLOW_NS) __atomic_add_fetch(&smp_info.slow_cross_calls, 1u, __ATOMIC_RELAXED);
    u32 bucket = smp_xcall_latency_bucket(elapsed_ns);
    if (bucket < SMP_XCALL_LATENCY_BUCKETS) __atomic_add_fetch(&smp_info.xcall_latency_buckets[bucket], 1u, __ATOMIC_RELAXED);
    smp_update_max_u64(&smp_info.max_cross_call_ns, elapsed_ns);
}

static void smp_report_xcall_timeout(u32 dst_cpu, const smp_call_item_t *item, u64 age_ns) {
    __atomic_add_fetch(&smp_info.xcall_timeouts, 1u, __ATOMIC_RELAXED);
    if (item) {
        __atomic_store_n(&smp_info.xcall_last_timeout_seq, item->seq, __ATOMIC_RELEASE);
        __atomic_store_n(&smp_info.xcall_last_timeout_fn, (uptr)item->fn, __ATOMIC_RELEASE);
        __atomic_store_n(&smp_info.xcall_last_timeout_ctx, (uptr)item->ctx, __ATOMIC_RELEASE);
        __atomic_store_n(&smp_info.xcall_last_timeout_flags, item->flags, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&smp_info.xcall_last_timeout_cpu, dst_cpu, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.xcall_last_timeout_age_ns, age_ns, __ATOMIC_RELEASE);
    __atomic_add_fetch(&smp_info.xcall_timeout_reports, 1u, __ATOMIC_RELAXED);
    if (dst_cpu < smp_info.cpu_count) {
        const smp_cpu_info_t *c = &smp_info.cpus[dst_cpu];
        KLOG(LOG_ERROR, "smp", "xcall timeout cpu%u state=%s q_depth=%u q_high=%u last_seq=%llu fn=%p ctx=%p flags=0x%x age_ns=%llu ipi_vec=%u wake=0x%x hb=%llu idle=%u xcall_depth=%u",
             dst_cpu, smp_cpu_state_name(c->state), c->queue_depth, c->queue_high_water,
             item ? (unsigned long long)item->seq : 0ull,
             item ? (void *)(uptr)item->fn : 0, item ? item->ctx : 0, item ? item->flags : 0u,
             (unsigned long long)age_ns, c->last_ipi_vector, c->last_wake_reason,
             (unsigned long long)c->heartbeat, c->idle, smp_info.percpu[dst_cpu].xcall_depth);
    }
}

static void smp_arm_active_poll(smp_cpu_info_t *cpu) {
    if (!cpu || pit_ticks() == 0) return;
    __atomic_store_n(&cpu->active_poll_until_ns, timer_now_ns() + SMP_AP_ACTIVE_POLL_NS, __ATOMIC_RELAXED);
}


void smp_note_interrupt_enter(u32 vector, bool from_user) {
    u32 cpu_id = smp_current_cpu_id();
    if (!smp_cpu_id_valid(cpu_id)) cpu_id = 0;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (pcpu) __atomic_add_fetch(&pcpu->interrupt_depth, 1u, __ATOMIC_ACQ_REL);
    u32 depth = __atomic_add_fetch(&cpu->interrupt_depth, 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&cpu->interrupt_entries, 1u, __ATOMIC_RELAXED);
    smp_update_max_u32(&cpu->interrupt_max_depth, depth);
    if (depth > 8u) __atomic_add_fetch(&cpu->interrupt_depth_anomalies, 1u, __ATOMIC_RELAXED);
    if (vector == RABBITBONE_APIC_TIMER_VECTOR) {
        smp_note_cpu_wake_reason(cpu_id, SMP_WAKE_REASON_TIMER);
        __atomic_add_fetch(&cpu->local_timer_ticks, 1u, __ATOMIC_RELAXED);
        if (from_user) __atomic_add_fetch(&cpu->local_timer_user_ticks, 1u, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&cpu->local_timer_kernel_ticks, 1u, __ATOMIC_RELAXED);
        if (cpu_id != smp_current_cpu_id()) __atomic_add_fetch(&cpu->timer_cpu_id_mismatches, 1u, __ATOMIC_RELAXED);
    }
}

void smp_note_interrupt_exit(u32 vector) {
    u32 cpu_id = smp_current_cpu_id();
    if (!smp_cpu_id_valid(cpu_id)) cpu_id = 0;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    u32 depth = __atomic_load_n(&cpu->interrupt_depth, __ATOMIC_ACQUIRE);
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (pcpu && __atomic_load_n(&pcpu->interrupt_depth, __ATOMIC_ACQUIRE) != 0u) __atomic_sub_fetch(&pcpu->interrupt_depth, 1u, __ATOMIC_ACQ_REL);
    if (depth == 0u) {
        __atomic_add_fetch(&cpu->interrupt_depth_anomalies, 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_sub_fetch(&cpu->interrupt_depth, 1u, __ATOMIC_ACQ_REL);
    }
    __atomic_add_fetch(&cpu->interrupt_exits, 1u, __ATOMIC_RELAXED);
    if (vector == RABBITBONE_APIC_TIMER_VECTOR) __atomic_add_fetch(&cpu->local_timer_eois, 1u, __ATOMIC_RELAXED);
}

void smp_background_service_enable(bool enabled) {
    __atomic_store_n(&smp_info.background_service, enabled, __ATOMIC_RELEASE);
}

void smp_ap_kernel_tasks_enable(bool enabled) {
    __atomic_store_n(&smp_info.ap_kernel_tasks_enabled, enabled, __ATOMIC_RELEASE);
    __atomic_add_fetch(&smp_info.idle_policy_version, 1u, __ATOMIC_RELAXED);
}

u32 smp_flush_local_call_queue(void) {
    u32 cpu_id = smp_current_cpu_id();
    if (!smp_cpu_id_valid(cpu_id)) return 0u;
    return smp_drain_cpu_queue(cpu_id, 0u);
}

void smp_ap_poll_once(void) {
    u32 cpu_id = smp_current_cpu_id();
    if (!smp_cpu_id_valid(cpu_id)) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    __atomic_add_fetch(&cpu->heartbeat, 1u, __ATOMIC_RELAXED);
    (void)smp_drain_cpu_queue(cpu_id, SMP_CALL_DRAIN_BATCH);
}

void smp_monitor_tick(void) {
    if (!__atomic_load_n(&smp_info.background_service, __ATOMIC_ACQUIRE)) return;
    u32 current = smp_current_cpu_id();
    if (smp_cpu_id_valid(current) && !smp_info.cpus[current].bsp) return;
    u64 now = smp_now_ticks();
    u64 last = __atomic_load_n(&smp_monitor_last_ticks, __ATOMIC_RELAXED);
    if (now == last || (last != 0 && now - last < SMP_MONITOR_PERIOD_TICKS)) return;
    __atomic_store_n(&smp_monitor_last_ticks, now, __ATOMIC_RELAXED);
    __atomic_add_fetch(&smp_info.monitor_passes, 1u, __ATOMIC_RELAXED);

    u32 bsp_count = 0;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        smp_cpu_info_t *cpu = &smp_info.cpus[i];
        if (cpu->bsp) ++bsp_count;
        if (!cpu->enabled || !cpu->started) continue;
        u64 hb = __atomic_load_n(&cpu->heartbeat, __ATOMIC_RELAXED);
        u64 prev = __atomic_load_n(&smp_monitor_last_heartbeat[i], __ATOMIC_RELAXED);
        u32 idle = __atomic_load_n(&cpu->idle, __ATOMIC_ACQUIRE);
        u32 qdepth = __atomic_load_n(&cpu->queue_depth, __ATOMIC_ACQUIRE);
        if (prev != 0 && hb == prev && !cpu->bsp && !idle && qdepth != 0u) {
            u64 report_last = __atomic_load_n(&smp_monitor_last_report_ticks[i], __ATOMIC_RELAXED);
            if (report_last == 0 || now - report_last >= SMP_MONITOR_REPORT_PERIOD_TICKS) {
                __atomic_store_n(&smp_monitor_last_report_ticks[i], now, __ATOMIC_RELAXED);
                smp_note_anomaly(i, "heartbeat stalled with queued SMP work");
            }
        }
        __atomic_store_n(&smp_monitor_last_heartbeat[i], hb, __ATOMIC_RELAXED);
    }
    if (bsp_count != 1u) smp_note_anomaly(0, "invalid BSP count");
}

static bool smp_ap_poll_window(u32 cpu_id) {
    if (!smp_cpu_id_valid(cpu_id)) return false;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    u64 until_ns = __atomic_load_n(&cpu->active_poll_until_ns, __ATOMIC_RELAXED);
    if (until_ns == 0 || timer_now_ns() >= until_ns) return false;
    for (u32 i = 0; i < SMP_AP_ACTIVE_POLL_BATCH; ++i) {
        if (smp_queue_pending(cpu_id)) return true;
        __atomic_add_fetch(&cpu->idle_poll_spins, 1u, __ATOMIC_RELAXED);
        smp_cpu_pause();
    }
    return smp_queue_pending(cpu_id);
}

static void smp_ap_wait_for_work(u32 cpu_id) {
    if (!smp_cpu_id_valid(cpu_id)) return;
    if (smp_ap_poll_window(cpu_id)) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    u64 flags = irq_save();
    bool irq_was_enabled = (flags & (1ull << 9)) != 0;
    if (!irq_was_enabled) {
        irq_restore(flags);
        __atomic_store_n(&cpu->idle_polling, 1u, __ATOMIC_RELEASE);
        for (u32 i = 0; i < 256u; ++i) {
            if (smp_queue_pending(cpu_id) || scheduler_need_resched()) break;
            __atomic_add_fetch(&cpu->idle_poll_spins, 1u, __ATOMIC_RELAXED);
            smp_cpu_pause();
        }
        __atomic_store_n(&cpu->idle_polling, 0u, __ATOMIC_RELEASE);
        return;
    }
    if (!smp_queue_pending(cpu_id) && !scheduler_need_resched()) {
        __atomic_store_n(&cpu->idle, 1u, __ATOMIC_RELEASE);
        __atomic_store_n(&cpu->idle_polling, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&cpu->wake_reason_mask, 0u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&cpu->idle_entries, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&cpu->idle_hlt_entries, 1u, __ATOMIC_RELAXED);
        scheduler_idle_enter();
        __asm__ volatile("sti; hlt; cli" ::: "memory");
        scheduler_idle_exit();
        __atomic_store_n(&cpu->idle, 0u, __ATOMIC_RELEASE);
        __atomic_add_fetch(&cpu->idle_wakeups, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&cpu->idle_hlt_wakeups, 1u, __ATOMIC_RELAXED);
        u32 reason = __atomic_exchange_n(&cpu->wake_reason_mask, 0u, __ATOMIC_ACQ_REL);
        if (reason == 0u) smp_note_cpu_wake_reason(cpu_id, SMP_WAKE_REASON_SPURIOUS);
        smp_arm_active_poll(cpu);
    }
    irq_restore(flags);
}

static void smp_idle_body(u32 logical_id) {
    for (;;) {
        smp_ap_poll_once();
        if (__atomic_load_n(&smp_info.ap_kernel_tasks_enabled, __ATOMIC_ACQUIRE) && task_is_initialized()) task_run_ready(1);
        smp_ap_wait_for_work(logical_id);
    }
}

void smp_ap_entry(u32 logical_id) {
    __atomic_store_n(&smp_boot_target_id, logical_id, __ATOMIC_RELEASE);
    if (logical_id >= SMP_MAX_CPUS) cpu_halt_forever();
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    if (mb) __atomic_store_n(&mb->tsc_entered, cpu_read_tsc(), __ATOMIC_RELEASE);
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_ENTERED_C);
    idt_load();
    cpu_init();
    smp_record_cpu_arch_state(logical_id);
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_CPU_INIT);
    gdt_init();
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_GDT_READY);
    if (!gdt_install_dynamic_stacks(64u * 1024u, 16u * 1024u)) {
        __atomic_add_fetch(&smp_info.cpus[logical_id].anomalies, 1u, __ATOMIC_RELAXED);
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_DYNAMIC_STACKS);
    } else {
        smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_STACKS_READY);
    }
    gdt_init();
    smp_percpu_install_cpu(logical_id);
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_PERCPU_READY);
    vmm_note_cpu_current_space(vmm_kernel_space());
    idt_load();
    apic_local_init();
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_APIC_READY);
    if (apic_local_timer_enable((u8)RABBITBONE_APIC_TIMER_VECTOR, RABBITBONE_APIC_TIMER_INITIAL_COUNT, true)) {
        smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_TIMER_READY);
    }
    u64 flags = spin_lock_irqsave(&smp_lock);
    smp_cpu_info_t *cpu = &smp_info.cpus[logical_id];
    cpu->started = true;
    smp_cpu_set_state_locked(logical_id, SMP_CPU_ACTIVE);
    cpu->heartbeat = 1;
    cpu->idle = 0;
    smp_recount_locked();
    spin_unlock_irqrestore(&smp_lock, flags);
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_ONLINE);
    u64 boot_generation = mb ? __atomic_load_n(&mb->generation, __ATOMIC_ACQUIRE) : __atomic_load_n(&smp_info.boot_generation, __ATOMIC_ACQUIRE);
    if (mb) __atomic_store_n(&mb->acked, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_id, logical_id, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_generation, boot_generation, __ATOMIC_RELEASE);
    KLOG(LOG_INFO, "smp", "cpu%u entered kernel apic=%u", logical_id, cpu->apic_id);
    cpu_sti();
    smp_idle_body(logical_id);
    __builtin_unreachable();
}

static bool smp_prepare_call(u32 dst_cpu, smp_call_item_t *item, smp_call_fn_t fn, void *ctx, u64 seq, u32 src_cpu, u32 flags) {
    if (!item || !fn || !smp_cpu_id_valid(dst_cpu)) return false;
    memset(item, 0, sizeof(*item));
    item->seq = seq;
    item->src_cpu = src_cpu;
    item->dst_cpu = dst_cpu;
    item->fn = fn;
    item->ctx = ctx;
    item->flags = flags;
    return true;
}

static bool smp_irq_enabled_now(void) {
#if defined(RABBITBONE_HOST_TEST)
    return true;
#else
    u64 flags = 0;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
    return (flags & SMP_RFLAGS_IF) != 0u;
#endif
}

static bool smp_xcall_context_allowed(u32 cpu_mask, u32 flags) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    u32 current = pcpu ? pcpu->logical_id : smp_current_cpu_id();
    u32 current_bit = current < 32u ? (1u << current) : 0u;
    bool has_remote = (cpu_mask & ~current_bit) != 0u;
    bool async = (flags & SMP_XCALL_F_ASYNC) != 0u;
    if (!has_remote) return true;
    if (pcpu && __atomic_load_n(&pcpu->interrupt_depth, __ATOMIC_ACQUIRE) != 0u && (flags & SMP_XCALL_F_ALLOW_IRQ_CONTEXT) == 0u) {
        __atomic_add_fetch(&smp_info.xcall_rejections, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.xcall_reject_irq_context, 1u, __ATOMIC_RELAXED);
        return false;
    }
    if (!smp_irq_enabled_now() && (flags & SMP_XCALL_F_ALLOW_IRQS_DISABLED) == 0u) {
        __atomic_add_fetch(&smp_info.xcall_rejections, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.xcall_reject_irq_disabled, 1u, __ATOMIC_RELAXED);
        return false;
    }
    if (!async && pcpu && __atomic_load_n(&pcpu->xcall_depth, __ATOMIC_ACQUIRE) != 0u && (flags & SMP_XCALL_F_ALLOW_NESTED) == 0u) {
        __atomic_add_fetch(&smp_info.xcall_rejections, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.xcall_reject_nested, 1u, __ATOMIC_RELAXED);
        return false;
    }
    return true;
}

static smp_call_item_t *smp_alloc_async_call(u32 dst_cpu, smp_call_fn_t fn, void *ctx, u64 seq, u32 src_cpu, u32 flags) {
    smp_call_item_t *item = (smp_call_item_t *)kmalloc(sizeof(smp_call_item_t));
    if (!item) return 0;
    if (!smp_prepare_call(dst_cpu, item, fn, ctx, seq, src_cpu, flags | SMP_XCALL_F_ASYNC)) {
        kfree(item);
        return 0;
    }
    __atomic_store_n(&item->heap_owned, 1u, __ATOMIC_RELEASE);
    return item;
}

static void smp_kick_remote_mask(u32 remote_mask, u32 current, u32 flags) {
    bool broadcast_kick = false;
    u32 current_bit = current < 32u ? (1u << current) : 0u;
    u32 all_other_targetable = smp_targetable_cpu_mask() & SMP_CPUMASK_VALID & ~current_bit;
    bool can_broadcast = remote_mask != 0u && remote_mask == all_other_targetable &&
                         (flags & SMP_XCALL_F_FORCE_UNICAST) == 0u &&
                         current < smp_info.cpu_count && smp_info.cpus[current].bsp;
    for (u32 i = 0; can_broadcast && i < smp_info.cpu_count; ++i) {
        if (smp_info.cpus[i].enabled && !smp_info.cpus[i].bsp && !smp_info.cpus[i].started) can_broadcast = false;
    }
    if (can_broadcast) {
        broadcast_kick = apic_send_fixed_ipi_all_others((u8)SMP_IPI_WAKE_VECTOR);
        if (broadcast_kick) __atomic_add_fetch(&smp_info.wake_ipi_sent, 1u, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&smp_info.wake_ipi_failures, 1u, __ATOMIC_RELAXED);
    }
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if ((remote_mask & (1u << i)) == 0u) continue;
        if (!broadcast_kick) smp_wake_cpu(i);
        else __atomic_add_fetch(&smp_call_queues[i].kicks, 1u, __ATOMIC_RELAXED);
    }
}

static bool smp_wait_sync_items(smp_call_item_t *items, u32 cpu_mask, u32 target_count, u64 timeout_ticks, u64 xcall_start_ns) {
    u64 start = smp_now_ticks();
    u64 last_kick[SMP_MAX_CPUS];
    memset(last_kick, 0, sizeof(last_kick));
    u64 initial_kick = start;
    for (u32 i = 0; i < SMP_MAX_CPUS; ++i) if (cpu_mask & (1u << i)) last_kick[i] = initial_kick;

    u64 spin_budget = 0;
    for (;;) {
        u64 now = smp_now_ticks();
        u32 done = 0;
        for (u32 i = 0; i < smp_info.cpu_count; ++i) {
            if ((cpu_mask & (1u << i)) == 0u) continue;
            if (__atomic_load_n(&items[i].done, __ATOMIC_ACQUIRE) != 0u) {
                ++done;
                continue;
            }
            if ((now != last_kick[i] && now - last_kick[i] >= SMP_CALL_WAIT_REKICK_TICKS) || (now == start && (spin_budget & 0x3fffu) == 0u)) {
                smp_wake_cpu(i);
                last_kick[i] = now;
                __atomic_add_fetch(&smp_info.queue_requeues, 1u, __ATOMIC_RELAXED);
            }
        }
        if (done == target_count) {
            smp_record_xcall_duration(timer_now_ns() - xcall_start_ns, true);
            return true;
        }
        if (now != start && now - start > timeout_ticks) break;
        if (now == start && ++spin_budget > SMP_CALL_WAIT_SPIN_LIMIT) break;
        u32 self = smp_current_cpu_id();
        if (smp_cpu_id_valid(self)) (void)smp_drain_cpu_queue(self, SMP_CALL_DRAIN_BATCH);
        for (u32 i = 0; i < 128u; ++i) smp_cpu_pause();
    }
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if ((cpu_mask & (1u << i)) && __atomic_load_n(&items[i].done, __ATOMIC_ACQUIRE) == 0u) {
            __atomic_store_n(&items[i].failed, 1u, __ATOMIC_RELEASE);
            if (!smp_cancel_queued_call_item(i, &items[i])) smp_wait_stack_item_safe_or_panic(&items[i], i);
            __atomic_add_fetch(&smp_info.total_call_item_failures, 1u, __ATOMIC_RELAXED);
            u64 age_ns = 0u;
            if (items[i].queued_ns != 0u) {
                u64 now_ns = timer_now_ns();
                age_ns = now_ns >= items[i].queued_ns ? now_ns - items[i].queued_ns : 0u;
            }
            smp_report_xcall_timeout(i, &items[i], age_ns);
            smp_note_anomaly(i, "SMP queued call timed out");
        }
    }
    smp_record_xcall_duration(timer_now_ns() - xcall_start_ns, false);
    return false;
}

u32 smp_run_on_mask_ex(u32 cpu_mask, smp_call_fn_t fn, void *ctx, u64 timeout_ticks, u32 flags) {
    if (!fn || !smp_info.initialized) return 0;
    bool async = (flags & SMP_XCALL_F_ASYNC) != 0u;
    if (!async && timeout_ticks == 0u) timeout_ticks = SMP_XCALL_DEFAULT_TIMEOUT_TICKS;
    u64 xcall_start_ns = timer_now_ns();
    u32 requested_mask = cpu_mask;
    u32 current = smp_current_cpu_id();
    u32 started_mask = smp_targetable_cpu_mask();
    u32 current_bit = current < 32u ? (1u << current) : 0u;
    if ((started_mask & current_bit) == 0u && (started_mask & 1u) != 0u) {
        current = 0u;
        current_bit = 1u;
    }
    if (requested_mask == SMP_CPU_MASK_ALL) cpu_mask = started_mask;
    else cpu_mask = requested_mask & started_mask;
    if (cpu_mask == 0u) return 0;
    if (!smp_xcall_context_allowed(cpu_mask, flags)) return 0;

    u64 seq = __atomic_add_fetch(&smp_call_seq, 1u, __ATOMIC_ACQ_REL);
    u32 targets = 0;
    u32 local_targets = 0;
    u32 remote_mask = 0;
    u32 issued_mask = 0;

    if (async) {
        for (u32 i = 0; i < smp_info.cpu_count; ++i) {
            if ((cpu_mask & (1u << i)) == 0u) continue;
            smp_call_item_t *item = smp_alloc_async_call(i, fn, ctx, seq, current, flags);
            if (!item) {
                __atomic_add_fetch(&smp_info.queue_enqueue_failures, 1u, __ATOMIC_RELAXED);
                __atomic_add_fetch(&smp_info.total_call_item_failures, 1u, __ATOMIC_RELAXED);
                continue;
            }
            if (i == current) {
                item->queued_ns = xcall_start_ns;
                __atomic_add_fetch(&smp_info.xcall_local_inline, 1u, __ATOMIC_RELAXED);
                smp_execute_call_item(i, item);
                issued_mask |= 1u << i;
                ++targets;
                ++local_targets;
            } else if (smp_enqueue_call_item(i, item)) {
                issued_mask |= 1u << i;
                remote_mask |= 1u << i;
                ++targets;
                __atomic_add_fetch(&smp_info.xcall_remote_enqueued, 1u, __ATOMIC_RELAXED);
            } else {
                __atomic_add_fetch(&smp_info.queue_enqueue_failures, 1u, __ATOMIC_RELAXED);
                __atomic_add_fetch(&smp_info.total_call_item_failures, 1u, __ATOMIC_RELAXED);
                kfree(item);
            }
        }
        if (targets == 0u) return 0;
        __atomic_add_fetch(&smp_info.total_cross_calls, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.async_cross_calls, 1u, __ATOMIC_RELAXED);
        smp_kick_remote_mask(remote_mask, current, flags);
        smp_record_xcall_duration(timer_now_ns() - xcall_start_ns, true);
        (void)local_targets;
        (void)issued_mask;
        return targets;
    }

    smp_call_item_t items[SMP_MAX_CPUS];
    memset(items, 0, sizeof(items));
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if ((cpu_mask & (1u << i)) == 0u) continue;
        if (!smp_prepare_call(i, &items[i], fn, ctx, seq, current, flags)) continue;
        if (i == current) {
            items[i].queued_ns = xcall_start_ns;
            __atomic_add_fetch(&smp_info.xcall_local_inline, 1u, __ATOMIC_RELAXED);
            smp_execute_call_item(i, &items[i]);
            if (__atomic_load_n(&items[i].done, __ATOMIC_ACQUIRE) != 0u) {
                issued_mask |= 1u << i;
                ++targets;
                ++local_targets;
            }
        } else if (smp_enqueue_call_item(i, &items[i])) {
            issued_mask |= 1u << i;
            remote_mask |= 1u << i;
            ++targets;
            __atomic_add_fetch(&smp_info.xcall_remote_enqueued, 1u, __ATOMIC_RELAXED);
        } else {
            __atomic_add_fetch(&smp_info.queue_enqueue_failures, 1u, __ATOMIC_RELAXED);
            __atomic_store_n(&items[i].failed, 1u, __ATOMIC_RELEASE);
            __atomic_store_n(&items[i].done, 1u, __ATOMIC_RELEASE);
            __atomic_add_fetch(&smp_info.total_call_item_failures, 1u, __ATOMIC_RELAXED);
        }
    }

    if (targets == 0u) return 0;
    __atomic_add_fetch(&smp_info.total_cross_calls, 1u, __ATOMIC_RELAXED);
    smp_kick_remote_mask(remote_mask, current, flags);

    u32 completed = 0;
    if (remote_mask == 0u) {
        smp_record_xcall_duration(timer_now_ns() - xcall_start_ns, true);
        completed = targets;
    } else if (smp_wait_sync_items(items, remote_mask, targets - local_targets, timeout_ticks, xcall_start_ns)) {
        completed = targets;
    } else {
        for (u32 i = 0; i < smp_info.cpu_count; ++i) {
            if ((issued_mask & (1u << i)) && __atomic_load_n(&items[i].done, __ATOMIC_ACQUIRE) && __atomic_load_n(&items[i].failed, __ATOMIC_ACQUIRE) == 0u) ++completed;
        }
    }
    return completed;
}

bool smp_run_on_cpu_ex(u32 cpu_id, smp_call_fn_t fn, void *ctx, u64 timeout_ticks, u32 flags) {
    if (cpu_id >= 32u) return false;
    return smp_run_on_mask_ex(1u << cpu_id, fn, ctx, timeout_ticks, flags) == 1u;
}

u32 smp_run_on_mask(u32 cpu_mask, smp_call_fn_t fn, void *ctx, u64 timeout_ticks) {
    return smp_run_on_mask_ex(cpu_mask, fn, ctx, timeout_ticks, 0u);
}

bool smp_run_on_cpu(u32 cpu_id, smp_call_fn_t fn, void *ctx, u64 timeout_ticks) {
    return smp_run_on_cpu_ex(cpu_id, fn, ctx, timeout_ticks, 0u);
}

bool smp_run_on_all(smp_call_fn_t fn, void *ctx, u64 timeout_ticks) {
    u32 mask = smp_targetable_cpu_mask();
    u32 n = smp_run_on_mask_ex(mask, fn, ctx, timeout_ticks, 0u);
    return n == smp_cpumask_weight(mask);
}

u32 smp_run_on_mask_async(u32 cpu_mask, smp_call_fn_t fn, void *ctx) {
    return smp_run_on_mask_ex(cpu_mask, fn, ctx, 0u, SMP_XCALL_F_ASYNC);
}

bool smp_run_on_cpu_async(u32 cpu_id, smp_call_fn_t fn, void *ctx) {
    if (cpu_id >= 32u) return false;
    return smp_run_on_mask_async(1u << cpu_id, fn, ctx) == 1u;
}

typedef struct smp_tlb_shootdown_ctx {
    uptr virt;
    usize length;
    uptr target_cr3;
    u64 generation;
    u32 pages;
    u32 full_flush;
    u32 target_mask;
    u32 expected_mask;
    volatile u32 acked_mask;
    volatile u32 flushed_mask;
    volatile u32 skipped_mask;
} smp_tlb_shootdown_ctx_t;

static void smp_local_flush_one_page(uptr virt) {
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

static void smp_local_flush_cr3(void) {
    uptr cr3 = vmm_read_cr3();
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

static bool smp_tlb_prepare_ctx(smp_tlb_shootdown_ctx_t *ctx, uptr virt, usize length, uptr target_cr3, u64 generation) {
    if (!ctx) return false;
    memset(ctx, 0, sizeof(*ctx));
    ctx->target_cr3 = target_cr3;
    ctx->generation = generation;
    if (length == 0) {
        ctx->full_flush = 1u;
        ctx->pages = 1u;
        return true;
    }
    uptr start = RABBITBONE_ALIGN_DOWN(virt, PAGE_SIZE);
    uptr end = 0;
    if (__builtin_add_overflow(virt, (uptr)length, &end)) return false;
    u64 end_aligned = 0;
    if (!rabbitbone_align_up_u64_checked((u64)end, PAGE_SIZE, &end_aligned) || end_aligned < start) return false;
    u64 bytes = end_aligned - start;
    u64 pages64 = bytes / PAGE_SIZE;
    if (pages64 == 0 || pages64 > 4096u) {
        ctx->full_flush = 1u;
        ctx->pages = 1u;
    } else {
        ctx->virt = start;
        ctx->length = (usize)bytes;
        ctx->pages = (u32)pages64;
    }
    return true;
}

static void smp_tlb_note_seen_generation(smp_tlb_shootdown_ctx_t *ctx) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (!pcpu || !ctx || ctx->generation == 0u) return;
    u64 old = __atomic_load_n(&pcpu->tlb_generation_seen, __ATOMIC_RELAXED);
    while (ctx->generation > old && !__atomic_compare_exchange_n(&pcpu->tlb_generation_seen, &old, ctx->generation, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

static void smp_tlb_shootdown_cb(u32 cpu_id, void *raw) {
    smp_tlb_shootdown_ctx_t *ctx = (smp_tlb_shootdown_ctx_t *)raw;
    if (!ctx || cpu_id >= 32u || !smp_cpu_id_valid(cpu_id)) return;
    u32 cpu_bit = 1u << cpu_id;
    uptr current_cr3 = vmm_read_cr3();
    smp_set_current_cr3(current_cr3);
    if (ctx->target_cr3 != 0 && current_cr3 != ctx->target_cr3) {
        __atomic_or_fetch(&ctx->skipped_mask, cpu_bit, __ATOMIC_ACQ_REL);
        __atomic_or_fetch(&ctx->acked_mask, cpu_bit, __ATOMIC_ACQ_REL);
        smp_tlb_note_seen_generation(ctx);
        __atomic_add_fetch(&smp_info.tlb_cpu_skips, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&smp_info.cpus[cpu_id].tlb_skips, 1u, __ATOMIC_RELAXED);
        __atomic_store_n(&smp_info.cpus[cpu_id].tlb_last_generation, ctx->generation, __ATOMIC_RELEASE);
        return;
    }
    if (ctx->full_flush) {
        smp_local_flush_cr3();
        smp_set_current_cr3(vmm_read_cr3());
    } else {
        uptr v = ctx->virt;
        for (u32 i = 0; i < ctx->pages; ++i, v += PAGE_SIZE) smp_local_flush_one_page(v);
    }
    smp_tlb_note_seen_generation(ctx);
    __atomic_or_fetch(&ctx->flushed_mask, cpu_bit, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&ctx->acked_mask, cpu_bit, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&smp_info.cpus[cpu_id].tlb_flushes, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&smp_info.cpus[cpu_id].tlb_last_generation, ctx->generation, __ATOMIC_RELEASE);
}

bool smp_tlb_shootdown_mask(uptr virt, usize length, uptr target_cr3, u32 cpu_mask, u64 generation, u64 timeout_ticks) {
    u64 start_ns = timer_now_ns();
    smp_tlb_shootdown_ctx_t ctx;
    if (!smp_tlb_prepare_ctx(&ctx, virt, length, target_cr3, generation)) {
        __atomic_add_fetch(&smp_info.tlb_shootdown_failures, 1u, __ATOMIC_RELAXED);
        return false;
    }

    u32 started_mask = smp_targetable_cpu_mask() & SMP_CPUMASK_VALID;
    u32 mask = (cpu_mask == SMP_CPU_MASK_ALL || cpu_mask == 0u) ? started_mask : (cpu_mask & started_mask);
    mask &= SMP_CPUMASK_VALID;
    ctx.target_mask = mask;
    ctx.expected_mask = mask;

    u32 started = 0;
    for (u32 i = 0; i < smp_info.cpu_count && i < 32u; ++i) if (mask & (1u << i)) ++started;
    u32 current = smp_current_cpu_id();
    u32 current_bit = current < 32u ? (1u << current) : 0u;
    u32 completed = 0;
    bool ok = false;
    if (started == 0u) {
        ok = true;
    } else if ((mask & ~current_bit) == 0u) {
        smp_tlb_shootdown_cb(current, &ctx);
        completed = 1u;
        ok = ((__atomic_load_n(&ctx.acked_mask, __ATOMIC_ACQUIRE) & mask) == mask);
    } else {
        completed = smp_run_on_mask_ex(mask, smp_tlb_shootdown_cb, &ctx, timeout_ticks ? timeout_ticks : SMP_XCALL_DEFAULT_TIMEOUT_TICKS, SMP_XCALL_F_ALLOW_IRQ_CONTEXT | SMP_XCALL_F_ALLOW_IRQS_DISABLED | SMP_XCALL_F_ALLOW_NESTED | SMP_XCALL_F_FORCE_UNICAST | SMP_XCALL_F_IPI_IMMEDIATE);
        u32 acked = __atomic_load_n(&ctx.acked_mask, __ATOMIC_ACQUIRE) & mask;
        ok = completed == started && acked == mask;
    }
    u32 flushed = __atomic_load_n(&ctx.flushed_mask, __ATOMIC_ACQUIRE) & mask;
    u32 skipped = __atomic_load_n(&ctx.skipped_mask, __ATOMIC_ACQUIRE) & mask;
    u32 acked = __atomic_load_n(&ctx.acked_mask, __ATOMIC_ACQUIRE) & mask;
    if (((flushed | skipped) & mask) != acked) ok = false;
    __atomic_add_fetch(&smp_info.tlb_shootdowns, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&smp_info.tlb_pages, ctx.full_flush ? 1u : ctx.pages, __ATOMIC_RELAXED);
    if (cpu_mask == SMP_CPU_MASK_ALL || cpu_mask == 0u) __atomic_add_fetch(&smp_info.tlb_broadcast_shootdowns, 1u, __ATOMIC_RELAXED);
    else __atomic_add_fetch(&smp_info.tlb_targeted_shootdowns, 1u, __ATOMIC_RELAXED);
    if (generation != 0u) __atomic_add_fetch(&smp_info.tlb_generation_updates, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&smp_info.tlb_last_generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_target_mask, mask, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_completed_mask, acked, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_skipped_mask, skipped, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_pages, ctx.full_flush ? 1u : ctx.pages, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_full_flush, ctx.full_flush, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_virt, ctx.virt, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_length, ctx.length, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.tlb_last_target_cr3, target_cr3, __ATOMIC_RELEASE);
    u64 elapsed = timer_now_ns() - start_ns;
    smp_update_max_u64(&smp_info.tlb_max_ns, elapsed);
    if (!ok) __atomic_add_fetch(&smp_info.tlb_shootdown_failures, 1u, __ATOMIC_RELAXED);
    return ok;
}

bool smp_tlb_shootdown(uptr virt, usize length, uptr target_cr3, u64 timeout_ticks) {
    return smp_tlb_shootdown_mask(virt, length, target_cr3, SMP_CPU_MASK_ALL, 0u, timeout_ticks);
}
