static void test_coverage_contracts(void) {
    suite_begin("coverage/contracts/postconditions");
    bool syscall_names_ok = true;
    for (u64 no = 0; no < RABBITBONE_SYS_MAX; ++no) {
        if (strcmp(syscall_name(no), "unknown") == 0) syscall_names_ok = false;
    }
    check(syscall_names_ok && strcmp(syscall_name(RABBITBONE_SYS_MAX), "unknown") == 0, "all syscall IDs have stable Rust names");
    check(rabbitbone_rust_syscall_selftest(), "Rust syscall validator remains linked and callable");
    check(rabbitbone_rust_vfs_route_selftest(), "Rust VFS router remains linked and callable");
    check(rabbitbone_rust_path_policy_selftest(), "Rust path policy remains linked and callable");
    check(rabbitbone_rust_usercopy_selftest(), "Rust usercopy checker remains linked and callable");
    check(!process_user_active(), "no active user process left after userland tests");
    check(vmm_current_space() == vmm_kernel_space(), "kernel CR3 space restored after all userland tests");
    vmm_stats_t vs;
    vmm_get_stats(&vs);
    check(vs.current_pml4_physical == vs.pml4_physical && vmm_read_cr3() == vs.pml4_physical, "hardware CR3 restored to kernel pml4");
    check(block_count() >= 1 && block_get(0) != 0, "block registry still valid after tests");
    check(vfs_stat("/tmp", &(vfs_stat_t){0}) == VFS_OK, "root ramfs /tmp still reachable after tests");
    suite_end();
}

bool ktest_run_all_flags(u32 flags) {
    memset(&totals, 0, sizeof(totals));
    ktest_verbose_passes = false;
    kprintf("\n%s started\n", RABBITBONE_KTEST_TITLE);
    KLOG(LOG_INFO, "ktest", "%s started", RABBITBONE_KTEST_TITLE);
    u64 start = pit_ticks();
    test_libc_bitmap_ring_crc();
    test_boot_memory_vmm_heap();
    test_vfs_ramfs_devfs();
    test_tarfs_module();
    test_hardening_regressions();
    test_block_mbr_ext4();
    test_syscall_task_timer_elf();
    test_user_processes();
    test_process_registry();
    test_scheduler_contracts();
    test_smp_contracts();
    test_smp_deep_contracts();
    if ((flags & RABBITBONE_KTEST_FLAG_SMP_EXPANDED) != 0u) test_smp_deep_expanded_matrix();
    test_coverage_contracts();
    u64 end = pit_ticks();
    kprintf("\n%s finished: suites=%u passed=%u failed=%u skipped=%u ticks=%llu\n", RABBITBONE_KTEST_TITLE,
            totals.suites, totals.passed, totals.failed, totals.skipped, (unsigned long long)(end - start));
    KLOG(totals.failed ? LOG_ERROR : LOG_INFO, "ktest", "%s finished: suites=%u passed=%u failed=%u skipped=%u ticks=%llu", RABBITBONE_KTEST_TITLE,
         totals.suites, totals.passed, totals.failed, totals.skipped, (unsigned long long)(end - start));
    if (totals.failed == 0) { kprintf("KTEST_STATUS: PASS\n"); KLOG(LOG_INFO, "ktest", "KTEST_STATUS: PASS"); }
    else { kprintf("KTEST_STATUS: FAIL\n"); KLOG(LOG_ERROR, "ktest", "KTEST_STATUS: FAIL"); }
    return totals.failed == 0;
}

void ktest_get_last_totals(ktest_totals_t *out) {
    if (out) *out = totals;
}


bool ktest_run_all(void) {
    return ktest_run_all_flags(0u);
}
