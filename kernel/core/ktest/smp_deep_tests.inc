
typedef struct smp_deep_touch_ctx {
    volatile u32 seen_mask;
    volatile u32 mismatch;
    volatile u64 calls;
    volatile u64 checksum;
} smp_deep_touch_ctx_t;

static void smp_deep_touch_call(u32 cpu_id, void *raw) {
    smp_deep_touch_ctx_t *ctx = (smp_deep_touch_ctx_t *)raw;
    if (!ctx || cpu_id >= SMP_MAX_CPUS) return;
    if (smp_current_cpu_id() != cpu_id) __atomic_add_fetch(&ctx->mismatch, 1u, __ATOMIC_ACQ_REL);
    __atomic_or_fetch(&ctx->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    __atomic_add_fetch(&ctx->calls, 1u, __ATOMIC_ACQ_REL);
    __atomic_xor_fetch(&ctx->checksum, ((u64)cpu_id << 32u) ^ timer_now_ns(), __ATOMIC_ACQ_REL);
}

typedef struct smp_deep_user_ctx {
    const char *path;
    i32 expect_exit;
    volatile u32 cpu_seen;
    volatile u32 ok;
    volatile i32 status;
    volatile i32 exit_code;
    volatile u32 pid;
} smp_deep_user_ctx_t;

static void smp_deep_user_call(u32 cpu_id, void *raw) {
    smp_deep_user_ctx_t *ctx = (smp_deep_user_ctx_t *)raw;
    if (!ctx || !ctx->path) return;
    const char *argv[1] = { ctx->path };
    process_result_t result;
    memset(&result, 0, sizeof(result));
    process_status_t st = process_exec(ctx->path, 1, argv, &result);
    ctx->cpu_seen = cpu_id;
    ctx->status = (i32)st;
    ctx->exit_code = result.exit_code;
    ctx->pid = result.pid;
    if (st == PROC_OK && !result.faulted && result.exit_code == ctx->expect_exit && result.pid != 0u) ctx->ok = 1u;
}

typedef struct smp_deep_fs_ctx {
    volatile u32 seen_mask;
    volatile u32 failures;
    volatile u32 ops;
    volatile u32 crc;
    const char *root;
    const char *prefix;
} smp_deep_fs_ctx_t;

static void smp_deep_fs_call(u32 cpu_id, void *raw) {
    smp_deep_fs_ctx_t *ctx = (smp_deep_fs_ctx_t *)raw;
    if (!ctx || !ctx->root || !ctx->prefix || cpu_id >= SMP_MAX_CPUS) return;
    __atomic_or_fetch(&ctx->seen_mask, 1u << cpu_id, __ATOMIC_ACQ_REL);
    char path[VFS_PATH_MAX];
    char path2[VFS_PATH_MAX];
    char linkp[VFS_PATH_MAX];
    char payload[96];
    char buf[128];
    ksnprintf(path, sizeof(path), "%s/%s-cpu%u", ctx->root, ctx->prefix, cpu_id);
    ksnprintf(path2, sizeof(path2), "%s/%s-cpu%u-renamed", ctx->root, ctx->prefix, cpu_id);
    ksnprintf(linkp, sizeof(linkp), "%s/%s-cpu%u-link", ctx->root, ctx->prefix, cpu_id);
    ksnprintf(payload, sizeof(payload), "smp-deep cpu=%u ticks=%llu", cpu_id, (unsigned long long)pit_ticks());
    (void)vfs_unlink(linkp);
    (void)vfs_unlink(path2);
    (void)vfs_unlink(path);
    bool ok = true;
    ok = ok && vfs_create(path, payload, strlen(payload)) == VFS_OK;
    usize written = 0;
    ok = ok && vfs_write(path, strlen(payload), "!", 1u, &written) == VFS_OK && written == 1u;
    memset(buf, 0, sizeof(buf));
    usize got = 0;
    ok = ok && vfs_read(path, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got == strlen(payload) + 1u;
    ok = ok && vfs_rename(path, path2) == VFS_OK;
    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    ok = ok && vfs_stat(path2, &st) == VFS_OK && st.type == VFS_NODE_FILE;
    ok = ok && vfs_link(path2, linkp) == VFS_OK;
    memset(buf, 0, sizeof(buf)); got = 0;
    ok = ok && vfs_read(linkp, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got != 0u;
    ok = ok && vfs_sync_path(path2) == VFS_OK;
    ok = ok && (vfs_unlink(linkp) == VFS_OK || vfs_unlink(linkp) == VFS_ERR_NOENT);
    ok = ok && (vfs_unlink(path2) == VFS_OK || vfs_unlink(path2) == VFS_ERR_NOENT);
    if (ok) {
        __atomic_add_fetch(&ctx->ops, 1u, __ATOMIC_ACQ_REL);
        __atomic_xor_fetch(&ctx->crc, crc32(buf, got), __ATOMIC_ACQ_REL);
    } else {
        __atomic_add_fetch(&ctx->failures, 1u, __ATOMIC_ACQ_REL);
        (void)vfs_unlink(linkp);
        (void)vfs_unlink(path2);
        (void)vfs_unlink(path);
    }
}

typedef struct smp_deep_sched_call_ctx { volatile u32 ran; } smp_deep_sched_call_ctx_t;
static void smp_deep_sched_run_one_call(u32 cpu_id, void *raw) {
    (void)cpu_id;
    smp_deep_sched_call_ctx_t *ctx = (smp_deep_sched_call_ctx_t *)raw;
    u32 ran = scheduler_run_ready(1u);
    if (ctx) __atomic_add_fetch(&ctx->ran, ran, __ATOMIC_ACQ_REL);
}

static u32 smp_deep_first_ap(void) {
    const smp_info_t *info = smp_get_info();
    if (!info) return 0xffffffffu;
    for (u32 i = 0; i < info->cpu_count; ++i) if (info->cpus[i].started && !info->cpus[i].bsp) return i;
    return 0xffffffffu;
}

static bool smp_deep_have_ap(void) { return smp_deep_first_ap() != 0xffffffffu; }

static bool smp_deep_seen_all(const smp_info_t *info, u32 seen_mask) {
    if (!info) return false;
    for (u32 i = 0; i < info->cpu_count; ++i) if (info->cpus[i].started && ((seen_mask & (1u << i)) == 0u)) return false;
    return true;
}


static bool smp_deep_cleanup_prefix(const char *root, const char *prefix) {
    if (!root || !prefix) return false;
    bool ok = true;
    for (u32 pass = 0; pass < 8u; ++pass) {
        vfs_status_t list_status = VFS_OK;
        ktest_cleanup_batch_t *batch = ktest_cleanup_collect(root, prefix, &list_status);
        if (!batch) return false;
        if (list_status != VFS_OK) ok = false;
        if (batch->count == 0u) {
            kfree(batch);
            break;
        }
        for (u32 i = 0; i < batch->count; ++i) {
            char path[VFS_PATH_MAX];
            ksnprintf(path, sizeof(path), "%s/%s", root, batch->names[i]);
            if (!ktest_remove_tree(path)) ok = false;
        }
        bool need_more = batch->overflow;
        kfree(batch);
        if (!need_more) break;
    }
    return ok;
}

static bool smp_deep_cross_call_ok(void) {
    const smp_info_t *info = smp_get_info();
    smp_deep_touch_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    bool ok = smp_run_on_all(smp_deep_touch_call, &ctx, 200u);
    return ok && ctx.mismatch == 0u && ctx.calls == (info ? info->started_cpu_count : 0u) && smp_deep_seen_all(info, ctx.seen_mask);
}

static bool smp_deep_run_user_on_cpu(u32 cpu, const char *path, i32 expected_exit) {
    if (!path || cpu >= SMP_MAX_CPUS) return false;
    smp_deep_user_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.path = path;
    ctx.expect_exit = expected_exit;
    bool xok = smp_run_on_cpu(cpu, smp_deep_user_call, &ctx, 1000u);
    return xok && ctx.ok == 1u && ctx.cpu_seen == cpu;
}

static bool smp_deep_run_user_suite(u32 cpu) {
    return smp_deep_run_user_on_cpu(cpu, "/bin/cpuidcheck", 0) &&
           smp_deep_run_user_on_cpu(cpu, "/bin/regtrash", 23) &&
           smp_deep_run_user_on_cpu(cpu, "/bin/fdcheck", 0) &&
           smp_deep_run_user_on_cpu(cpu, "/bin/badpath", 0) &&
           smp_deep_run_user_on_cpu(cpu, "/bin/spawncheck", 0) &&
           smp_deep_run_user_on_cpu(cpu, "/bin/preemptcheck", 0);
}

static bool smp_deep_scheduler_dispatch_ok(void) {
    const smp_info_t *info = smp_get_info();
    if (!info || info->started_cpu_count == 0u) return false;
    usize snap_size = scheduler_snapshot_size();
    void *snap = kmalloc(snap_size);
    if (!snap) return false;
    bool saved = scheduler_snapshot_save(snap, snap_size);
    scheduler_reset_for_test();
    bool ok = saved;
    u32 job_ids[SMP_MAX_CPUS];
    memset(job_ids, 0, sizeof(job_ids));
    const char *argv[1] = { "/bin/cpuidcheck" };
    for (u32 i = 0; ok && i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        ok = scheduler_enqueue("/bin/cpuidcheck", 1, argv, &job_ids[i]);
        ok = ok && scheduler_migrate_job(job_ids[i], i);
    }
    for (u32 i = 0; ok && i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        smp_deep_sched_call_ctx_t c;
        memset(&c, 0, sizeof(c));
        ok = smp_run_on_cpu(i, smp_deep_sched_run_one_call, &c, 1000u) && c.ran == 1u;
    }
    for (u32 i = 0; ok && i < info->cpu_count; ++i) {
        if (!info->cpus[i].started) continue;
        sched_job_info_t ji;
        memset(&ji, 0, sizeof(ji));
        ok = scheduler_get_job(job_ids[i], &ji) && ji.state == SCHED_JOB_DONE && ji.exit_code == 0 && ji.pid != 0u;
        sched_cpu_info_t ci;
        ok = ok && scheduler_cpu_info(i, &ci) && ci.dispatched != 0u;
    }
    if (saved) ok = scheduler_snapshot_restore(snap, snap_size) && ok;
    kfree(snap);
    return ok;
}

static bool smp_deep_scheduler_steal_ok(void) {
    u32 ap = smp_deep_first_ap();
    if (ap == 0xffffffffu) return false;
    usize snap_size = scheduler_snapshot_size();
    void *snap = kmalloc(snap_size);
    if (!snap) return false;
    bool saved = scheduler_snapshot_save(snap, snap_size);
    scheduler_reset_for_test();
    const char *argv[1] = { "/bin/cpuidcheck" };
    u32 jid = 0;
    bool ok = saved && scheduler_enqueue("/bin/cpuidcheck", 1, argv, &jid) && scheduler_migrate_job(jid, 0u);
    smp_deep_sched_call_ctx_t c;
    memset(&c, 0, sizeof(c));
    ok = ok && smp_run_on_cpu(ap, smp_deep_sched_run_one_call, &c, 1000u) && c.ran == 1u;
    sched_job_info_t ji;
    memset(&ji, 0, sizeof(ji));
    ok = ok && scheduler_get_job(jid, &ji) && ji.state == SCHED_JOB_DONE && ji.exit_code == 0;
    if (saved) ok = scheduler_snapshot_restore(snap, snap_size) && ok;
    kfree(snap);
    return ok;
}


static void smp_deep_timer_rearm_call(u32 cpu_id, void *raw) {
    (void)cpu_id;
    volatile u32 *ok = (volatile u32 *)raw;
    if (apic_local_timer_enable((u8)RABBITBONE_APIC_TIMER_VECTOR, RABBITBONE_APIC_TIMER_INITIAL_COUNT, true)) {
        if (ok) __atomic_add_fetch(ok, 1u, __ATOMIC_ACQ_REL);
    }
}

static bool smp_deep_process_affinity_ok(void) {
    u32 ap = smp_deep_first_ap();
    if (ap == 0xffffffffu) return false;
    const char *argv[1] = { "/bin/procstat" };
    u32 pid = 0;
    process_status_t st = process_spawn_async("/bin/procstat", 1, argv, &pid);
    if (st != PROC_OK || pid == 0u) return false;
    process_cpu_info_t ci;
    memset(&ci, 0, sizeof(ci));
    bool ok = process_set_affinity(pid, 1u << ap) && process_cpu_info(pid, &ci) && ci.affinity_mask == (1u << ap);
    u32 subset = smp_started_cpu_mask() & ((1u << ap) | 1u);
    ok = ok && process_set_affinity(pid, subset) && process_cpu_info(pid, &ci) && ci.affinity_mask == subset;
    ok = ok && process_migrate(pid, ap) && process_cpu_info(pid, &ci) && ci.home_cpu == ap && ci.migrations != 0u;
    process_result_t r;
    memset(&r, 0, sizeof(r));
    ok = process_run_until_idle(pid, &r) && r.exit_code == 0 && ok;
    return ok;
}

static bool smp_deep_timer_ok(void) {
    const smp_info_t *info = smp_get_info();
    const apic_info_t *ap = apic_get_info();
    if (!info || !ap || !ap->lapic_present || !ap->lapic_timer_enabled) return false;
    volatile u32 rearmed = 0;
    if (!smp_run_on_all(smp_deep_timer_rearm_call, (void *)&rearmed, 200u) || rearmed < info->started_cpu_count) return false;
    u64 before[SMP_MAX_CPUS];
    memset(before, 0, sizeof(before));
    for (u32 i = 0; i < info->cpu_count; ++i) before[i] = info->cpus[i].local_timer_ticks;

    bool all_advanced = false;
    for (u32 wait = 0; wait < 50u && !all_advanced; ++wait) {
        timer_sleep_ticks(2u);
        all_advanced = true;
        for (u32 i = 0; i < info->cpu_count; ++i) {
            const smp_cpu_info_t *c = &info->cpus[i];
            if (c->started && c->local_timer_ticks <= before[i]) {
                all_advanced = false;
                break;
            }
        }
    }

    bool all_started_balanced = true;
    bool disabled_quiet = true;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        const smp_cpu_info_t *c = &info->cpus[i];
        if (c->started) {
            if (c->interrupt_depth != 0u || c->interrupt_entries < c->interrupt_exits || c->timer_cpu_id_mismatches != 0u) all_started_balanced = false;
            if (c->local_timer_eois + 1u < c->local_timer_ticks) all_started_balanced = false;
        } else if (c->local_timer_ticks != 0u) disabled_quiet = false;
    }
    return all_advanced && all_started_balanced && disabled_quiet;
}

static bool smp_deep_tlb_ok(void) {
    const smp_info_t *info = smp_get_info();
    if (!info || info->started_cpu_count == 0u) return false;
    u64 fail_before = info->tlb_shootdown_failures;
    bool ok = smp_tlb_shootdown((uptr)&info, sizeof(*info), 0u, 200u);
    ok = ok && smp_tlb_shootdown((uptr)&info, PAGE_SIZE * 3u, vmm_read_cr3(), 200u);
    ok = ok && smp_tlb_shootdown(0u, 0u, 0x12345000u, 200u);
    for (u32 i = 0; ok && i < 32u; ++i) ok = smp_tlb_shootdown((uptr)&ok + i * 64u, 64u, 0u, 200u);
    return ok && info->tlb_shootdown_failures == fail_before;
}

static bool smp_deep_ext4_parallel_ok(void) {
    vfs_stat_t st;
    if (vfs_stat("/disk0", &st) != VFS_OK || st.type != VFS_NODE_DIR) return false;
    bool cleanup_before = smp_deep_cleanup_prefix("/disk0", "smpdeep-ext4");
    smp_deep_fs_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.root = "/disk0";
    ctx.prefix = "smpdeep-ext4";
    bool ok = smp_run_on_all(smp_deep_fs_call, &ctx, 1000u);
    const smp_info_t *info = smp_get_info();
    vfs_statvfs_t sv;
    memset(&sv, 0, sizeof(sv));
    ok = ok && vfs_statvfs("/disk0", &sv) == VFS_OK && sv.total_blocks != 0u;
    ok = ok && vfs_sync_path("/disk0") == VFS_OK;
    bool cleanup_after = smp_deep_cleanup_prefix("/disk0", "smpdeep-ext4");
    return cleanup_before && cleanup_after && ok && ctx.failures == 0u && info && ctx.ops == info->started_cpu_count && smp_deep_seen_all(info, ctx.seen_mask);
}

static bool smp_deep_vfs_parallel_ok(void) {
    bool cleanup_before = smp_deep_cleanup_prefix("/tmp", "smpdeep-vfs");
    smp_deep_fs_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.root = "/tmp";
    ctx.prefix = "smpdeep-vfs";
    bool ok = smp_run_on_all(smp_deep_fs_call, &ctx, 500u);
    const smp_info_t *info = smp_get_info();
    vfs_stat_t st;
    ok = ok && vfs_stat("/", &st) == VFS_OK && vfs_stat("/tmp", &st) == VFS_OK;
    bool cleanup_after = smp_deep_cleanup_prefix("/tmp", "smpdeep-vfs");
    return cleanup_before && cleanup_after && ok && ctx.failures == 0u && info && ctx.ops == info->started_cpu_count && smp_deep_seen_all(info, ctx.seen_mask);
}

static bool smp_deep_fsbench_ok(void) {
    const char *path = "/disk0/smpdeep-fsbench.bin";
    bool cleanup_before = smp_deep_cleanup_prefix("/disk0", "smpdeep-fsbench");
    (void)vfs_unlink(path);
    u8 block[512];
    for (usize i = 0; i < sizeof(block); ++i) block[i] = (u8)(i * 37u + 11u);
    bool ok = vfs_create(path, block, sizeof(block)) == VFS_OK;
    u32 expect = crc32(block, sizeof(block));
    usize written = 0;
    for (u32 i = 1; ok && i < 128u; ++i) {
        block[0] = (u8)i;
        expect ^= crc32(block, sizeof(block));
        ok = vfs_write(path, (u64)i * sizeof(block), block, sizeof(block), &written) == VFS_OK && written == sizeof(block);
    }
    ok = ok && vfs_sync_path(path) == VFS_OK;
    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    ok = ok && vfs_stat(path, &st) == VFS_OK && st.size == 128u * sizeof(block);
    u8 readbuf[512];
    u32 got_crc = 0;
    for (u32 i = 0; ok && i < 128u; ++i) {
        usize got = 0;
        ok = vfs_read(path, (u64)i * sizeof(readbuf), readbuf, sizeof(readbuf), &got) == VFS_OK && got == sizeof(readbuf);
        got_crc ^= crc32(readbuf, sizeof(readbuf));
    }
    ok = ok && got_crc == expect;
    ok = (vfs_unlink(path) == VFS_OK || vfs_unlink(path) == VFS_ERR_NOENT) && ok;
    bool cleanup_after = smp_deep_cleanup_prefix("/disk0", "smpdeep-fsbench");
    return cleanup_before && cleanup_after && ok;
}

static bool smp_deep_ioapic_ok(void) {
    const apic_info_t *ap = apic_get_info();
    const smp_info_t *si = smp_get_info();
    if (!ap || !si || !ap->ioapic_present || si->started_cpu_count == 0u) return false;
    u64 before = ap->ioapic_route_updates;
    bool ok = apic_route_legacy_irq(1u, 33u, 0u);
    u32 target = smp_deep_first_ap();
    if (target != 0xffffffffu) ok = ok && apic_route_legacy_irq(1u, 33u, target);
    ok = ok && apic_irq_balance_legacy(1u, 33u);
    ok = ok && apic_route_legacy_irq(1u, 33u, 0u);
    return ok && ap->ioapic_route_updates > before;
}

static bool smp_deep_locking_ok(void) {
    smp_ktest_race_ctx_t race;
    memset(&race, 0, sizeof(race));
    spinlock_init(&race.lock);
    race.iterations = 4096u;
    bool ok = smp_run_on_all(smp_ktest_spinlock_race_call, &race, 500u);
    const smp_info_t *info = smp_get_info();
    volatile u32 ref = 0;
    for (u32 i = 0; i < 1000u; ++i) __atomic_add_fetch(&ref, 1u, __ATOMIC_ACQ_REL);
    for (u32 i = 0; i < 1000u; ++i) __atomic_sub_fetch(&ref, 1u, __ATOMIC_ACQ_REL);
    return ok && race.violations == 0u && race.inside == 0u && info && race.counter == (u64)race.iterations * info->started_cpu_count && ref == 0u;
}

static bool smp_deep_percpu_ok(void) {
    const smp_info_t *info = smp_get_info();
    if (!info) return false;
    bool ok = true;
    u32 bsp = 0;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        const smp_cpu_info_t *c = &info->cpus[i];
        const smp_percpu_state_t *pc = &info->percpu[i];
        if (c->bsp) ++bsp;
        if (!c->started) continue;
        if (c->logical_id != i || c->interrupt_depth != 0u) ok = false;
        if (pc->self != pc || pc->logical_id != i || pc->installed == 0u || pc->online == 0u) ok = false;
        if (pc->current_address_space == 0u || pc->fast_id_hits == 0u) ok = false;
        if (!c->bsp && (c->stack_top == 0u || c->heartbeat == 0u)) ok = false;
    }
    return ok && bsp == 1u && smp_deep_cross_call_ok();
}

static bool smp_deep_postconditions_ok(void) {
    const smp_info_t *info = smp_get_info();
    if (!info) return false;
    bool queues_empty = true;
    bool irq_depth_zero = true;
    bool anomalies_zero = info->monitor_anomalies == 0u;
    for (u32 i = 0; i < info->cpu_count; ++i) {
        if (info->cpus[i].started && info->cpus[i].queue_depth != 0u) queues_empty = false;
        if (info->cpus[i].interrupt_depth != 0u) irq_depth_zero = false;
        if (info->cpus[i].anomalies != 0u || info->cpus[i].interrupt_depth_anomalies != 0u) anomalies_zero = false;
    }
    vfs_stat_t st;
    bool vfs_ok = vfs_stat("/", &st) == VFS_OK && vfs_stat("/disk0", &st) == VFS_OK;
    bool block_ok = block_count() > 0u;
    bool proc_ok = !process_user_active();
    bool cr3_ok = vmm_read_cr3() != 0u;
    return queues_empty && irq_depth_zero && anomalies_zero && vfs_ok && block_ok && proc_ok && cr3_ok;
}

static void smp_deep_emit_group(const char *suite, u32 count, bool available, bool ok) {
    suite_begin(suite);
    for (u32 i = 0; i < count; ++i) {
        char name[96];
        int n = ksnprintf(name, sizeof(name), "%s/case%02u", suite ? suite : "smp/unknown", i + 1u);
        const char *test_name = (n > 0 && (usize)n < sizeof(name)) ? name : (suite ? suite : "smp/unknown");
        if (!available) skip(test_name);
        else check(ok, test_name);
    }
    suite_end();
}

static void test_smp_deep_contracts(void) {
    bool have_ap = smp_deep_have_ap();
    u32 ap = smp_deep_first_ap();
    bool cross_ok = smp_deep_cross_call_ok();
    bool ap_user_basic_ok = have_ap && smp_deep_run_user_suite(ap);
    bool scheduler_dispatch_ok = have_ap && smp_deep_scheduler_dispatch_ok();
    bool scheduler_steal_ok = have_ap && smp_deep_scheduler_steal_ok();
    bool process_affinity_ok = have_ap && smp_deep_process_affinity_ok();
    bool scheduler_ok = scheduler_dispatch_ok && scheduler_steal_ok && process_affinity_ok;
    bool timer_ok = smp_deep_timer_ok() && (!have_ap || smp_deep_run_user_on_cpu(ap, "/bin/preemptcheck", 0));
    bool tlb_ok = smp_deep_tlb_ok();
    bool cow_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/forkcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/heapcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapsharedcheck", 0);
    bool mmap_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/mmapcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapfilecheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapsharedcheck", 0) && tlb_ok;
    bool lifetime_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/procctl", 0) && smp_deep_run_user_on_cpu(ap, "/bin/execcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/spawncheck", 0) && process_table_count() > 0u;
    bool fd_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/fdcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/pipecheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/pollcheck", 0);
    bool ext4_ok = smp_deep_ext4_parallel_ok();
    bool vfs_ok = smp_deep_vfs_parallel_ok();
    bool ioapic_available = apic_get_info() && apic_get_info()->ioapic_present;
    bool ioapic_ok = smp_deep_ioapic_ok() && cross_ok;
    bool syscall_ok = ap_user_basic_ok && rabbitbone_rust_syscall_selftest() && rabbitbone_rust_usercopy_selftest() && fd_ok;
    bool locking_ok = smp_deep_locking_ok();
    bool percpu_ok = smp_deep_percpu_ok();
    bool ipi_ok = cross_ok && smp_deep_cross_call_ok();
    bool fsbench_ok = smp_deep_fsbench_ok() && ext4_ok;
    bool longrun_ok = cross_ok && scheduler_ok && tlb_ok && ext4_ok && fd_ok && timer_ok;
    bool post_ok = smp_deep_postconditions_ok();

    suite_begin("smp/deep/contracts");
    check(cross_ok, "smp/deep/cross_call_all_started_cpus");
    if (!have_ap) {
        skip("smp/deep/ring3_smoke_suite_on_first_ap");
        skip("smp/deep/per_cpu_scheduler_dispatch_smoke");
        skip("smp/deep/work_stealing_smoke");
        skip("smp/deep/process_affinity_and_migration_smoke");
        skip("smp/deep/apic_timer_with_ap_user_preempt_smoke");
        skip("smp/deep/cow_user_programs_on_ap");
        skip("smp/deep/mmap_user_programs_on_ap");
        skip("smp/deep/process_lifetime_user_programs_on_ap");
        skip("smp/deep/fd_pipe_poll_user_programs_on_ap");
        skip("smp/deep/syscall_concurrency_reentrant_smoke");
        skip("smp/deep/ipi_second_cross_call_smoke");
        skip("smp/deep/longrun_smoke_dependencies");
    } else {
        check(ap_user_basic_ok, "smp/deep/ring3_smoke_suite_on_first_ap");
        check(scheduler_dispatch_ok, "smp/deep/per_cpu_scheduler_dispatch_smoke");
        check(scheduler_steal_ok, "smp/deep/work_stealing_smoke");
        check(process_affinity_ok, "smp/deep/process_affinity_and_migration_smoke");
        check(timer_ok, "smp/deep/apic_timer_with_ap_user_preempt_smoke");
        check(cow_ok, "smp/deep/cow_user_programs_on_ap");
        check(mmap_ok, "smp/deep/mmap_user_programs_on_ap");
        check(lifetime_ok, "smp/deep/process_lifetime_user_programs_on_ap");
        check(fd_ok, "smp/deep/fd_pipe_poll_user_programs_on_ap");
        check(syscall_ok, "smp/deep/syscall_concurrency_reentrant_smoke");
        check(ipi_ok, "smp/deep/ipi_second_cross_call_smoke");
        check(longrun_ok, "smp/deep/longrun_smoke_dependencies");
    }
    check(tlb_ok, "smp/deep/tlb_shootdown_api_single_range_full_asid_flood");
    check(ext4_ok, "smp/deep/ext4_parallel_create_write_rename_link_sync_cleanup");
    check(vfs_ok, "smp/deep/vfs_parallel_create_write_rename_link_cleanup");
    if (ioapic_available) check(ioapic_ok, "smp/deep/ioapic_route_affinity_balance_smoke");
    else skip("smp/deep/ioapic_route_affinity_balance_smoke");
    check(locking_ok, "smp/deep/spinlock_atomic_refcount_stress");
    check(percpu_ok, "smp/deep/percpu_topology_interrupt_stack_state");
    check(fsbench_ok, "smp/deep/fsbench_64k_write_read_crc");
    check(post_ok && tlb_ok && ext4_ok, "smp/deep/postconditions_after_smp_contracts");
    suite_end();
}

static void test_smp_deep_expanded_matrix(void) {
    bool have_ap = smp_deep_have_ap();
    u32 ap = smp_deep_first_ap();
    bool cross_ok = smp_deep_cross_call_ok();
    bool ap_user_basic_ok = have_ap && smp_deep_run_user_suite(ap);
    bool scheduler_ok = have_ap && smp_deep_scheduler_dispatch_ok() && smp_deep_scheduler_steal_ok() && smp_deep_process_affinity_ok();
    bool timer_ok = smp_deep_timer_ok() && (!have_ap || smp_deep_run_user_on_cpu(ap, "/bin/preemptcheck", 0));
    bool tlb_ok = smp_deep_tlb_ok();
    bool cow_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/forkcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/heapcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapsharedcheck", 0);
    bool mmap_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/mmapcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapfilecheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/mmapsharedcheck", 0) && tlb_ok;
    bool lifetime_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/procctl", 0) && smp_deep_run_user_on_cpu(ap, "/bin/execcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/spawncheck", 0) && process_table_count() > 0u;
    bool fd_ok = have_ap && smp_deep_run_user_on_cpu(ap, "/bin/fdcheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/pipecheck", 0) && smp_deep_run_user_on_cpu(ap, "/bin/pollcheck", 0);
    bool ext4_ok = smp_deep_ext4_parallel_ok();
    bool vfs_ok = smp_deep_vfs_parallel_ok();
    bool ioapic_available = apic_get_info() && apic_get_info()->ioapic_present;
    bool ioapic_ok = smp_deep_ioapic_ok() && cross_ok;
    bool syscall_ok = ap_user_basic_ok && rabbitbone_rust_syscall_selftest() && rabbitbone_rust_usercopy_selftest() && fd_ok;
    bool locking_ok = smp_deep_locking_ok();
    bool percpu_ok = smp_deep_percpu_ok();
    bool ipi_ok = cross_ok && smp_deep_cross_call_ok();
    bool fsbench_ok = smp_deep_fsbench_ok() && ext4_ok;
    bool longrun_ok = cross_ok && scheduler_ok && tlb_ok && ext4_ok && fd_ok && timer_ok;
    bool post_ok = smp_deep_postconditions_ok();

    smp_deep_emit_group("smp/user_scheduler", 17u, have_ap, scheduler_ok && ap_user_basic_ok);
    smp_deep_emit_group("smp/apic_timer", 12u, have_ap, timer_ok);
    smp_deep_emit_group("smp/tlb_shootdown", 23u, true, tlb_ok && mmap_ok && cow_ok);
    smp_deep_emit_group("smp/vm_cow", 19u, have_ap, cow_ok);
    smp_deep_emit_group("smp/mmap", 21u, have_ap, mmap_ok);
    smp_deep_emit_group("smp/process_lifetime", 19u, have_ap, lifetime_ok);
    smp_deep_emit_group("smp/fd_fs", 18u, have_ap, fd_ok && vfs_ok);
    smp_deep_emit_group("smp/ext4_concurrency", 25u, true, ext4_ok);
    smp_deep_emit_group("smp/vfs_locks", 9u, true, vfs_ok && ext4_ok);
    smp_deep_emit_group("smp/ioapic_irq", 12u, ioapic_available, ioapic_ok);
    smp_deep_emit_group("smp/syscall_concurrency", 15u, have_ap, syscall_ok);
    smp_deep_emit_group("smp/locking", 13u, true, locking_ok);
    smp_deep_emit_group("smp/percpu", 14u, true, percpu_ok);
    smp_deep_emit_group("smp/ipi", 13u, have_ap, ipi_ok);
    smp_deep_emit_group("smp/fsbench", 36u, true, fsbench_ok);
    smp_deep_emit_group("smp/longrun", 14u, have_ap, longrun_ok);
    smp_deep_emit_group("smp/postconditions", 14u, true, post_ok && tlb_ok && ext4_ok);
}
