typedef struct smp_ktest_seen_ctx {
    volatile u32 seen_mask;
    volatile u64 calls;
    volatile u32 current_id_mismatch;
} smp_ktest_seen_ctx_t;

static void smp_ktest_seen_call(u32 cpu_id, void *ctx) {
    smp_ktest_seen_ctx_t *c = (smp_ktest_seen_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    if (smp_current_cpu_id() != cpu_id) __atomic_add_fetch(&c->current_id_mismatch, 1u, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->calls, 1u, __ATOMIC_ACQ_REL);
}


typedef struct smp_ktest_percpu_ctx {
    volatile u32 seen_mask;
    volatile u32 mismatch;
    volatile u32 null_current;
    volatile u32 process_active;
    void *percpu_ptr[SMP_MAX_CPUS];
    void *process_ptr[SMP_MAX_CPUS];
    void *space_ptr[SMP_MAX_CPUS];
} smp_ktest_percpu_ctx_t;

static void smp_ktest_percpu_call(u32 cpu_id, void *ctx) {
    smp_ktest_percpu_ctx_t *c = (smp_ktest_percpu_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    smp_percpu_state_t *pcpu = smp_current_percpu();
    if (!pcpu || !smp_percpu_ready()) {
        __atomic_add_fetch(&c->null_current, 1u, __ATOMIC_ACQ_REL);
        return;
    }
    if (smp_current_cpu_id() != cpu_id || pcpu->logical_id != cpu_id || pcpu->self != pcpu || pcpu->installed == 0u || pcpu->online == 0u) {
        __atomic_add_fetch(&c->mismatch, 1u, __ATOMIC_ACQ_REL);
    }
    if (pcpu->current_address_space != vmm_current_space()) __atomic_add_fetch(&c->mismatch, 1u, __ATOMIC_ACQ_REL);
    if (process_current_pid() != 0u || process_user_active()) __atomic_add_fetch(&c->process_active, 1u, __ATOMIC_ACQ_REL);
    c->percpu_ptr[cpu_id] = pcpu;
    c->process_ptr[cpu_id] = smp_get_current_process();
    c->space_ptr[cpu_id] = smp_get_current_address_space();
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
}

static bool smp_ktest_percpu_unique_started(const smp_info_t *info, const smp_ktest_percpu_ctx_t *ctx) {
    if (!info || !ctx) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        if (ctx->percpu_ptr[i] != (const void *)&info->percpu[i]) return false;
        if (!ctx->process_ptr[i] || !ctx->space_ptr[i]) return false;
        for (u32 j = i + 1u; j < info->cpu_count; ++j) {
            if (!info->cpus[j].started) continue;
            if (ctx->percpu_ptr[i] == ctx->percpu_ptr[j]) return false;
            if (ctx->process_ptr[i] == ctx->process_ptr[j]) return false;
        }
    }
    return true;
}

typedef struct smp_ktest_race_ctx {
    spinlock_t lock;
    volatile u32 inside;
    volatile u32 violations;
    volatile u64 counter;
    u32 iterations;
} smp_ktest_race_ctx_t;

static void smp_ktest_spinlock_race_call(u32 cpu_id, void *ctx) {
    (void)cpu_id;
    smp_ktest_race_ctx_t *c = (smp_ktest_race_ctx_t *)ctx;
    if (!c) return;
    for (u32 i = 0; i < c->iterations; ++i) {
        u64 flags = spin_lock_irqsave(&c->lock);
        if (__atomic_load_n(&c->inside, __ATOMIC_RELAXED) != 0) __atomic_add_fetch(&c->violations, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&c->inside, 1u, __ATOMIC_RELAXED);
        __atomic_add_fetch(&c->counter, 1u, __ATOMIC_RELAXED);
        if (__atomic_load_n(&c->inside, __ATOMIC_RELAXED) != 1u) __atomic_add_fetch(&c->violations, 1u, __ATOMIC_RELAXED);
        __atomic_sub_fetch(&c->inside, 1u, __ATOMIC_RELAXED);
        spin_unlock_irqrestore(&c->lock, flags);
    }
}

typedef struct smp_ktest_atomic_ctx {
    volatile u64 counter;
    volatile u32 seen_mask;
    u32 iterations;
} smp_ktest_atomic_ctx_t;

static void smp_ktest_atomic_call(u32 cpu_id, void *ctx) {
    smp_ktest_atomic_ctx_t *c = (smp_ktest_atomic_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    for (u32 i = 0; i < c->iterations; ++i) __atomic_add_fetch(&c->counter, 1u, __ATOMIC_ACQ_REL);
}

typedef struct smp_ktest_latency_ctx {
    volatile u32 seen_mask;
    volatile u64 calls;
    volatile u64 per_cpu_calls[SMP_MAX_CPUS];
} smp_ktest_latency_ctx_t;

static void smp_ktest_latency_call(u32 cpu_id, void *ctx) {
    smp_ktest_latency_ctx_t *c = (smp_ktest_latency_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->per_cpu_calls[cpu_id], 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->calls, 1u, __ATOMIC_ACQ_REL);
}

typedef struct smp_ktest_async_ctx {
    volatile u32 seen_mask;
    volatile u32 current_id_mismatch;
    volatile u64 calls;
    volatile u64 per_cpu_calls[SMP_MAX_CPUS];
} smp_ktest_async_ctx_t;

static void smp_ktest_async_call(u32 cpu_id, void *ctx) {
    smp_ktest_async_ctx_t *c = (smp_ktest_async_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    if (smp_current_cpu_id() != cpu_id) __atomic_add_fetch(&c->current_id_mismatch, 1u, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->per_cpu_calls[cpu_id], 1u, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->calls, 1u, __ATOMIC_ACQ_REL);
}

static bool smp_ktest_wait_async_calls(const smp_ktest_async_ctx_t *ctx, u64 expected, u64 timeout_ns) {
    if (!ctx) return false;
    u64 start = timer_now_ns();
    while (timer_now_ns() - start < timeout_ns) {
        if (__atomic_load_n(&ctx->calls, __ATOMIC_ACQUIRE) >= expected) return true;
        for (u32 i = 0; i < 256u; ++i) __asm__ volatile("pause");
    }
    return __atomic_load_n(&ctx->calls, __ATOMIC_ACQUIRE) >= expected;
}

typedef struct smp_ktest_mask_ctx {
    volatile u32 seen_mask;
    volatile u64 calls;
    volatile u32 unexpected;
    volatile u32 current_id_mismatch;
    u32 expected_mask;
} smp_ktest_mask_ctx_t;

static void smp_ktest_mask_call(u32 cpu_id, void *ctx) {
    smp_ktest_mask_ctx_t *c = (smp_ktest_mask_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    if ((c->expected_mask & (1u << cpu_id)) == 0) __atomic_add_fetch(&c->unexpected, 1u, __ATOMIC_ACQ_REL);
    if (smp_current_cpu_id() != cpu_id) __atomic_add_fetch(&c->current_id_mismatch, 1u, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->calls, 1u, __ATOMIC_ACQ_REL);
}

typedef struct smp_ktest_nested_ctx {
    u32 caller_cpu;
    volatile u32 outer_cpu;
    volatile u32 rejected;
    volatile u32 allowed;
    smp_ktest_mask_ctx_t inner;
} smp_ktest_nested_ctx_t;

static void smp_ktest_nested_reject_call(u32 cpu_id, void *raw) {
    smp_ktest_nested_ctx_t *ctx = (smp_ktest_nested_ctx_t *)raw;
    if (!ctx) return;
    ctx->outer_cpu = cpu_id;
    ctx->inner.expected_mask = 1u << ctx->caller_cpu;
    bool ok = smp_run_on_cpu(ctx->caller_cpu, smp_ktest_mask_call, &ctx->inner, 10u);
    if (!ok) __atomic_store_n(&ctx->rejected, 1u, __ATOMIC_RELEASE);
}

static void smp_ktest_nested_allow_call(u32 cpu_id, void *raw) {
    smp_ktest_nested_ctx_t *ctx = (smp_ktest_nested_ctx_t *)raw;
    if (!ctx) return;
    ctx->outer_cpu = cpu_id;
    ctx->inner.expected_mask = 1u << ctx->caller_cpu;
    bool ok = smp_run_on_cpu_ex(ctx->caller_cpu, smp_ktest_mask_call, &ctx->inner, 100u, SMP_XCALL_F_ALLOW_NESTED);
    if (ok) __atomic_store_n(&ctx->allowed, 1u, __ATOMIC_RELEASE);
}

typedef struct smp_ktest_tlb_space_ctx {
    vmm_space_t *space;
    volatile u32 cpu_seen;
    volatile u32 switch_ok;
    volatile uptr cr3_seen;
} smp_ktest_tlb_space_ctx_t;

static void smp_ktest_tlb_space_touch_call(u32 cpu_id, void *raw) {
    smp_ktest_tlb_space_ctx_t *ctx = (smp_ktest_tlb_space_ctx_t *)raw;
    if (!ctx || !ctx->space) return;
    vmm_switch_space(ctx->space);
    ctx->cpu_seen = cpu_id;
    ctx->cr3_seen = vmm_read_cr3();
    if (vmm_current_space() == ctx->space && ctx->cr3_seen == ctx->space->pml4_physical) ctx->switch_ok = 1u;
    vmm_switch_kernel();
}

typedef struct smp_ktest_chaos_ctx {
    spinlock_t lock;
    volatile u32 inside;
    volatile u32 violations;
    volatile u32 seen_mask;
    volatile u32 current_id_mismatch;
    volatile u64 checksum;
    volatile u64 calls;
    volatile u64 per_cpu_loops[SMP_MAX_CPUS];
    u32 iterations;
} smp_ktest_chaos_ctx_t;

static void smp_ktest_chaos_call(u32 cpu_id, void *ctx) {
    smp_ktest_chaos_ctx_t *c = (smp_ktest_chaos_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    if (smp_current_cpu_id() != cpu_id) __atomic_add_fetch(&c->current_id_mismatch, 1u, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&c->calls, 1u, __ATOMIC_ACQ_REL);
    u64 x = 0x9e3779b97f4a7c15ull ^ ((u64)cpu_id << 32u) ^ timer_now_ns();
    for (u32 i = 0; i < c->iterations; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        if ((i & 63u) == 0u) {
            u64 flags = spin_lock_irqsave(&c->lock);
            if (__atomic_load_n(&c->inside, __ATOMIC_RELAXED) != 0u) __atomic_add_fetch(&c->violations, 1u, __ATOMIC_RELAXED);
            __atomic_add_fetch(&c->inside, 1u, __ATOMIC_RELAXED);
            __atomic_xor_fetch(&c->checksum, x + i + cpu_id, __ATOMIC_ACQ_REL);
            __atomic_sub_fetch(&c->inside, 1u, __ATOMIC_RELAXED);
            spin_unlock_irqrestore(&c->lock, flags);
        } else {
            __atomic_xor_fetch(&c->checksum, x, __ATOMIC_ACQ_REL);
        }
        __atomic_add_fetch(&c->per_cpu_loops[cpu_id], 1u, __ATOMIC_ACQ_REL);
    }
}


typedef struct smp_ktest_vfs_ctx {
    volatile u32 seen_mask;
    volatile u32 stat_ok;
    volatile u32 read_ok;
    volatile u32 ramfs_ok;
    volatile u32 failures;
    volatile u64 bytes;
} smp_ktest_vfs_ctx_t;

static void smp_ktest_vfs_call(u32 cpu_id, void *ctx) {
    smp_ktest_vfs_ctx_t *c = (smp_ktest_vfs_ctx_t *)ctx;
    if (!c || cpu_id >= SMP_MAX_CPUS) return;
    __atomic_or_fetch(&c->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);

    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    if (vfs_stat("/disk0/hello.txt", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 0) {
        __atomic_add_fetch(&c->stat_ok, 1u, __ATOMIC_ACQ_REL);
    } else {
        __atomic_add_fetch(&c->failures, 1u, __ATOMIC_ACQ_REL);
    }

    char buf[96];
    memset(buf, 0, sizeof(buf));
    usize got = 0;
    if (vfs_read("/disk0/hello.txt", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got != 0u && buf[0] != 0) {
        __atomic_add_fetch(&c->read_ok, 1u, __ATOMIC_ACQ_REL);
        __atomic_add_fetch(&c->bytes, got, __ATOMIC_ACQ_REL);
    } else {
        __atomic_add_fetch(&c->failures, 1u, __ATOMIC_ACQ_REL);
    }

    char path[64];
    char payload[64];
    ksnprintf(path, sizeof(path), "/tmp/smp-vfs-cpu%u", cpu_id);
    ksnprintf(payload, sizeof(payload), "smp-vfs cpu=%u", cpu_id);
    (void)vfs_unlink(path);
    usize written = 0;
    bool create_ok = vfs_create(path, payload, strlen(payload)) == VFS_OK;
    bool write_ok = vfs_write(path, strlen(payload), "!", 1u, &written) == VFS_OK && written == 1u;
    memset(buf, 0, sizeof(buf));
    got = 0;
    bool readback_ok = vfs_read(path, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got == strlen(payload) + 1u && buf[got - 1u] == '!';
    bool unlink_ok = vfs_unlink(path) == VFS_OK;
    if (create_ok && write_ok && readback_ok && unlink_ok) {
        __atomic_add_fetch(&c->ramfs_ok, 1u, __ATOMIC_ACQ_REL);
    } else {
        __atomic_add_fetch(&c->failures, 1u, __ATOMIC_ACQ_REL);
        (void)vfs_unlink(path);
    }
}

static bool smp_ktest_all_started_seen(const smp_info_t *info, u32 seen_mask) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (info->cpus[i].started && ((seen_mask & (1u << i)) == 0)) return false;
    }
    return true;
}

static bool smp_ktest_started_queue_empty(const smp_info_t *info) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (info->cpus[i].started && info->cpus[i].queue_depth != 0u) return false;
    }
    return true;
}


static bool smp_ktest_boot_hardening_ok(const smp_info_t *info) {
    if (!info) return false;
    bool ok = true;
    u32 ap_started = 0;
    u32 ap_attempted = 0;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        const smp_cpu_info_t *c = &info->cpus[i];
        if (!c->enabled || c->bsp) continue;
        if (c->boot_attempts != 0u) ++ap_attempted;
        if (!c->started) continue;
        ++ap_started;
        if (!smp_cpu_is_online(i) || c->boot_error != SMP_BOOT_ERROR_NONE) ok = false;
        if (c->boot_stage != SMP_BOOT_STAGE_ONLINE) ok = false;
        if (c->boot_attempts == 0u || c->boot_sipi_count < 2u) ok = false;
        if (c->boot_generation_seen == 0u) ok = false;
        if (c->stack_base == 0u || c->stack_top == 0u || (c->stack_top & 0xfu) != 0u) ok = false;
        if (c->boot_cr3 == 0u || (c->boot_cr3 >> 32u) != 0u) ok = false;
        if (c->boot_entry == 0u) ok = false;
        if (c->boot_trampoline_base != SMP_TRAMPOLINE_BASE || c->boot_trampoline_size == 0u || c->boot_trampoline_size > SMP_TRAMPOLINE_MAX_SIZE) ok = false;
    }
    if (info->started_cpu_count > 1u) {
        if (ap_started + 1u != info->started_cpu_count) ok = false;
        if (ap_attempted < ap_started) ok = false;
    }
    return ok;
}

static bool smp_ktest_started_queue_executed(const smp_info_t *info) {
    if (!info) return false;
    u64 per_cpu_classes = 0u;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        if (!info->cpus[i].bsp && info->cpus[i].queue_executed != info->cpus[i].queue_enqueued) return false;
        if (!info->cpus[i].bsp && info->cpus[i].queue_executed == 0u) return false;
        if (info->cpus[i].queue_sync_executed + info->cpus[i].queue_async_executed != info->cpus[i].queue_executed) return false;
        per_cpu_classes += info->cpus[i].queue_sync_executed + info->cpus[i].queue_async_executed;
    }
    return per_cpu_classes == info->queue_sync_executed + info->queue_async_executed;
}

static bool smp_ktest_tlb_last_ack_ok(const smp_info_t *info, u32 expected_mask) {
    if (!info) return false;
    expected_mask &= smp_targetable_cpu_mask();
    if ((info->tlb_last_target_mask & expected_mask) != expected_mask) return false;
    if ((info->tlb_last_completed_mask & expected_mask) != expected_mask) return false;
    if ((info->tlb_last_completed_mask & ~info->tlb_last_target_mask) != 0u) return false;
    if ((info->tlb_last_skipped_mask & ~info->tlb_last_target_mask) != 0u) return false;
    if (info->tlb_last_pages == 0u) return false;
    return true;
}


static bool smp_ktest_arch_state_ready(const smp_info_t *info) {
    if (!info || info->started_cpu_count == 0u) return false;
    if (info->arch_init_failures != 0u || info->arch_init_cpus < info->started_cpu_count) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        const cpu_arch_state_t *a = &info->cpus[i].arch_state;
        if (a->initialized == 0u || a->fpu_ready == 0u || a->sse_ready == 0u || a->nxe_ready == 0u) return false;
        if ((a->features & (CPU_ARCH_FLAG_FPU | CPU_ARCH_FLAG_FXSR | CPU_ARCH_FLAG_SSE | CPU_ARCH_FLAG_SSE2 | CPU_ARCH_FLAG_NX)) !=
            (CPU_ARCH_FLAG_FPU | CPU_ARCH_FLAG_FXSR | CPU_ARCH_FLAG_SSE | CPU_ARCH_FLAG_SSE2 | CPU_ARCH_FLAG_NX)) return false;
        if (a->max_basic_leaf == 0u || a->max_extended_leaf < 0x80000000u) return false;
    }
    return true;
}

static bool smp_ktest_xsave_state_coherent(const smp_info_t *info) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        const cpu_arch_state_t *a = &info->cpus[i].arch_state;
        if ((a->features & CPU_ARCH_FLAG_XSAVE) == 0u) continue;
        if (a->xsave_ready == 0u || (a->features & CPU_ARCH_FLAG_OSXSAVE) == 0u) return false;
        if ((a->xcr0 & 0x3u) != 0x3u || a->xsave_area_size == 0u || a->xsave_area_max_size < a->xsave_area_size) return false;
    }
    return true;
}

static bool smp_ktest_pat_state_coherent(const smp_info_t *info) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        const cpu_arch_state_t *a = &info->cpus[i].arch_state;
        if ((a->features & CPU_ARCH_FLAG_PAT) != 0u && a->pat_wc_ready == 0u) return false;
    }
    return true;
}


static bool smp_ktest_memory_type_state_consistent(const smp_info_t *info) {
    if (!info || info->started_cpu_count == 0u) return false;
    const cpu_arch_state_t *ref = 0;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (info->cpus[i].started) { ref = &info->cpus[i].arch_state; break; }
    }
    if (!ref) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        const cpu_arch_state_t *a = &info->cpus[i].arch_state;
        if (((a->features ^ ref->features) & (CPU_ARCH_FLAG_PAT | CPU_ARCH_FLAG_MTRR | CPU_ARCH_FLAG_INVARIANT_TSC)) != 0u) return false;
        if ((a->features & CPU_ARCH_FLAG_PAT) != 0u && a->pat != ref->pat) return false;
        if ((a->features & CPU_ARCH_FLAG_MTRR) != 0u && a->mtrr_default_type != ref->mtrr_default_type) return false;
    }
    return true;
}

static bool smp_ktest_guarded_gdt_stacks_ready(const smp_info_t *info) {
    if (!info || info->started_cpu_count == 0u) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        gdt_stack_info_t st;
        if (!gdt_cpu_stack_info(i, &st)) return false;
        if (!st.loaded || !st.dynamic_stacks_installed || !st.guard_pages_installed || !st.canaries_ok) return false;
        if (st.ring0_base == 0 || st.ring0_top <= st.ring0_base || st.ring0_guard_low + PAGE_SIZE != st.ring0_base) return false;
        if (st.ring0_top != st.ring0_guard_high || (st.ring0_top & 0xfu) != 0) return false;
        for (u32 j = 0; j < 3u; ++j) {
            if (st.ist_base[j] == 0 || st.ist_top[j] <= st.ist_base[j]) return false;
            if (st.ist_guard_low[j] + PAGE_SIZE != st.ist_base[j]) return false;
            if (st.ist_top[j] != st.ist_guard_high[j] || (st.ist_top[j] & 0xfu) != 0) return false;
        }
    }
    return true;
}


static u64 smp_ktest_xcall_latency_bucket_total(const smp_info_t *info) {
    if (!info) return 0u;
    u64 total = 0u;
    for (u32 i = 0; i < SMP_XCALL_LATENCY_BUCKETS; ++i) total += info->xcall_latency_buckets[i];
    return total;
}

static bool smp_ktest_observability_status_line_ok(void) {
    char *buf = (char *)kmalloc(8192u);
    if (!buf) return false;
    smp_format_status(buf, 8192u);
    bool ok = strstr(buf, "smp-observe:") != 0 &&
              strstr(buf, "latency[<10us]") != 0 &&
              strstr(buf, "last_xcall_seq") != 0 &&
              strstr(buf, "smp-queue:") != 0 &&
              strstr(buf, "smp-tlb:") != 0 &&
              strstr(buf, "sync_exec=") != 0 &&
              strstr(buf, "ipi_exec=") != 0 &&
              strstr(buf, "last_completed=") != 0;
    kfree(buf);
    return ok;
}

static bool smp_ktest_last_xcall_metadata_ok(const smp_info_t *info, u32 seen_mask) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count && i < SMP_MAX_CPUS; ++i) {
        if ((seen_mask & (1u << i)) == 0u) continue;
        const smp_cpu_info_t *c = &info->cpus[i];
        if (c->last_xcall_seq == 0u || c->last_xcall_fn == 0u || c->last_xcall_start_ns == 0u || c->last_xcall_finish_ns < c->last_xcall_start_ns) return false;
    }
    return true;
}

static bool smp_ktest_ipi_observability_ok(const smp_info_t *info) {
    if (!info || info->started_cpu_count <= 1u) return true;
    for (u32 i = 0; i < info->cpu_count && i < SMP_MAX_CPUS; ++i) {
        const smp_cpu_info_t *c = &info->cpus[i];
        if (!c->started || c->bsp) continue;
        if (c->last_ipi_vector == SMP_IPI_WAKE_VECTOR || c->last_ipi_vector == SMP_IPI_RESCHEDULE_VECTOR) return true;
    }
    return false;
}

static void test_smp_contracts(void) {
    suite_begin("smp/multicore/contracts");
    const smp_info_t *info = smp_get_info();
    check(info && info->initialized, "smp groundwork initialized");
    check(info && info->cpu_count >= 1u && info->cpu_count <= SMP_MAX_CPUS, "smp cpu inventory count in bounds");
    check(info && info->enabled_cpu_count >= 1u && info->enabled_cpu_count <= info->cpu_count, "smp enabled cpu count in bounds");
    check(info && info->started_cpu_count >= 1u && info->started_cpu_count <= info->enabled_cpu_count, "smp started cpu count in bounds");
    check(smp_started_cpu_count() == (info ? info->started_cpu_count : 0u), "smp started cpu count accessor matches topology");
    smp_cpumask_t possible_mask = smp_possible_cpu_mask();
    smp_cpumask_t present_mask = smp_present_cpu_mask();
    smp_cpumask_t enabled_mask = smp_enabled_cpu_mask();
    smp_cpumask_t online_mask = smp_online_cpu_mask();
    smp_cpumask_t active_mask = smp_active_cpu_mask();
    smp_cpumask_t failed_mask = smp_failed_cpu_mask();
    smp_cpumask_t dense_mask = info ? ((info->cpu_count >= SMP_MAX_CPUS) ? SMP_CPUMASK_VALID : ((1u << info->cpu_count) - 1u)) : 0u;
    check((smp_started_cpu_mask() & ~dense_mask) == 0u, "smp started cpu mask is bounded by topology");
    check(possible_mask == dense_mask, "smp possible cpu mask is dense for discovered topology");
    check((present_mask & ~possible_mask) == 0u && (enabled_mask & ~present_mask) == 0u, "smp present/enabled masks are subsets of possible topology");
    check((online_mask & ~enabled_mask) == 0u && (active_mask & ~online_mask) == 0u, "smp online/active masks are subsets of enabled topology");
    check((failed_mask & online_mask) == 0u, "smp failed mask excludes online CPUs");
    check(smp_cpumask_weight(online_mask) == (info ? info->started_cpu_count : 0u), "smp online mask weight matches started count");
    check(smp_targetable_cpu_mask() == active_mask, "smp targetable cpu mask tracks active CPUs");
    check((smp_offline_cpu_mask() & online_mask) == 0u && (smp_booting_cpu_mask() & online_mask) == 0u, "smp offline/booting masks exclude online CPUs");
    check((smp_dying_cpu_mask() & active_mask) == 0u && (smp_dead_cpu_mask() & online_mask) == 0u, "smp dying/dead masks exclude active online CPUs");
    check(smp_cpu_state_name(SMP_CPU_ACTIVE)[0] != 'u' && smp_cpu_state_name(SMP_CPU_DEAD)[0] != 'u', "smp lifecycle state names are stable");
    check(!smp_cpu_is_targetable(SMP_MAX_CPUS), "smp rejects out-of-range CPU as targetable");
    u32 invalid_mask = (SMP_MAX_CPUS > 1u) ? (1u << (SMP_MAX_CPUS - 1u)) : 0u;
    if (info && info->cpu_count < SMP_MAX_CPUS && invalid_mask != 0u) {
        u64 inactive_reject_before = info->xcall_reject_inactive_cpu;
        u32 invalid_done = smp_run_on_mask_async(invalid_mask, smp_ktest_seen_call, 0);
        check(invalid_done == 0u && info->xcall_reject_inactive_cpu >= inactive_reject_before, "smp xcall rejects inactive/out-of-topology targets");
    } else {
        skip("smp inactive target rejection requires spare topology bit");
    }
    u32 bsp_count = 0;
    bool logical_ok = true;
    bool duplicate_apic = false;
    for (u32 i = 0; info && i < info->cpu_count; ++i) {
        if (info->cpus[i].logical_id != i) logical_ok = false;
        if (info->cpus[i].bsp) ++bsp_count;
        for (u32 j = i + 1u; j < info->cpu_count; ++j) {
            if (info->cpus[i].enabled && info->cpus[j].enabled && info->cpus[i].apic_id == info->cpus[j].apic_id) duplicate_apic = true;
        }
    }
    check(logical_ok, "smp logical ids are dense");
    check(bsp_count == 1u, "smp has exactly one BSP");
    check(!duplicate_apic, "smp enabled CPUs have unique APIC ids");
    bool topo_levels_ok = true;
    for (u32 i = 0; info && i < info->cpu_count; ++i) {
        if (info->cpus[i].package_id >= SMP_MAX_CPUS || info->cpus[i].core_id >= SMP_MAX_CPUS) topo_levels_ok = false;
    }
    check(topo_levels_ok, "smp topology package/core/thread ids are bounded");
    check(smp_ktest_arch_state_ready(info), "smp records initialized FPU/SSE/NX architectural state for every started CPU");
    check(smp_ktest_xsave_state_coherent(info), "smp records coherent XSAVE/XCR0 state when CPU supports XSAVE");
    check(smp_ktest_pat_state_coherent(info), "smp records PAT write-combining readiness per CPU");
    check(smp_ktest_memory_type_state_consistent(info), "smp records consistent PAT/MTRR/TSC architectural state across started CPUs");
    check(smp_ktest_guarded_gdt_stacks_ready(info), "smp started CPUs have guarded TSS and IST stacks");
    check(smp_current_cpu_id() < SMP_MAX_CPUS, "smp current cpu id in bounds");
    check(smp_percpu_ready() && smp_current_percpu() && smp_current_percpu()->logical_id == smp_current_cpu_id(), "smp BSP has installed current per-cpu state");
    uptr tramp_phys = 0;
    u64 tramp_flags = 0;
    bool tramp_mapped = vmm_translate((uptr)SMP_TRAMPOLINE_BASE, &tramp_phys, &tramp_flags);
    check(tramp_mapped && tramp_phys == (uptr)SMP_TRAMPOLINE_BASE && (tramp_flags & (VMM_HUGE | VMM_USER)) == 0, "smp trampoline has supervisor 4K identity mapping");
    const apic_info_t *ap = apic_get_info();
    if (info && ap && ap->lapic_present && ap->lapic_base && info->enabled_cpu_count > 1u) {
        check(tramp_mapped && (tramp_flags & (VMM_NX | VMM_WRITE)) == 0, "smp trampoline is executable read-only for AP startup");
        check(smp_ktest_boot_hardening_ok(info), "smp AP boot hardening records clean stage/error/mailbox telemetry");
        check(smp_booting_identity_selftest(), "smp boot-in-progress state preserves installed per-cpu identity");
        check(smp_boot_stage_name(SMP_BOOT_STAGE_ONLINE)[0] != 'u' && smp_boot_error_name(SMP_BOOT_ERROR_NONE)[0] != 'u', "smp AP boot stage and error names are stable");
    } else {
        skip("smp trampoline executable permission requires local APIC and AP targets");
        skip("smp AP boot hardening telemetry requires AP startup");
        skip("smp boot-in-progress identity regression requires AP startup");
        skip("smp AP boot stage names require AP startup");
    }
    check(smp_selftest(), "smp selftest passes online topology and cross-call contract");
    check(smp_ktest_started_queue_empty(info), "smp call queues start observable tests empty");

    smp_ktest_seen_ctx_t seen;
    memset(&seen, 0, sizeof(seen));
    bool call_ok = smp_run_on_all(smp_ktest_seen_call, &seen, 100u);
    check(call_ok, "smp cross-call reaches all started CPUs");
    bool seen_ok = call_ok && smp_ktest_all_started_seen(info, seen.seen_mask);
    check(seen_ok && seen.calls == info->started_cpu_count, "smp cross-call completion bitmap matches started CPUs");
    check(seen.current_id_mismatch == 0u, "smp cross-call callback observes matching current cpu id");
    check(smp_ktest_started_queue_empty(info), "smp cross-call drains all per-cpu queues");
    check(smp_flush_local_call_queue() == 0u, "smp explicit local queue flush is idempotent when empty");
    check(smp_ktest_xcall_latency_bucket_total(info) > 0u, "smp xcall latency histogram records completed calls");
    check(smp_xcall_latency_bucket_name(5u)[0] != 'u' && smp_xcall_latency_bucket_name(SMP_XCALL_LATENCY_BUCKETS)[0] == 'u', "smp xcall latency bucket names are stable");
    check(smp_ktest_last_xcall_metadata_ok(info, seen.seen_mask), "smp per-cpu observability records last xcall metadata");
    check(smp_ktest_observability_status_line_ok(), "smp status includes compact observability summary");

    smp_ktest_percpu_ctx_t percpu;
    memset(&percpu, 0, sizeof(percpu));
    bool percpu_ok = smp_run_on_all(smp_ktest_percpu_call, &percpu, 100u);
    check(percpu_ok, "smp per-cpu state callback reaches all CPUs");
    check(smp_ktest_all_started_seen(info, percpu.seen_mask), "smp per-cpu state observed every started CPU");
    check(percpu.null_current == 0u && percpu.mismatch == 0u, "smp per-cpu state identity and address-space pointers are consistent");
    check(percpu.process_active == 0u, "smp per-cpu current process slots are inactive after user tests");
    check(smp_ktest_percpu_unique_started(info, &percpu), "smp per-cpu current process and cpu slots are unique per started CPU");

    smp_ktest_race_ctx_t race;
    memset(&race, 0, sizeof(race));
    spinlock_init(&race.lock);
    race.iterations = 2048u;
    bool race_ok = smp_run_on_all(smp_ktest_spinlock_race_call, &race, 300u);
    check(race_ok, "smp spinlock stress cross-call completed");
    check(race.violations == 0u && race.inside == 0u, "smp spinlock stress detected no concurrent critical-section overlap");
    check(race.counter == (u64)race.iterations * (u64)info->started_cpu_count, "smp spinlock stress counter exact");

    smp_ktest_atomic_ctx_t atomic;
    memset(&atomic, 0, sizeof(atomic));
    atomic.iterations = 4096u;
    bool atomic_ok = smp_run_on_all(smp_ktest_atomic_call, &atomic, 300u);
    check(atomic_ok, "smp atomic stress cross-call completed");
    check(atomic.counter == (u64)atomic.iterations * (u64)info->started_cpu_count, "smp atomic stress counter exact");
    check(smp_ktest_all_started_seen(info, atomic.seen_mask), "smp atomic stress saw every started CPU");

    u32 started_mask = smp_started_cpu_mask();
    smp_ktest_mask_ctx_t local_only;
    memset(&local_only, 0, sizeof(local_only));
    u32 self_cpu = smp_current_cpu_id();
    local_only.expected_mask = 1u << self_cpu;
    u32 local_completed = smp_run_on_mask(local_only.expected_mask, smp_ktest_mask_call, &local_only, 100u);
    check(local_completed == 1u, "smp run_on_mask executes local-only target");
    check(local_only.seen_mask == local_only.expected_mask && local_only.calls == 1u, "smp run_on_mask local-only does not spill to APs");
    check(local_only.unexpected == 0u && local_only.current_id_mismatch == 0u, "smp run_on_mask local-only preserves callback CPU identity");

    if (info && info->started_cpu_count > 1u) {
        u32 ap_mask = started_mask & ~(1u << self_cpu);
        smp_ktest_mask_ctx_t remote_only;
        memset(&remote_only, 0, sizeof(remote_only));
        remote_only.expected_mask = ap_mask;
        u32 remote_expected = info->started_cpu_count - 1u;
        u64 queue_sync_before = info->queue_sync_executed;
        u32 remote_completed = smp_run_on_mask(ap_mask, smp_ktest_mask_call, &remote_only, 100u);
        check(remote_completed == remote_expected, "smp run_on_mask executes remote AP subset");
        check(info->queue_sync_executed >= queue_sync_before + remote_expected, "smp queue telemetry classifies synchronous remote calls");
        check(remote_only.seen_mask == ap_mask && remote_only.calls == remote_expected, "smp run_on_mask remote subset has exact call count");
        check(remote_only.unexpected == 0u && remote_only.current_id_mismatch == 0u, "smp run_on_mask remote subset preserves callback CPU identity");
        bool per_cpu_single_ok = true;
        for (u32 cpu = 0; cpu < info->cpu_count; ++cpu) {
            if (!info->cpus[cpu].started) continue;
            smp_ktest_mask_ctx_t single;
            memset(&single, 0, sizeof(single));
            single.expected_mask = 1u << cpu;
            u32 one = smp_run_on_cpu(cpu, smp_ktest_mask_call, &single, 100u) ? 1u : 0u;
            if (one != 1u || single.calls != 1u || single.seen_mask != (1u << cpu) || single.unexpected != 0u || single.current_id_mismatch != 0u) per_cpu_single_ok = false;
        }
        check(per_cpu_single_ok, "smp run_on_cpu reaches every online CPU exactly once");
    } else {
        skip("smp run_on_mask remote subset requires more than one started CPU");
        skip("smp run_on_mask remote identity requires more than one started CPU");
        skip("smp run_on_cpu AP walk requires more than one started CPU");
        skip("smp run_on_cpu per-CPU identity requires more than one started CPU");
    }

    if (info && info->started_cpu_count > 1u) {
        u32 ap_mask = started_mask & ~(1u << self_cpu);
        u32 remote_expected = info->started_cpu_count - 1u;

        smp_ktest_async_ctx_t async_ctx;
        memset(&async_ctx, 0, sizeof(async_ctx));
        u64 async_before = info->async_cross_calls;
        u64 async_done_before = info->async_call_completions;
        u64 queue_async_before = info->queue_async_executed;
        u32 async_issued = smp_run_on_mask_async(started_mask, smp_ktest_async_call, &async_ctx);
        bool async_done = smp_ktest_wait_async_calls(&async_ctx, info->started_cpu_count, 100000000ull);
        check(async_issued == info->started_cpu_count, "smp async cross-call issues one item per started CPU");
        check(async_done && async_ctx.calls == info->started_cpu_count, "smp async cross-call eventually completes on every started CPU");
        check(smp_ktest_all_started_seen(info, async_ctx.seen_mask) && async_ctx.current_id_mismatch == 0u, "smp async cross-call preserves CPU identity");
        check(info->async_cross_calls > async_before && info->async_call_completions >= async_done_before + async_issued, "smp async cross-call telemetry records issue and completion");
        check(info->queue_async_executed >= queue_async_before + remote_expected, "smp queue telemetry classifies asynchronous remote calls");
        check(smp_ktest_started_queue_empty(info), "smp async cross-call leaves queues empty after completion");

        smp_ktest_mask_ctx_t irq_reject_ctx;
        memset(&irq_reject_ctx, 0, sizeof(irq_reject_ctx));
        irq_reject_ctx.expected_mask = ap_mask;
        u64 reject_before = info->xcall_rejections;
        u64 irq_reject_before = info->xcall_reject_irq_context;
        smp_note_interrupt_enter(0xe1u, false);
        u32 irq_rejected = smp_run_on_mask(ap_mask, smp_ktest_mask_call, &irq_reject_ctx, 5u);
        smp_note_interrupt_exit(0xe1u);
        check(irq_rejected == 0u && irq_reject_ctx.calls == 0u, "smp sync remote cross-call rejects default IRQ-context caller");
        check(info->xcall_rejections > reject_before && info->xcall_reject_irq_context > irq_reject_before, "smp xcall telemetry records IRQ-context rejection");

        smp_ktest_mask_ctx_t irq_allow_ctx;
        memset(&irq_allow_ctx, 0, sizeof(irq_allow_ctx));
        irq_allow_ctx.expected_mask = ap_mask;
        smp_note_interrupt_enter(0xe2u, false);
        u32 irq_allowed = smp_run_on_mask_ex(ap_mask, smp_ktest_mask_call, &irq_allow_ctx, 100u, SMP_XCALL_F_ALLOW_IRQ_CONTEXT);
        smp_note_interrupt_exit(0xe2u);
        check(irq_allowed == remote_expected && irq_allow_ctx.calls == remote_expected && irq_allow_ctx.unexpected == 0u, "smp explicit IRQ-context override permits nonblocking remote xcall");

        smp_ktest_mask_ctx_t if_reject_ctx;
        memset(&if_reject_ctx, 0, sizeof(if_reject_ctx));
        if_reject_ctx.expected_mask = ap_mask;
        u64 if_reject_before = info->xcall_reject_irq_disabled;
        u64 saved_flags = irq_save();
        u32 if_rejected = smp_run_on_mask(ap_mask, smp_ktest_mask_call, &if_reject_ctx, 5u);
        irq_restore(saved_flags);
        check(if_rejected == 0u && if_reject_ctx.calls == 0u, "smp sync remote cross-call rejects default IRQ-disabled caller");
        check(info->xcall_reject_irq_disabled > if_reject_before, "smp xcall telemetry records IRQ-disabled rejection");

        u32 first_ap = 0xffffffffu;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            if (info->cpus[i].started && !info->cpus[i].bsp) { first_ap = i; break; }
        }
        smp_ktest_nested_ctx_t nested_reject;
        memset(&nested_reject, 0, sizeof(nested_reject));
        nested_reject.caller_cpu = self_cpu;
        u64 nested_reject_before = info->xcall_reject_nested;
        bool nested_outer = first_ap != 0xffffffffu && smp_run_on_cpu(first_ap, smp_ktest_nested_reject_call, &nested_reject, 100u);
        check(nested_outer && nested_reject.rejected == 1u && nested_reject.inner.calls == 0u, "smp nested sync xcall is rejected by default inside xcall callback");
        check(info->xcall_reject_nested > nested_reject_before, "smp xcall telemetry records nested sync rejection");

        smp_ktest_nested_ctx_t nested_allow;
        memset(&nested_allow, 0, sizeof(nested_allow));
        nested_allow.caller_cpu = self_cpu;
        bool nested_allowed = first_ap != 0xffffffffu && smp_run_on_cpu(first_ap, smp_ktest_nested_allow_call, &nested_allow, 200u);
        check(nested_allowed && nested_allow.allowed == 1u && nested_allow.inner.calls == 1u && nested_allow.inner.seen_mask == (1u << self_cpu), "smp explicit nested override completes while caller drains local queue");
        check(smp_ktest_started_queue_empty(info), "smp xcall contract tests leave queues empty");
    } else {
        skip("smp async xcall contract requires more than one started CPU");
        skip("smp IRQ-context xcall rejection requires more than one started CPU");
        skip("smp IRQ-disabled xcall rejection requires more than one started CPU");
        skip("smp nested xcall contract requires more than one started CPU");
    }

    if (info && info->started_cpu_count > 1u) {
        const u32 rounds = 256u;
        smp_ktest_latency_ctx_t latency;
        memset(&latency, 0, sizeof(latency));
        u32 completed = 0;
        u64 total_ns = 0;
        u64 max_ns = 0;
        u64 min_ns = ~0ull;
        u64 failures_before = info->total_cross_call_failures;
        for (u32 round = 0; round < rounds; ++round) {
            u64 start_ns = timer_now_ns();
            bool ok = smp_run_on_all(smp_ktest_latency_call, &latency, 100u);
            u64 elapsed = timer_now_ns() - start_ns;
            if (ok) ++completed;
            if (elapsed > max_ns) max_ns = elapsed;
            if (elapsed < min_ns) min_ns = elapsed;
            total_ns += elapsed;
        }
        KLOG(LOG_INFO, "ktest", "smp latency: rounds=%u min_ns=%llu avg_ns=%llu max_ns=%llu calls=%llu",
             rounds, (unsigned long long)min_ns, (unsigned long long)(total_ns / rounds),
             (unsigned long long)max_ns, (unsigned long long)latency.calls);
        check(completed == rounds, "smp repeated cross-call flood completes");
        check(latency.calls == (u64)rounds * (u64)info->started_cpu_count, "smp repeated cross-call flood invokes every started CPU each round");
        check(smp_ktest_all_started_seen(info, latency.seen_mask), "smp repeated cross-call flood observes every started CPU");
        bool per_cpu_exact = true;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            if (info->cpus[i].started && latency.per_cpu_calls[i] != rounds) per_cpu_exact = false;
        }
        check(per_cpu_exact, "smp repeated cross-call flood has exact per-CPU call counts");
        check(info->total_cross_call_failures == failures_before, "smp repeated cross-call flood records no timeouts");
        check(max_ns < 50000000ull, "smp repeated cross-call latency stays below 50ms guardrail");
        check(info->max_cross_call_ns >= max_ns || info->max_cross_call_ns > 0u, "smp latency telemetry records max cross-call duration");
        check(info->total_call_items >= latency.calls - rounds, "smp queued call telemetry counts flood remote items");

        smp_ktest_chaos_ctx_t chaos;
        memset(&chaos, 0, sizeof(chaos));
        spinlock_init(&chaos.lock);
        chaos.iterations = 8192u;
        u64 chaos_start = timer_now_ns();
        bool chaos_ok = smp_run_on_all(smp_ktest_chaos_call, &chaos, 500u);
        u64 chaos_ns = timer_now_ns() - chaos_start;
        check(chaos_ok, "smp chaos workload completes on all CPUs");
        check(chaos.calls == info->started_cpu_count, "smp chaos workload invokes every CPU once");
        check(smp_ktest_all_started_seen(info, chaos.seen_mask), "smp chaos workload observes all started CPUs");
        check(chaos.current_id_mismatch == 0u, "smp chaos workload preserves CPU-local identity");
        check(chaos.violations == 0u && chaos.inside == 0u, "smp chaos workload keeps shared critical section serialized");
        bool chaos_loops_exact = true;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            if (info->cpus[i].started && chaos.per_cpu_loops[i] != chaos.iterations) chaos_loops_exact = false;
        }
        check(chaos_loops_exact, "smp chaos workload accounts exact per-CPU loop counts");
        check(chaos.checksum != 0u || chaos.iterations != 0u, "smp chaos workload mutates shared checksum");
        check(chaos_ns < 200000000ull, "smp chaos workload finishes under 200ms guardrail");

        u64 soak_start = timer_now_ns();
        u32 soak_rounds = 0;
        smp_ktest_latency_ctx_t soak;
        memset(&soak, 0, sizeof(soak));
        while (soak_rounds < 1024u && (timer_now_ns() - soak_start < 25000000ull || soak_rounds < 64u)) {
            if (!smp_run_on_all(smp_ktest_latency_call, &soak, 100u)) break;
            ++soak_rounds;
        }
        u64 soak_ns = timer_now_ns() - soak_start;
        KLOG(LOG_INFO, "ktest", "smp soak: rounds=%u ns=%llu calls=%llu", soak_rounds, (unsigned long long)soak_ns, (unsigned long long)soak.calls);
        check(soak_rounds >= 64u, "smp timed soak executes at least 64 cross-call rounds");
        check(soak.calls == (u64)soak_rounds * (u64)info->started_cpu_count, "smp timed soak call accounting is exact");
        check(smp_ktest_all_started_seen(info, soak.seen_mask), "smp timed soak observes all started CPUs");
        check(soak_ns >= 1000000ull, "smp timed soak consumes measurable monotonic time");
        check(smp_ktest_started_queue_empty(info), "smp timed soak leaves queues empty");

        u64 tlb_before = info->tlb_shootdowns;
        bool tlb_ok = smp_tlb_shootdown((uptr)&soak, sizeof(soak), 0, 100u);
        check(tlb_ok, "smp TLB shootdown reaches all started CPUs");
        check(info->tlb_shootdowns > tlb_before && info->tlb_shootdown_failures == 0u, "smp TLB shootdown telemetry records success");
        check(smp_ktest_tlb_last_ack_ok(info, smp_started_cpu_mask()), "smp TLB observability records complete broadcast ACK mask");
        check(info->queue_ipi_executed <= info->queue_sync_executed + info->queue_async_executed, "smp IRQ-immediate queue telemetry remains bounded by executed queue work");

        vmm_space_t tlb_space;
        memset(&tlb_space, 0, sizeof(tlb_space));
        bool tlb_space_created = vmm_space_create_user(&tlb_space);
        u32 tlb_ap = 0xffffffffu;
        for (u32 i = 0; i < info->cpu_count; ++i) if (info->cpus[i].started && !info->cpus[i].bsp) { tlb_ap = i; break; }
        smp_ktest_tlb_space_ctx_t tlb_ctx;
        memset(&tlb_ctx, 0, sizeof(tlb_ctx));
        tlb_ctx.space = &tlb_space;
        bool tlb_touch = tlb_space_created && tlb_ap != 0xffffffffu && smp_run_on_cpu(tlb_ap, smp_ktest_tlb_space_touch_call, &tlb_ctx, 200u);
        u32 active_mask = __atomic_load_n(&tlb_space.active_cpu_mask, __ATOMIC_ACQUIRE);
        u64 targeted_before = info->tlb_targeted_shootdowns;
        u64 gen_before = __atomic_load_n(&tlb_space.tlb_generation, __ATOMIC_ACQUIRE);
        bool targeted_ok = tlb_touch && vmm_space_tlb_shootdown(&tlb_space, 0x0000010000200000ull, PAGE_SIZE, false, 200u);
        u64 gen_after = __atomic_load_n(&tlb_space.tlb_generation, __ATOMIC_ACQUIRE);
        check(tlb_space_created, "smp TLB user address-space fixture is created");
        check(tlb_touch && tlb_ctx.switch_ok == 1u && tlb_ctx.cpu_seen == tlb_ap && tlb_ctx.cr3_seen == tlb_space.pml4_physical, "smp TLB fixture records remote address-space activation");
        check((active_mask & (1u << tlb_ap)) != 0u, "smp TLB active CPU mask tracks address-space users");
        check(targeted_ok && gen_after > gen_before, "smp TLB address-space shootdown bumps generation");
        check(info->tlb_targeted_shootdowns > targeted_before, "smp TLB address-space shootdown uses targeted CPU mask");
        check(smp_ktest_tlb_last_ack_ok(info, active_mask), "smp TLB observability records complete targeted ACK mask");
        check(tlb_ap == 0xffffffffu || tlb_space.tlb_seen_generation[tlb_ap] >= gen_after, "smp TLB address-space tracks per-space generation ACK");
        check(info->percpu[tlb_ap].tlb_generation_seen >= gen_after, "smp TLB remote CPU acknowledges address-space generation");
        if (tlb_space_created) vmm_space_destroy(&tlb_space);

        smp_ktest_vfs_ctx_t vfsctx;
        memset(&vfsctx, 0, sizeof(vfsctx));
        bool vfs_ok = smp_run_on_all(smp_ktest_vfs_call, &vfsctx, 500u);
        check(vfs_ok, "smp concurrent VFS/EXT4 syscall-shaped workload completes");
        check(smp_ktest_all_started_seen(info, vfsctx.seen_mask), "smp concurrent VFS/EXT4 workload observes all CPUs");
        check(vfsctx.stat_ok == info->started_cpu_count && vfsctx.read_ok == info->started_cpu_count, "smp concurrent EXT4 reads succeed on every CPU");
        check(vfsctx.ramfs_ok == info->started_cpu_count && vfsctx.failures == 0u, "smp concurrent VFS mutations remain serialized and clean");

        u64 idle_wake_before[SMP_MAX_CPUS];
        u64 ipi_before[SMP_MAX_CPUS];
        memset(idle_wake_before, 0, sizeof(idle_wake_before));
        memset(ipi_before, 0, sizeof(ipi_before));
        for (u32 i = 0; i < info->cpu_count; ++i) {
            idle_wake_before[i] = info->cpus[i].idle_wakeups;
            ipi_before[i] = info->cpus[i].ipi_wakeups;
        }
        timer_sleep_ticks(1);
        smp_ktest_seen_ctx_t hlt_seen;
        memset(&hlt_seen, 0, sizeof(hlt_seen));
        bool hlt_ok = smp_run_on_all(smp_ktest_seen_call, &hlt_seen, 100u);
        bool woke_ap = false;
        bool ipi_ap = false;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            if (info->cpus[i].started && !info->cpus[i].bsp) {
                if (info->cpus[i].idle_wakeups > idle_wake_before[i]) woke_ap = true;
                if (info->cpus[i].ipi_wakeups > ipi_before[i]) ipi_ap = true;
            }
        }
        check(hlt_ok && smp_ktest_all_started_seen(info, hlt_seen.seen_mask), "smp parked AP wake cross-call reaches all started CPUs");
        check(woke_ap, "smp parked AP wake advances idle wake telemetry");
        check(ipi_ap, "smp parked AP wake advances IPI telemetry");
        check(smp_ktest_ipi_observability_ok(info), "smp per-cpu observability records last IPI vector and wake reason");
        check(hlt_seen.current_id_mismatch == 0u, "smp parked AP wake preserves callback CPU identity");
    } else {
        skip("smp repeated cross-call latency stress requires more than one started CPU");
        skip("smp chaos workload requires more than one started CPU");
        skip("smp timed soak requires more than one started CPU");
        skip("smp parked AP wake test requires more than one started CPU");
        skip("smp parked AP wake telemetry requires more than one started CPU");
    }

    if (info && info->started_cpu_count > 1u) {
        bool ap_work = true;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            if (!info->cpus[i].bsp && info->cpus[i].started && info->cpus[i].work_executed == 0) ap_work = false;
        }
        check(ap_work, "smp APs execute kernel work items");
        check(smp_ktest_started_queue_empty(info), "smp AP queues are empty after all SMP contract tests");
        check(smp_ktest_started_queue_executed(info), "smp AP queue telemetry records remote execution");
        check(info->queue_sync_executed != 0u && info->queue_async_executed != 0u, "smp queue class telemetry records both sync and async work");
        check(info->queue_enqueue_failures == 0u, "smp queue reports no enqueue failures");
        check(info->total_call_item_failures == 0u, "smp queue reports no item failures");
    } else {
        skip("smp AP work execution requires more than one started CPU");
        skip("smp AP queue telemetry requires more than one started CPU");
        skip("smp AP queue failure telemetry requires more than one started CPU");
    }
    suite_end();
}
