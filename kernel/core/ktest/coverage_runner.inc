static void test_coverage_contracts(void) {
    suite_begin("coverage/contracts/postconditions");
    bool syscall_names_ok = true;
    for (u64 no = 0; no < AURORA_SYS_MAX; ++no) {
        if (strcmp(syscall_name(no), "unknown") == 0) syscall_names_ok = false;
    }
    check(syscall_names_ok && strcmp(syscall_name(AURORA_SYS_MAX), "unknown") == 0, "all syscall IDs have stable Rust names");
    check(aurora_rust_syscall_selftest(), "Rust syscall validator remains linked and callable");
    check(aurora_rust_vfs_route_selftest(), "Rust VFS router remains linked and callable");
    check(aurora_rust_path_policy_selftest(), "Rust path policy remains linked and callable");
    check(aurora_rust_usercopy_selftest(), "Rust usercopy checker remains linked and callable");
    check(!process_user_active(), "no active user process left after userland tests");
    check(vmm_current_space() == vmm_kernel_space(), "kernel CR3 space restored after all userland tests");
    vmm_stats_t vs;
    vmm_get_stats(&vs);
    check(vs.current_pml4_physical == vs.pml4_physical && vmm_read_cr3() == vs.pml4_physical, "hardware CR3 restored to kernel pml4");
    check(block_count() >= 1 && block_get(0) != 0, "block registry still valid after tests");
    check(vfs_stat("/tmp", &(vfs_stat_t){0}) == VFS_OK, "root ramfs /tmp still reachable after tests");
    suite_end();
}

bool ktest_run_all(void) {
    memset(&totals, 0, sizeof(totals));
    kprintf("\n%s started\n", AURORA_KTEST_TITLE);
    u64 start = pit_ticks();
    test_libc_bitmap_ring_crc();
    test_boot_memory_vmm_heap();
    test_vfs_ramfs_devfs();
    test_tarfs_module();
    test_block_mbr_ext4();
    test_syscall_task_timer_elf();
    test_user_processes();
    test_process_registry();
    test_scheduler_contracts();
    test_coverage_contracts();
    u64 end = pit_ticks();
    kprintf("\n%s finished: suites=%u passed=%u failed=%u skipped=%u ticks=%llu\n", AURORA_KTEST_TITLE,
            totals.suites, totals.passed, totals.failed, totals.skipped, (unsigned long long)(end - start));
    if (totals.failed == 0) kprintf("KTEST_STATUS: PASS\n");
    else kprintf("KTEST_STATUS: FAIL\n");
    return totals.failed == 0;
}

void ktest_get_last_totals(ktest_totals_t *out) {
    if (out) *out = totals;
}
