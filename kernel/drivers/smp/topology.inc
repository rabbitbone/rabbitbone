const char *smp_cpu_state_name(u32 state) {
    switch (state) {
        case SMP_CPU_POSSIBLE: return "possible";
        case SMP_CPU_PRESENT: return "present";
        case SMP_CPU_OFFLINE: return "offline";
        case SMP_CPU_BOOTING: return "booting";
        case SMP_CPU_ONLINE: return "online";
        case SMP_CPU_ACTIVE: return "active";
        case SMP_CPU_DYING: return "dying";
        case SMP_CPU_DEAD: return "dead";
        case SMP_CPU_FAILED: return "failed";
        default: return "unknown";
    }
}

static bool smp_cpu_state_onlineish(u32 state) {
    return state == SMP_CPU_ONLINE || state == SMP_CPU_ACTIVE;
}

static bool smp_cpu_state_targetable(u32 state) {
    return state == SMP_CPU_ACTIVE;
}

static bool smp_cpu_state_terminal(u32 state) {
    return state == SMP_CPU_DEAD || state == SMP_CPU_FAILED;
}

static void smp_cpu_set_state_locked(u32 cpu_id, u32 state) {
    if (cpu_id >= smp_info.cpu_count || cpu_id >= SMP_MAX_CPUS) return;
    smp_cpu_info_t *cpu = &smp_info.cpus[cpu_id];
    u32 old = cpu->state;
    if (old == state) return;
    if (smp_cpu_state_terminal(old) && state != old) {
        __atomic_add_fetch(&smp_info.lifecycle_invalid_transitions, 1u, __ATOMIC_RELAXED);
        return;
    }
    cpu->state = state;
    __atomic_add_fetch(&smp_info.lifecycle_transitions, 1u, __ATOMIC_RELAXED);
}

const char *smp_boot_stage_name(u32 stage) {
    switch (stage) {
        case SMP_BOOT_STAGE_NONE: return "none";
        case SMP_BOOT_STAGE_STACK_ALLOCATED: return "stack_allocated";
        case SMP_BOOT_STAGE_MAILBOX_READY: return "mailbox_ready";
        case SMP_BOOT_STAGE_TRAMPOLINE_WRITABLE: return "trampoline_writable";
        case SMP_BOOT_STAGE_TRAMPOLINE_COPIED: return "trampoline_copied";
        case SMP_BOOT_STAGE_TRAMPOLINE_PATCHED: return "trampoline_patched";
        case SMP_BOOT_STAGE_TRAMPOLINE_LOCKED: return "trampoline_locked";
        case SMP_BOOT_STAGE_INIT_SENT: return "init_sent";
        case SMP_BOOT_STAGE_SIPI1_SENT: return "sipi1_sent";
        case SMP_BOOT_STAGE_SIPI2_SENT: return "sipi2_sent";
        case SMP_BOOT_STAGE_ENTERED_C: return "entered_c";
        case SMP_BOOT_STAGE_CPU_INIT: return "cpu_init";
        case SMP_BOOT_STAGE_GDT_READY: return "gdt_ready";
        case SMP_BOOT_STAGE_STACKS_READY: return "stacks_ready";
        case SMP_BOOT_STAGE_PERCPU_READY: return "percpu_ready";
        case SMP_BOOT_STAGE_APIC_READY: return "apic_ready";
        case SMP_BOOT_STAGE_TIMER_READY: return "timer_ready";
        case SMP_BOOT_STAGE_ONLINE: return "online";
        default: return "unknown";
    }
}

const char *smp_boot_error_name(u32 error) {
    switch (error) {
        case SMP_BOOT_ERROR_NONE: return "none";
        case SMP_BOOT_ERROR_BAD_CPU: return "bad_cpu";
        case SMP_BOOT_ERROR_DISABLED_CPU: return "disabled_cpu";
        case SMP_BOOT_ERROR_STACK_ALLOC: return "stack_alloc";
        case SMP_BOOT_ERROR_TRAMPOLINE_SIZE: return "trampoline_size";
        case SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_WRITE: return "trampoline_protect_write";
        case SMP_BOOT_ERROR_TRAMPOLINE_OFFSET: return "trampoline_offset";
        case SMP_BOOT_ERROR_TRAMPOLINE_CR3_HIGH: return "trampoline_cr3_high";
        case SMP_BOOT_ERROR_TRAMPOLINE_PROTECT_EXEC: return "trampoline_protect_exec";
        case SMP_BOOT_ERROR_INIT_IPI: return "init_ipi";
        case SMP_BOOT_ERROR_SIPI1: return "sipi1";
        case SMP_BOOT_ERROR_SIPI2: return "sipi2";
        case SMP_BOOT_ERROR_TIMEOUT: return "timeout";
        case SMP_BOOT_ERROR_DYNAMIC_STACKS: return "dynamic_stacks";
        case SMP_BOOT_ERROR_APIC_LOCAL_INIT: return "apic_local_init";
        default: return "unknown";
    }
}

static smp_cpumask_t smp_mask_for_cpu(u32 cpu_id) {
    return cpu_id < SMP_MAX_CPUS ? (smp_cpumask_t)(1u << cpu_id) : SMP_CPUMASK_NONE;
}

bool smp_cpumask_cpu_valid(u32 cpu_id) {
    return cpu_id < SMP_MAX_CPUS;
}

bool smp_cpumask_test_cpu(smp_cpumask_t mask, u32 cpu_id) {
    return cpu_id < SMP_MAX_CPUS && (mask & smp_mask_for_cpu(cpu_id)) != 0u;
}

u32 smp_cpumask_weight(smp_cpumask_t mask) {
    mask &= SMP_CPUMASK_VALID;
    u32 n = 0;
    while (mask) {
        mask &= mask - 1u;
        ++n;
    }
    return n;
}

static smp_cpumask_t smp_dense_mask_for_count(u32 count) {
    if (count >= SMP_MAX_CPUS) return SMP_CPUMASK_VALID;
    return count == 0u ? SMP_CPUMASK_NONE : (smp_cpumask_t)((1u << count) - 1u);
}

static u32 smp_current_id_from_apic(void) {
    u32 apic_id = apic_current_id();
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        if (smp_info.cpus[i].enabled && smp_info.cpus[i].apic_id == apic_id) return i;
    }
    return 0;
}

static smp_percpu_state_t *smp_percpu_from_base(uptr base) {
    uptr start = (uptr)&smp_info.percpu[0];
    uptr end = (uptr)&smp_info.percpu[SMP_MAX_CPUS];
    if (base < start || base >= end) return 0;
    uptr off = base - start;
    if ((off % (uptr)sizeof(smp_percpu_state_t)) != 0u) return 0;
    smp_percpu_state_t *pcpu = (smp_percpu_state_t *)base;
    if (pcpu->self != pcpu) return 0;
    u32 id = __atomic_load_n(&pcpu->logical_id, __ATOMIC_RELAXED);
    if (id >= smp_info.cpu_count || pcpu != &smp_info.percpu[id]) return 0;
    if (__atomic_load_n(&pcpu->installed, __ATOMIC_ACQUIRE) == 0u) return 0;
    return pcpu;
}

static smp_percpu_state_t *smp_current_percpu_fast(void) {
    if (!__atomic_load_n(&smp_percpu_system_ready, __ATOMIC_ACQUIRE)) return 0;
    return smp_percpu_from_base((uptr)cpu_read_msr(SMP_MSR_GS_BASE));
}

static smp_percpu_state_t *smp_current_percpu_fallback(void) {
    if (!smp_info.initialized) return 0;
    u32 id = smp_current_id_from_apic();
    if (id >= smp_info.cpu_count) id = 0;
    if (id >= SMP_MAX_CPUS) return 0;
    smp_percpu_state_t *pcpu = &smp_info.percpu[id];
    __atomic_add_fetch(&pcpu->apic_fallbacks, 1u, __ATOMIC_RELAXED);
    if (__atomic_load_n(&pcpu->installed, __ATOMIC_ACQUIRE) != 0u) cpu_write_msr(SMP_MSR_GS_BASE, (u64)(uptr)pcpu);
    return pcpu;
}

static void smp_percpu_install_cpu(u32 cpu_id) {
    if (cpu_id >= SMP_MAX_CPUS) return;
    smp_percpu_state_t *pcpu = &smp_info.percpu[cpu_id];
    memset(pcpu, 0, sizeof(*pcpu));
    pcpu->self = pcpu;
    pcpu->logical_id = cpu_id;
    pcpu->apic_id = cpu_id < smp_info.cpu_count ? smp_info.cpus[cpu_id].apic_id : 0u;
    pcpu->generation = __atomic_add_fetch(&smp_percpu_generation, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&pcpu->installed, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&pcpu->online, 1u, __ATOMIC_RELEASE);
    pcpu->current_address_space = vmm_kernel_space();
    pcpu->current_cr3 = vmm_kernel_space() ? vmm_kernel_space()->pml4_physical : 0u;
    pcpu->tlb_generation_seen = vmm_kernel_space() ? vmm_kernel_space()->tlb_generation : 0u;
    cpu_write_msr(SMP_MSR_GS_BASE, (u64)(uptr)pcpu);
    __atomic_store_n(&smp_percpu_system_ready, true, __ATOMIC_RELEASE);
}

smp_percpu_state_t *smp_current_percpu(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu_fast();
    if (pcpu) return pcpu;
    return smp_current_percpu_fallback();
}

bool smp_percpu_ready(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    return pcpu && __atomic_load_n(&pcpu->installed, __ATOMIC_ACQUIRE) != 0u;
}

void smp_set_current_process(void *process) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (pcpu) __atomic_store_n(&pcpu->current_process, process, __ATOMIC_RELEASE);
}

void *smp_get_current_process(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    return pcpu ? __atomic_load_n(&pcpu->current_process, __ATOMIC_ACQUIRE) : 0;
}

void smp_set_current_address_space(void *space) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (pcpu) __atomic_store_n(&pcpu->current_address_space, space, __ATOMIC_RELEASE);
}

void *smp_get_current_address_space(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    return pcpu ? __atomic_load_n(&pcpu->current_address_space, __ATOMIC_ACQUIRE) : 0;
}

void smp_set_current_cr3(uptr cr3) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (pcpu) __atomic_store_n(&pcpu->current_cr3, cr3, __ATOMIC_RELEASE);
}

uptr smp_get_current_cr3(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu();
    return pcpu ? __atomic_load_n(&pcpu->current_cr3, __ATOMIC_ACQUIRE) : 0u;
}

static bool smp_local_cpu_is_bsp(void) {
    return (cpu_read_msr(0x1bull) & (1ull << 8)) != 0;
}

u32 smp_current_cpu_id(void) {
    smp_percpu_state_t *pcpu = smp_current_percpu_fast();
    if (pcpu) {
        __atomic_add_fetch(&pcpu->fast_id_hits, 1u, __ATOMIC_RELAXED);
        return pcpu->logical_id;
    }
    if (__atomic_load_n(&smp_booting_cpu, __ATOMIC_ACQUIRE)) {
        if (smp_info.initialized && smp_local_cpu_is_bsp()) return smp_current_id_from_apic();
        u32 target = __atomic_load_n(&smp_boot_target_id, __ATOMIC_ACQUIRE);
        if (target < SMP_MAX_CPUS) return target;
    }
    pcpu = smp_current_percpu_fallback();
    return pcpu ? pcpu->logical_id : 0;
}

u32 smp_started_cpu_count(void) {
    return __atomic_load_n(&smp_info.started_cpu_count, __ATOMIC_ACQUIRE);
}

u32 smp_cpu_state(u32 cpu_id) {
    if (cpu_id >= smp_info.cpu_count || cpu_id >= SMP_MAX_CPUS) return SMP_CPU_DEAD;
    return __atomic_load_n(&smp_info.cpus[cpu_id].state, __ATOMIC_ACQUIRE);
}

bool smp_cpu_is_online(u32 cpu_id) {
    return cpu_id < smp_info.cpu_count && smp_cpu_state_onlineish(smp_cpu_state(cpu_id));
}

bool smp_cpu_is_active(u32 cpu_id) {
    return cpu_id < smp_info.cpu_count && smp_cpu_state(cpu_id) == SMP_CPU_ACTIVE;
}

bool smp_cpu_is_targetable(u32 cpu_id) {
    return cpu_id < smp_info.cpu_count && cpu_id < SMP_MAX_CPUS &&
           smp_cpu_state_targetable(smp_cpu_state(cpu_id)) &&
           __atomic_load_n(&smp_info.cpus[cpu_id].started, __ATOMIC_ACQUIRE) != 0u &&
           __atomic_load_n(&smp_info.percpu[cpu_id].online, __ATOMIC_ACQUIRE) != 0u;
}

smp_cpumask_t smp_possible_cpu_mask(void) {
    return __atomic_load_n(&smp_info.possible_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_present_cpu_mask(void) {
    return __atomic_load_n(&smp_info.present_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_enabled_cpu_mask(void) {
    return __atomic_load_n(&smp_info.enabled_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_online_cpu_mask(void) {
    return __atomic_load_n(&smp_info.online_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_active_cpu_mask(void) {
    return __atomic_load_n(&smp_info.active_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_targetable_cpu_mask(void) {
    return __atomic_load_n(&smp_info.targetable_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_offline_cpu_mask(void) {
    return __atomic_load_n(&smp_info.offline_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_booting_cpu_mask(void) {
    return __atomic_load_n(&smp_info.booting_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_dying_cpu_mask(void) {
    return __atomic_load_n(&smp_info.dying_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_dead_cpu_mask(void) {
    return __atomic_load_n(&smp_info.dead_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_failed_cpu_mask(void) {
    return __atomic_load_n(&smp_info.failed_cpu_mask, __ATOMIC_ACQUIRE) & SMP_CPUMASK_VALID;
}

smp_cpumask_t smp_started_cpu_mask(void) {
    return smp_targetable_cpu_mask();
}

static bool smp_cpu_id_valid(u32 cpu_id) {
    return cpu_id < smp_info.cpu_count && smp_info.cpus[cpu_id].enabled;
}

static bool smp_cpu_targetable(u32 cpu_id) {
    return smp_cpu_id_valid(cpu_id) && smp_cpu_state_targetable(__atomic_load_n(&smp_info.cpus[cpu_id].state, __ATOMIC_ACQUIRE)) &&
           __atomic_load_n(&smp_info.cpus[cpu_id].started, __ATOMIC_ACQUIRE) != 0u &&
           __atomic_load_n(&smp_info.percpu[cpu_id].online, __ATOMIC_ACQUIRE) != 0u;
}

static void smp_note_anomaly(u32 cpu_id, const char *msg) {
    if (cpu_id < SMP_MAX_CPUS) __atomic_add_fetch(&smp_info.cpus[cpu_id].anomalies, 1u, __ATOMIC_RELAXED);
    __atomic_add_fetch(&smp_info.monitor_anomalies, 1u, __ATOMIC_RELAXED);
    KLOG(LOG_ERROR, "smp", "anomaly cpu%u: %s", cpu_id, msg ? msg : "unknown");
}


static void smp_record_cpu_arch_state(u32 cpu_id) {
    if (cpu_id >= SMP_MAX_CPUS) return;
    cpu_arch_state_t state;
    cpu_capture_arch_state(&state);
    u64 flags = spin_lock_irqsave(&smp_lock);
    if (cpu_id < smp_info.cpu_count) {
        smp_info.cpus[cpu_id].arch_state = state;
        if (state.initialized) __atomic_add_fetch(&smp_info.arch_init_cpus, 1u, __ATOMIC_RELAXED);
        else {
            __atomic_add_fetch(&smp_info.arch_init_failures, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&smp_info.cpus[cpu_id].anomalies, 1u, __ATOMIC_RELAXED);
        }
        if (state.xsave_ready) __atomic_add_fetch(&smp_info.arch_xsave_cpus, 1u, __ATOMIC_RELAXED);
        if (state.invariant_tsc_ready) __atomic_add_fetch(&smp_info.arch_invariant_tsc_cpus, 1u, __ATOMIC_RELAXED);
    }
    spin_unlock_irqrestore(&smp_lock, flags);
}

static void smp_mark_bsp_started(void) {
    u32 id = smp_current_id_from_apic();
    if (id >= smp_info.cpu_count) id = 0;
    smp_cpu_info_t *c = &smp_info.cpus[id];
    c->bsp = true;
    c->started = true;
    smp_cpu_set_state_locked(id, SMP_CPU_ACTIVE);
    c->boot_stage = SMP_BOOT_STAGE_ONLINE;
    c->boot_error = SMP_BOOT_ERROR_NONE;
    c->boot_generation_seen = (u32)smp_info.boot_generation;
    c->boot_cr3 = vmm_read_cr3();
    c->boot_entry = (u64)(uptr)smp_ap_entry;
    c->boot_trampoline_base = SMP_TRAMPOLINE_BASE;
    c->boot_trampoline_size = 0u;
    c->heartbeat = 1;
    smp_info.started_cpu_count = 1;
    smp_info.bsp_apic_id = c->apic_id;
    smp_recount_locked();
}

static void smp_recount_locked(void) {
    u32 enabled = 0;
    u32 started = 0;
    u32 failed = 0;
    smp_cpumask_t possible = smp_dense_mask_for_count(smp_info.cpu_count);
    smp_cpumask_t present = 0;
    smp_cpumask_t enabled_mask = 0;
    smp_cpumask_t online_mask = 0;
    smp_cpumask_t active_mask = 0;
    smp_cpumask_t targetable_mask = 0;
    smp_cpumask_t offline_mask = 0;
    smp_cpumask_t booting_mask = 0;
    smp_cpumask_t dying_mask = 0;
    smp_cpumask_t dead_mask = 0;
    smp_cpumask_t failed_mask = 0;
    smp_cpumask_t bsp_mask = 0;
    for (u32 i = 0; i < smp_info.cpu_count && i < SMP_MAX_CPUS; ++i) {
        smp_cpu_info_t *cpu = &smp_info.cpus[i];
        smp_cpumask_t bit = smp_mask_for_cpu(i);
        u32 state = __atomic_load_n(&cpu->state, __ATOMIC_ACQUIRE);
        present |= bit;
        if (cpu->bsp) bsp_mask |= bit;
        if (cpu->enabled) {
            ++enabled;
            enabled_mask |= bit;
        }
        if (state == SMP_CPU_BOOTING) booting_mask |= bit;
        if (state == SMP_CPU_OFFLINE) offline_mask |= bit;
        if (state == SMP_CPU_DYING) dying_mask |= bit;
        if (state == SMP_CPU_DEAD) dead_mask |= bit;
        if (state == SMP_CPU_FAILED) {
            ++failed;
            failed_mask |= bit;
        }
        if (smp_cpu_state_onlineish(state) && cpu->started) {
            ++started;
            online_mask |= bit;
        }
        if (state == SMP_CPU_ACTIVE && cpu->started && __atomic_load_n(&smp_info.percpu[i].online, __ATOMIC_ACQUIRE) != 0u) {
            active_mask |= bit;
            targetable_mask |= bit;
        }
    }
    smp_info.enabled_cpu_count = enabled;
    smp_info.started_cpu_count = started;
    smp_info.failed_cpu_count = failed;
    smp_info.possible_cpu_mask = possible;
    smp_info.present_cpu_mask = present;
    smp_info.enabled_cpu_mask = enabled_mask;
    smp_info.online_cpu_mask = online_mask;
    smp_info.active_cpu_mask = active_mask;
    smp_info.targetable_cpu_mask = targetable_mask;
    smp_info.offline_cpu_mask = offline_mask;
    smp_info.booting_cpu_mask = booting_mask;
    smp_info.dying_cpu_mask = dying_mask;
    smp_info.dead_cpu_mask = dead_mask;
    smp_info.failed_cpu_mask = failed_mask;
    smp_info.bsp_cpu_mask = bsp_mask;
    smp_info.bootstrap_only = started <= 1u;
}
static bool smp_add_fallback_cpu(void) {
    smp_cpu_info_t *sc = &smp_info.cpus[0];
    sc->logical_id = 0;
    sc->acpi_id = 0;
    sc->apic_id = smp_info.bsp_apic_id;
    sc->package_id = 0;
    sc->core_id = 0;
    sc->thread_id = 0;
    sc->enabled = true;
    sc->bsp = true;
    sc->started = true;
    sc->state = SMP_CPU_ACTIVE;
    sc->boot_stage = SMP_BOOT_STAGE_ONLINE;
    sc->boot_error = SMP_BOOT_ERROR_NONE;
    smp_info.cpu_count = 1;
    smp_info.enabled_cpu_count = 1;
    smp_info.started_cpu_count = 1;
    return true;
}

void smp_init_groundwork(void) {
    u64 flags = spin_lock_irqsave(&smp_lock);
    memset(&smp_info, 0, sizeof(smp_info));
    smp_call_queue_init();
    memset((void *)smp_monitor_last_heartbeat, 0, sizeof(smp_monitor_last_heartbeat));
    memset((void *)smp_monitor_last_report_ticks, 0, sizeof(smp_monitor_last_report_ticks));
    memset((void *)smp_boot_mailboxes, 0, sizeof(smp_boot_mailboxes));
    __atomic_store_n(&smp_percpu_system_ready, false, __ATOMIC_RELEASE);
    smp_info.initialized = true;
    smp_info.bootstrap_only = true;
    smp_info.background_service = true;
    smp_info.ap_kernel_tasks_enabled = false;
    smp_info.idle_policy_version = 1;
    smp_info.boot_generation = 1;
    __atomic_store_n(&smp_percpu_generation, 1u, __ATOMIC_RELEASE);
    const acpi_info_t *ai = acpi_get_info();
    const apic_info_t *ap = apic_get_info();
    smp_info.bsp_apic_id = ap ? ap->bsp_apic_id : 0;
    if (ai && ai->madt_found && ai->cpu_count) {
        for (u32 i = 0; i < ai->cpu_count && smp_info.cpu_count < SMP_MAX_CPUS; ++i) {
            const acpi_cpu_info_t *ac = &ai->cpus[i];
            smp_cpu_info_t *sc = &smp_info.cpus[smp_info.cpu_count];
            memset(sc, 0, sizeof(*sc));
            sc->logical_id = smp_info.cpu_count;
            sc->acpi_id = ac->acpi_id;
            sc->apic_id = ac->apic_id;
            sc->package_id = 0;
            sc->core_id = smp_info.cpu_count;
            sc->thread_id = 0;
            sc->enabled = ac->enabled;
            sc->bsp = ac->apic_id == smp_info.bsp_apic_id;
            sc->started = sc->bsp;
            sc->state = sc->bsp ? SMP_CPU_ACTIVE : (sc->enabled ? SMP_CPU_OFFLINE : SMP_CPU_FAILED);
            sc->boot_stage = sc->bsp ? SMP_BOOT_STAGE_ONLINE : SMP_BOOT_STAGE_NONE;
            sc->boot_error = SMP_BOOT_ERROR_NONE;
            ++smp_info.cpu_count;
        }
    } else {
        (void)smp_add_fallback_cpu();
    }
    bool found_bsp = false;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) if (smp_info.cpus[i].bsp) found_bsp = true;
    if (!found_bsp && smp_info.cpu_count > 0) {
        smp_info.cpus[0].bsp = true;
        smp_info.cpus[0].started = true;
        smp_cpu_set_state_locked(0, SMP_CPU_ACTIVE);
        smp_info.cpus[0].boot_stage = SMP_BOOT_STAGE_ONLINE;
        smp_info.cpus[0].boot_error = SMP_BOOT_ERROR_NONE;
        smp_info.bsp_apic_id = smp_info.cpus[0].apic_id;
    }
    smp_recount_locked();
    spin_unlock_irqrestore(&smp_lock, flags);
    smp_mark_bsp_started();
    smp_percpu_install_cpu(smp_current_id_from_apic());
    smp_record_cpu_arch_state(smp_current_id_from_apic());
    u64 recount_flags = spin_lock_irqsave(&smp_lock);
    smp_recount_locked();
    spin_unlock_irqrestore(&smp_lock, recount_flags);
    KLOG(LOG_INFO, "smp", "groundwork cpus=%u enabled=%u possible=0x%x present=0x%x online=0x%x active=0x%x bsp_apic_id=%u percpu=%p", smp_info.cpu_count, smp_info.enabled_cpu_count, smp_info.possible_cpu_mask, smp_info.present_cpu_mask, smp_info.online_cpu_mask, smp_info.active_cpu_mask, smp_info.bsp_apic_id, (void *)smp_current_percpu());
}
