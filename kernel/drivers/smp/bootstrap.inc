static smp_boot_mailbox_t *smp_boot_mailbox_for(u32 logical_id) {
    return logical_id < SMP_MAX_CPUS ? &smp_boot_mailboxes[logical_id] : 0;
}

static void smp_boot_set_stage(u32 logical_id, u32 stage) {
    if (logical_id >= SMP_MAX_CPUS) return;
    smp_cpu_info_t *cpu = logical_id < smp_info.cpu_count ? &smp_info.cpus[logical_id] : 0;
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    if (cpu) __atomic_store_n(&cpu->boot_stage, stage, __ATOMIC_RELEASE);
    if (mb) __atomic_store_n(&mb->stage, stage, __ATOMIC_RELEASE);
}

static void smp_boot_set_error(u32 logical_id, u32 error) {
    if (logical_id >= SMP_MAX_CPUS || error == SMP_BOOT_ERROR_NONE) return;
    smp_cpu_info_t *cpu = logical_id < smp_info.cpu_count ? &smp_info.cpus[logical_id] : 0;
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    if (cpu) {
        __atomic_store_n(&cpu->boot_error, error, __ATOMIC_RELEASE);
        __atomic_add_fetch(&cpu->boot_failures, 1u, __ATOMIC_RELAXED);
    }
    if (mb) __atomic_store_n(&mb->error, error, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_info.boot_last_failed_cpu, logical_id, __ATOMIC_RELEASE);
}

static void smp_boot_mailbox_reset(u32 logical_id, u64 generation) {
    if (logical_id >= SMP_MAX_CPUS || logical_id >= smp_info.cpu_count) return;
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    smp_cpu_info_t *cpu = &smp_info.cpus[logical_id];
    memset((void *)mb, 0, sizeof(*mb));
    __atomic_store_n(&mb->generation, generation, __ATOMIC_RELEASE);
    __atomic_store_n(&mb->logical_id, logical_id, __ATOMIC_RELEASE);
    __atomic_store_n(&mb->apic_id, cpu->apic_id, __ATOMIC_RELEASE);
    __atomic_store_n(&mb->active, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&mb->error, SMP_BOOT_ERROR_NONE, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_generation_seen, (u32)generation, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_stage, SMP_BOOT_STAGE_MAILBOX_READY, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_error, SMP_BOOT_ERROR_NONE, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_sipi_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_wait_rounds, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&cpu->boot_tsc_delta, 0u, __ATOMIC_RELEASE);
    cpu->boot_cr3 = 0;
    cpu->boot_entry = 0;
    cpu->boot_trampoline_base = SMP_TRAMPOLINE_BASE;
    cpu->boot_trampoline_size = 0;
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_MAILBOX_READY);
}

static void smp_boot_delay(u32 pauses) {
    for (u32 i = 0; i < pauses; ++i) smp_cpu_pause();
}

static bool smp_trampoline_lock_exec(u32 logical_id) {
    bool ok = vmm_space_protect_4k(vmm_kernel_space(), (uptr)SMP_TRAMPOLINE_BASE, VMM_GLOBAL);
    if (ok) {
        smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_TRAMPOLINE_LOCKED);
        __atomic_add_fetch(&smp_info.boot_trampoline_lock_count, 1u, __ATOMIC_RELAXED);
    } else {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_EXEC);
    }
    return ok;
}

static bool smp_prepare_trampoline(u32 logical_id) {
    if (logical_id >= smp_info.cpu_count) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_BAD_CPU);
        return false;
    }
    usize size = (usize)(smp_trampoline_end - smp_trampoline_start);
    if (size == 0 || size > SMP_TRAMPOLINE_MAX_SIZE) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TRAMPOLINE_SIZE);
        return false;
    }
    uptr cr3 = vmm_read_cr3();
    if ((cr3 >> 32u) != 0u) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TRAMPOLINE_CR3_HIGH);
        return false;
    }
    if (!vmm_space_protect_4k(vmm_kernel_space(), (uptr)SMP_TRAMPOLINE_BASE, VMM_WRITE | VMM_GLOBAL | VMM_NX)) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_WRITE);
        return false;
    }
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_TRAMPOLINE_WRITABLE);
    u8 *dst = (u8 *)(uptr)SMP_TRAMPOLINE_BASE;
    memcpy(dst, smp_trampoline_start, size);
    memset(dst + size, 0, SMP_TRAMPOLINE_MAX_SIZE - size);
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_TRAMPOLINE_COPIED);

    u32 cr3_off = smp_trampoline_offset(smp_trampoline_cr3_offset);
    u32 entry_off = smp_trampoline_offset(smp_trampoline_entry64_offset);
    u32 stack_off = smp_trampoline_offset(smp_trampoline_stack_top_offset);
    u32 id_off = smp_trampoline_offset(smp_trampoline_logical_id_offset);
    if (cr3_off + sizeof(u64) > SMP_TRAMPOLINE_MAX_SIZE || entry_off + sizeof(u64) > SMP_TRAMPOLINE_MAX_SIZE ||
        stack_off + sizeof(u64) > SMP_TRAMPOLINE_MAX_SIZE || id_off + sizeof(u32) > SMP_TRAMPOLINE_MAX_SIZE) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TRAMPOLINE_OFFSET);
        (void)smp_trampoline_lock_exec(logical_id);
        return false;
    }

    smp_cpu_info_t *cpu = &smp_info.cpus[logical_id];
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    u64 entry64 = (u64)(uptr)smp_ap_entry;
    *(volatile u64 *)(uptr)(SMP_TRAMPOLINE_BASE + cr3_off) = (u64)cr3;
    *(volatile u64 *)(uptr)(SMP_TRAMPOLINE_BASE + entry_off) = entry64;
    *(volatile u64 *)(uptr)(SMP_TRAMPOLINE_BASE + stack_off) = cpu->stack_top;
    *(volatile u32 *)(uptr)(SMP_TRAMPOLINE_BASE + id_off) = logical_id;
    cpu->boot_cr3 = cr3;
    cpu->boot_entry = entry64;
    cpu->boot_trampoline_base = SMP_TRAMPOLINE_BASE;
    cpu->boot_trampoline_size = size;
    if (mb) {
        __atomic_store_n(&mb->cr3, cr3, __ATOMIC_RELEASE);
        __atomic_store_n(&mb->entry64, entry64, __ATOMIC_RELEASE);
        __atomic_store_n(&mb->stack_top, cpu->stack_top, __ATOMIC_RELEASE);
        __atomic_store_n(&mb->trampoline_base, SMP_TRAMPOLINE_BASE, __ATOMIC_RELEASE);
        __atomic_store_n(&mb->trampoline_size, size, __ATOMIC_RELEASE);
    }
    __asm__ volatile("mfence" ::: "memory");
    smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_TRAMPOLINE_PATCHED);
    __atomic_add_fetch(&smp_info.boot_prepare_count, 1u, __ATOMIC_RELAXED);
    return smp_trampoline_lock_exec(logical_id);
}

static bool smp_allocate_ap_stack(u32 logical_id) {
    if (logical_id >= smp_info.cpu_count) return false;
    smp_cpu_info_t *c = &smp_info.cpus[logical_id];
    if (c->stack_base && c->stack_top) {
        smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_STACK_ALLOCATED);
        return true;
    }
    void *stack = kmalloc(SMP_AP_STACK_SIZE);
    if (!stack) return false;
    memset(stack, 0, SMP_AP_STACK_SIZE);
    c->stack_base = (u64)(uptr)stack;
    c->stack_top = RABBITBONE_ALIGN_DOWN((u64)(uptr)stack + SMP_AP_STACK_SIZE, 16u);
    if (c->stack_top != 0u) smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_STACK_ALLOCATED);
    return c->stack_top != 0;
}

static bool smp_wait_for_cpu(u32 logical_id, u64 start_generation) {
    u64 start_ticks = smp_now_ticks();
    u64 start_tsc = cpu_read_tsc();
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    for (u32 round = 0; round < SMP_BOOT_WAIT_ROUNDS; ++round) {
        if (logical_id < smp_info.cpu_count) __atomic_store_n(&smp_info.cpus[logical_id].boot_wait_rounds, (u64)round, __ATOMIC_RELAXED);
        if (__atomic_load_n(&smp_info.cpus[logical_id].started, __ATOMIC_ACQUIRE)) return true;
        if (__atomic_load_n(&smp_boot_ack_generation, __ATOMIC_ACQUIRE) == start_generation &&
            __atomic_load_n(&smp_boot_ack_id, __ATOMIC_ACQUIRE) == logical_id) return true;
        if (mb && __atomic_load_n(&mb->acked, __ATOMIC_ACQUIRE) != 0u &&
            __atomic_load_n(&mb->generation, __ATOMIC_ACQUIRE) == start_generation) return true;
        u64 now_ticks = smp_now_ticks();
        if (now_ticks != start_ticks && now_ticks - start_ticks > SMP_XCALL_DEFAULT_TIMEOUT_TICKS) return false;
        if (now_ticks == start_ticks && round > (SMP_BOOT_WAIT_ROUNDS / 8u)) {
            u64 now_tsc = cpu_read_tsc();
            if (now_tsc > start_tsc) {
                u64 delta = now_tsc - start_tsc;
                if (logical_id < smp_info.cpu_count) __atomic_store_n(&smp_info.cpus[logical_id].boot_tsc_delta, delta, __ATOMIC_RELAXED);
                if (delta > SMP_BOOT_WAIT_TSC_MIN_DELTA) return false;
            }
        }
        for (u32 i = 0; i < SMP_BOOT_WAIT_PAUSES; ++i) smp_cpu_pause();
    }
    return __atomic_load_n(&smp_info.cpus[logical_id].started, __ATOMIC_ACQUIRE);
}

static bool smp_start_one(u32 logical_id) {
    if (logical_id >= smp_info.cpu_count) return false;
    smp_cpu_info_t *c = &smp_info.cpus[logical_id];
    if (!c->enabled) {
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_DISABLED_CPU);
        smp_cpu_set_state_locked(logical_id, SMP_CPU_FAILED);
        smp_recount_locked();
        return false;
    }
    if (c->bsp || c->started) return true;
    smp_cpu_set_state_locked(logical_id, SMP_CPU_BOOTING);
    smp_recount_locked();
    ++c->boot_attempts;
    u64 generation = __atomic_add_fetch(&smp_info.boot_generation, 1u, __ATOMIC_ACQ_REL);
    smp_boot_mailbox_reset(logical_id, generation);
    __atomic_store_n(&smp_boot_target_id, logical_id, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_id, 0xffffffffu, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_boot_ack_generation, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&smp_booting_cpu, true, __ATOMIC_RELEASE);
    if (!smp_allocate_ap_stack(logical_id)) {
        __atomic_store_n(&smp_booting_cpu, false, __ATOMIC_RELEASE);
        smp_cpu_set_state_locked(logical_id, SMP_CPU_FAILED);
        smp_recount_locked();
        smp_boot_set_error(logical_id, SMP_BOOT_ERROR_STACK_ALLOC);
        KLOG(LOG_ERROR, "smp", "cpu%u stack allocation failed", logical_id);
        return false;
    }
    if (!smp_prepare_trampoline(logical_id)) {
        __atomic_store_n(&smp_booting_cpu, false, __ATOMIC_RELEASE);
        smp_cpu_set_state_locked(logical_id, SMP_CPU_FAILED);
        smp_recount_locked();
        KLOG(LOG_ERROR, "smp", "cpu%u trampoline preparation failed stage=%s error=%s", logical_id, smp_boot_stage_name(c->boot_stage), smp_boot_error_name(c->boot_error));
        return false;
    }
    KLOG(LOG_INFO, "smp", "cpu%u apic=%u startup begin stack=%p gen=%llu", logical_id, c->apic_id, (void *)(uptr)c->stack_top, (unsigned long long)generation);

    bool sent_init = apic_send_init_ipi(c->apic_id);
    if (sent_init) smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_INIT_SENT);
    else smp_boot_set_error(logical_id, SMP_BOOT_ERROR_INIT_IPI);
    smp_boot_delay(SMP_BOOT_INIT_DELAY_PAUSES);

    bool sent_sipi1 = false;
    bool sent_sipi2 = false;
    if (sent_init) {
        sent_sipi1 = apic_send_startup_ipi(c->apic_id, (u8)(SMP_TRAMPOLINE_BASE >> 12u));
        if (sent_sipi1) {
            __atomic_add_fetch(&c->boot_sipi_count, 1u, __ATOMIC_RELAXED);
            smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
            if (mb) __atomic_add_fetch(&mb->sipi_count, 1u, __ATOMIC_RELAXED);
            smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_SIPI1_SENT);
        } else {
            smp_boot_set_error(logical_id, SMP_BOOT_ERROR_SIPI1);
        }
        smp_boot_delay(SMP_BOOT_SIPI_DELAY_PAUSES);
        sent_sipi2 = apic_send_startup_ipi(c->apic_id, (u8)(SMP_TRAMPOLINE_BASE >> 12u));
        if (sent_sipi2) {
            __atomic_add_fetch(&c->boot_sipi_count, 1u, __ATOMIC_RELAXED);
            smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
            if (mb) __atomic_add_fetch(&mb->sipi_count, 1u, __ATOMIC_RELAXED);
            smp_boot_set_stage(logical_id, SMP_BOOT_STAGE_SIPI2_SENT);
        } else {
            smp_boot_set_error(logical_id, SMP_BOOT_ERROR_SIPI2);
        }
    }

    bool acked = sent_init && sent_sipi1 && sent_sipi2 && smp_wait_for_cpu(logical_id, generation);
    __atomic_store_n(&smp_booting_cpu, false, __ATOMIC_RELEASE);
    smp_boot_mailbox_t *mb = smp_boot_mailbox_for(logical_id);
    if (mb) __atomic_store_n(&mb->active, 0u, __ATOMIC_RELEASE);
    bool ok = acked && __atomic_load_n(&c->started, __ATOMIC_ACQUIRE);
    if (!ok) {
        smp_cpu_set_state_locked(logical_id, SMP_CPU_FAILED);
        smp_recount_locked();
        if (c->boot_error == SMP_BOOT_ERROR_NONE) smp_boot_set_error(logical_id, SMP_BOOT_ERROR_TIMEOUT);
        if (!sent_init || !sent_sipi1 || !sent_sipi2) __atomic_add_fetch(&smp_info.boot_ipi_failures, 1u, __ATOMIC_RELAXED);
        else __atomic_add_fetch(&smp_info.boot_timeouts, 1u, __ATOMIC_RELAXED);
        KLOG(LOG_ERROR, "smp", "cpu%u apic=%u failed to start stage=%s error=%s sipi=%u wait_rounds=%llu", logical_id, c->apic_id, smp_boot_stage_name(c->boot_stage), smp_boot_error_name(c->boot_error), c->boot_sipi_count, (unsigned long long)c->boot_wait_rounds);
        return false;
    }
    KLOG(LOG_INFO, "smp", "cpu%u apic=%u online stack=%p stage=%s sipi=%u", logical_id, c->apic_id, (void *)(uptr)c->stack_top, smp_boot_stage_name(c->boot_stage), c->boot_sipi_count);
    return true;
}

static void smp_ipi_wake_handler(cpu_regs_t *regs) {
    (void)regs;
    u32 cpu_id = smp_current_cpu_id();
    smp_note_interrupt_enter((u32)SMP_IPI_WAKE_VECTOR, false);
    if (smp_cpu_id_valid(cpu_id)) {
        __atomic_add_fetch(&smp_info.cpus[cpu_id].ipi_wakeups, 1u, __ATOMIC_RELAXED);
        smp_record_ipi_event(cpu_id, (u32)SMP_IPI_WAKE_VECTOR, SMP_WAKE_REASON_XCALL);
        smp_note_cpu_wake_reason(cpu_id, SMP_WAKE_REASON_XCALL);
        u32 drained = smp_drain_ipi_immediate_queue(cpu_id, SMP_CALL_DRAIN_BATCH);
        if (drained == 0u && !smp_queue_pending(cpu_id)) {
            __atomic_add_fetch(&smp_info.cpus[cpu_id].empty_ipis, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_call_queues[cpu_id].empty_ipis, 1u, __ATOMIC_RELAXED);
        } else if (smp_queue_pending(cpu_id)) {
            smp_arm_active_poll(&smp_info.cpus[cpu_id]);
        }
    }
    apic_send_eoi();
    smp_note_interrupt_exit((u32)SMP_IPI_WAKE_VECTOR);
}

static void smp_ipi_reschedule_handler(cpu_regs_t *regs) {
    (void)regs;
    u32 cpu_id = smp_current_cpu_id();
    smp_note_interrupt_enter((u32)SMP_IPI_RESCHEDULE_VECTOR, false);
    if (smp_cpu_id_valid(cpu_id)) {
        __atomic_add_fetch(&smp_info.cpus[cpu_id].reschedule_ipi_wakeups, 1u, __ATOMIC_RELAXED);
        smp_record_ipi_event(cpu_id, (u32)SMP_IPI_RESCHEDULE_VECTOR, SMP_WAKE_REASON_RESCHEDULE);
        smp_note_cpu_wake_reason(cpu_id, SMP_WAKE_REASON_RESCHEDULE);
        scheduler_note_reschedule_ipi(cpu_id);
    }
    apic_send_eoi();
    smp_note_interrupt_exit((u32)SMP_IPI_RESCHEDULE_VECTOR);
}

void smp_start_all_aps(void) {
    if (!smp_info.initialized || smp_info.startup_attempted) return;
    smp_info.startup_attempted = true;
    idt_set_handler((u8)SMP_IPI_WAKE_VECTOR, smp_ipi_wake_handler);
    idt_set_handler((u8)SMP_IPI_RESCHEDULE_VECTOR, smp_ipi_reschedule_handler);
    const apic_info_t *ap = apic_get_info();
    if (!ap || !ap->lapic_present || (!ap->lapic_base && !ap->x2apic_enabled)) {
        KLOG(LOG_WARN, "smp", "local APIC unavailable; staying on BSP");
        return;
    }
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if (smp_info.cpus[i].enabled && !smp_info.cpus[i].bsp) (void)smp_start_one(i);
    }
    u64 flags = spin_lock_irqsave(&smp_lock);
    smp_recount_locked();
    spin_unlock_irqrestore(&smp_lock, flags);
    KLOG(LOG_INFO, "smp", "startup complete started=%u enabled=%u failed=%u boot_prepare=%llu timeouts=%llu ipi_fail=%llu", smp_info.started_cpu_count, smp_info.enabled_cpu_count, smp_info.failed_cpu_count, (unsigned long long)smp_info.boot_prepare_count, (unsigned long long)smp_info.boot_timeouts, (unsigned long long)smp_info.boot_ipi_failures);
}
