static bool ktest_create_text_file(const char *path, const char *body, u32 mode) {
    if (!path || !body) return false;
    (void)vfs_unlink(path);
    return vfs_create(path, body, strlen(body)) == VFS_OK && vfs_chmod(path, mode) == VFS_OK;
}

static void detail_process_result(const char *label, process_status_t st, const process_result_t *r) {
    if (!r) return;
    kprintf("[ detail] %s status=%s exit=%d faulted=%u pml4=%p asid=%llu entry=%p stack_top=%p pages=%llu",
            label, process_status_name(st), r->exit_code, r->faulted ? 1u : 0u,
            (void *)(uptr)r->address_space, (unsigned long long)r->address_space_generation,
            (void *)(uptr)r->entry, (void *)(uptr)r->user_stack_top,
            (unsigned long long)r->mapped_pages);
    if (r->faulted) {
        kprintf(" vector=%llu rip=%p addr=%p",
                (unsigned long long)r->fault_vector, (void *)(uptr)r->fault_rip,
                (void *)(uptr)r->fault_addr);
    }
    kprintf("\n");
}

static void test_user_processes(void) {
    suite_begin("gdt/userbin/ring3-process");
    check(gdt_selftest(), "GDT has user segments and TSS kernel stack");
    check(user_bins_selftest(), "embedded /bin ELF payloads installed in VFS");
    vfs_stat_t st;
    check(vfs_stat("/bin/hello", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/hello user ELF");
    check(vfs_stat("/bin/regtrash", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/regtrash ABI stress ELF");
    check(vfs_stat("/bin/badptr", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/badptr pointer hardening ELF");
    check(vfs_stat("/bin/badpath", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/badpath path-policy ELF");
    check(vfs_stat("/bin/statcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/statcheck syscall-surface ELF");
    check(vfs_stat("/bin/procstat", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/procstat process-info ELF");
    check(vfs_stat("/bin/spawncheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/spawncheck process-control ELF");
    check(vfs_stat("/bin/schedcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/schedcheck scheduler syscall ELF");
    check(vfs_stat("/bin/preemptcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/preemptcheck preempt syscall ELF");
    check(vfs_stat("/bin/fdcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdcheck fd syscall ELF");
    check(vfs_stat("/bin/isolate", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/isolate address-space ELF");
    check(vfs_stat("/bin/fdleak", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdleak fd-table ELF");
    check(vfs_stat("/bin/forkcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/forkcheck fork/wait ELF");
    check(vfs_stat("/bin/heapcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/heapcheck brk/sbrk malloc ELF");
    check(vfs_stat("/bin/mmapcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/mmapcheck anonymous mmap/mprotect ELF");
    check(vfs_stat("/bin/mmapfilecheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/mmapfilecheck file-backed private mmap ELF");
    check(vfs_stat("/bin/mmapsharedcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/mmapsharedcheck anonymous shared mmap ELF");
    check(vfs_stat("/bin/procctl", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/procctl async spawn/wait ELF");
    check(vfs_stat("/bin/execcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execcheck execv ELF");
    check(vfs_stat("/bin/execfdcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execfdcheck fd inheritance/CLOEXEC ELF");
    check(vfs_stat("/bin/execvecheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execvecheck env ELF");
    check(vfs_stat("/bin/exectarget", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/exectarget exec target ELF");
    check(vfs_stat("/bin/pipecheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/pipecheck IPC pipe ELF");
    check(vfs_stat("/bin/fdremapcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdremapcheck dup2 ELF");
    check(vfs_stat("/bin/pollcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/pollcheck readiness ELF");
    check(vfs_stat("/bin/stdcat", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/stdcat stdio copy ELF");
    check(vfs_stat("/bin/termcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/termcheck terminal ELF");
    elf_loaded_image_t img;
    elf_status_t es = elf64_load_from_vfs("/bin/hello", &img);
    check(es == ELF_OK && img.segment_count > 0u && img.entry >= 0x0000010000000000ull, "ELF loader parses /bin/hello");
    if (es == ELF_OK) elf64_free_image(&img);

    const char *hello_argv[] = { "/bin/hello" };
    process_result_t r;
    process_status_t ps = process_exec("/bin/hello", 1, hello_argv, &r);
    bool hello_ok = ps == PROC_OK && !r.faulted && r.exit_code == 7;
    check(hello_ok, "ring3 /bin/hello exits through int80 SYS_EXIT");
    if (!hello_ok) detail_process_result("/bin/hello", ps, &r);

    const char *reg_argv[] = { "/bin/regtrash" };
    ps = process_exec("/bin/regtrash", 1, reg_argv, &r);
    bool reg_ok = ps == PROC_OK && !r.faulted && r.exit_code == 23;
    check(reg_ok, "ring3 /bin/regtrash cannot corrupt kernel callee-saved registers");
    if (!reg_ok) detail_process_result("/bin/regtrash", ps, &r);

    const char *bad_argv[] = { "/bin/badptr" };
    ps = process_exec("/bin/badptr", 1, bad_argv, &r);
    bool bad_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(bad_ok, "ring3 bad user pointers return syscall errors without kernel fault");
    if (!bad_ok) detail_process_result("/bin/badptr", ps, &r);

    const char *badpath_argv[] = { "/bin/badpath" };
    ps = process_exec("/bin/badpath", 1, badpath_argv, &r);
    bool badpath_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(badpath_ok, "ring3 /bin/badpath exercises path-policy and cwd-relative syscalls");
    if (!badpath_ok) detail_process_result("/bin/badpath", ps, &r);

    const char *stat_argv[] = { "/bin/statcheck" };
    ps = process_exec("/bin/statcheck", 1, stat_argv, &r);
    bool stat_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(stat_ok, "ring3 /bin/statcheck exercises stat/mkdir/ticks syscalls");
    if (!stat_ok) detail_process_result("/bin/statcheck", ps, &r);

    const char *proc_argv[] = { "/bin/procstat" };
    ps = process_exec("/bin/procstat", 1, proc_argv, &r);
    bool proc_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(proc_ok, "ring3 /bin/procstat reads current process info through syscalls");
    if (!proc_ok) detail_process_result("/bin/procstat", ps, &r);

    const char *spawncheck_argv[] = { "/bin/spawncheck" };
    ps = process_exec("/bin/spawncheck", 1, spawncheck_argv, &r);
    bool spawncheck_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(spawncheck_ok, "ring3 /bin/spawncheck exercises async spawn/wait safely");
    if (!spawncheck_ok) detail_process_result("/bin/spawncheck", ps, &r);


    const char *procctl_argv[] = { "/bin/procctl" };
    ps = process_exec("/bin/procctl", 1, procctl_argv, &r);
    bool procctl_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(procctl_ok, "ring3 /bin/procctl performs async spawnv and blocking wait");
    if (!procctl_ok) detail_process_result("/bin/procctl", ps, &r);

    const char *exec_argv[] = { "/bin/execcheck" };
    ps = process_exec("/bin/execcheck", 1, exec_argv, &r);
    bool exec_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/fscheck") == 0;
    check(exec_ok, "ring3 /bin/execcheck rejects failed exec then replaces image with execv");
    if (!exec_ok) detail_process_result("/bin/execcheck", ps, &r);

    const char *execfd_argv[] = { "/bin/execfdcheck" };
    ps = process_exec("/bin/execfdcheck", 1, execfd_argv, &r);
    bool execfd_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execfd_ok, "ring3 /bin/execfdcheck preserves non-CLOEXEC fd across execv");
    if (!execfd_ok) detail_process_result("/bin/execfdcheck", ps, &r);

    const char *execve_argv[] = { "/bin/execvecheck" };
    ps = process_exec("/bin/execvecheck", 1, execve_argv, &r);
    bool execve_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execve_ok, "ring3 /bin/execvecheck replaces image with argv+envp");
    if (!execve_ok) detail_process_result("/bin/execvecheck", ps, &r);

    const char *execfdclo_argv[] = { "/bin/execfdcheck", "cloexec" };
    ps = process_exec("/bin/execfdcheck", 2, execfdclo_argv, &r);
    bool execfdclo_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execfdclo_ok, "ring3 /bin/execfdcheck closes FD_CLOEXEC handles across execv");
    if (!execfdclo_ok) detail_process_result("/bin/execfdcheck", ps, &r);

    const char *sb_basic_path = "/tmp/ktest-sb0";
    const char *sb_opt_path = "/tmp/ktest-sb1";
    const char *sb_env_path = "/tmp/ktest-sb2";
    const char *sb_exec_path = "/tmp/ktest-sb3";
    const char *sb_relative_path = "/tmp/ktest-sbr";
    const char *sb_empty_path = "/tmp/ktest-sbe";
    const char *sb_loop_path = "/tmp/ktest-sbl";
    bool sb_scripts_ok =
        ktest_create_text_file(sb_basic_path, "#!/bin/exectarget\n", 0644u) &&
        ktest_create_text_file(sb_opt_path, "#!/bin/exectarget --sb-opt\n", 0644u) &&
        ktest_create_text_file(sb_env_path, "#!/bin/exectarget --sb-env\n", 0644u) &&
        ktest_create_text_file(sb_exec_path, "#!/bin/exectarget --sb-exec\n", 0644u) &&
        ktest_create_text_file(sb_relative_path, "#!bin/exectarget\n", 0644u) &&
        ktest_create_text_file(sb_empty_path, "#!\n", 0644u) &&
        ktest_create_text_file(sb_loop_path, "#!/tmp/ktest-sbl\n", 0644u);
    check(sb_scripts_ok, "VFS creates shebang test scripts as readable non-ELF files");

    const char *sb_basic_argv[] = { sb_basic_path, "solo" };
    ps = process_exec(sb_basic_path, 2, sb_basic_argv, &r);
    bool sb_basic_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(sb_basic_ok, "ring3 shebang script runs via interpreter without optional arg");
    if (!sb_basic_ok) detail_process_result(sb_basic_path, ps, &r);

    const char *sb_opt_argv[] = { sb_opt_path, "alpha", "beta" };
    ps = process_exec(sb_opt_path, 3, sb_opt_argv, &r);
    bool sb_opt_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(sb_opt_ok, "ring3 shebang script inserts optional interpreter argument before script path");
    if (!sb_opt_ok) detail_process_result(sb_opt_path, ps, &r);

    const char *sb_env_argv[] = { sb_env_path };
    const char *sb_envp[] = { "SHEBANG_ENV=ok", "UNCHANGED=yes" };
    ps = process_execve(sb_env_path, 1, sb_env_argv, 2, sb_envp, &r);
    bool sb_env_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(sb_env_ok, "ring3 shebang execve preserves caller envp limits and values");
    if (!sb_env_ok) detail_process_result(sb_env_path, ps, &r);

    syscall_result_t sb_sv = syscall_dispatch(AURORA_SYS_SPAWNV, (u64)(uptr)sb_basic_path, 2u, (u64)(uptr)sb_basic_argv, 0, 0, 0);
    bool sb_spawnv_ok = sb_sv.error == 0 && sb_sv.value > 0;
    if (sb_spawnv_ok) {
        process_info_t info;
        sb_spawnv_ok = process_wait((u32)sb_sv.value, &info) && info.exit_code == 0 && info.state == PROCESS_STATE_EXITED && strcmp(info.name, "/bin/exectarget") == 0;
    }
    check(sb_spawnv_ok, "syscall spawnv executes readable shebang script through ELF interpreter");

    const char *sb_exec_argv[] = { "/bin/execcheck", "shebang", sb_exec_path };
    ps = process_exec("/bin/execcheck", 3, sb_exec_argv, &r);
    bool sb_exec_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(sb_exec_ok, "ring3 execv can replace current image with shebang interpreter image");
    if (!sb_exec_ok) detail_process_result("/bin/execcheck shebang", ps, &r);

    const char *sb_bad_argv[] = { sb_relative_path };
    ps = process_exec(sb_relative_path, 1, sb_bad_argv, &r);
    check(ps == PROC_ERR_FORMAT, "shebang rejects relative interpreter path");

    const char *sb_empty_argv[] = { sb_empty_path };
    ps = process_exec(sb_empty_path, 1, sb_empty_argv, &r);
    check(ps == PROC_ERR_FORMAT, "shebang rejects empty interpreter path");

    const char *sb_loop_argv[] = { sb_loop_path };
    ps = process_exec(sb_loop_path, 1, sb_loop_argv, &r);
    check(ps == PROC_ERR_RANGE, "shebang recursion limit rejects interpreter loop");

    const char *fork_argv[] = { "/bin/forkcheck" };
    memory_stats_t fork_mem_before;
    memory_stats_t fork_mem_after;
    memory_get_stats(&fork_mem_before);
    ps = process_exec("/bin/forkcheck", 1, fork_argv, &r);
    memory_get_stats(&fork_mem_after);
    bool fork_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && fork_mem_after.free_frames >= fork_mem_before.free_frames;
    check(fork_ok, "ring3 /bin/forkcheck validates COW fork isolation, fork+exec release, and non-COW text faults");
    if (!fork_ok) detail_process_result("/bin/forkcheck", ps, &r);

    const char *heap_argv[] = { "/bin/heapcheck" };
    memory_stats_t heap_mem_before;
    memory_stats_t heap_mem_after;
    memory_get_stats(&heap_mem_before);
    ps = process_exec("/bin/heapcheck", 1, heap_argv, &r);
    memory_get_stats(&heap_mem_after);
    bool heap_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && heap_mem_after.free_frames >= heap_mem_before.free_frames;
    check(heap_ok, "ring3 /bin/heapcheck validates brk/sbrk, malloc/calloc/realloc/free, and heap COW fork");
    if (!heap_ok) detail_process_result("/bin/heapcheck", ps, &r);

    const char *mmap_argv[] = { "/bin/mmapcheck" };
    memory_stats_t mmap_mem_before;
    memory_stats_t mmap_mem_after;
    memory_get_stats(&mmap_mem_before);
    ps = process_exec("/bin/mmapcheck", 1, mmap_argv, &r);
    memory_get_stats(&mmap_mem_after);
    bool mmap_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && mmap_mem_after.free_frames >= mmap_mem_before.free_frames;
    check(mmap_ok, "ring3 /bin/mmapcheck validates demand-paged anonymous mmap/munmap/mprotect, VMA splitting, and mmap COW fork");
    if (!mmap_ok) detail_process_result("/bin/mmapcheck", ps, &r);

    const char *mmapfile_argv[] = { "/bin/mmapfilecheck" };
    memory_stats_t mmapfile_mem_before;
    memory_stats_t mmapfile_mem_after;
    memory_get_stats(&mmapfile_mem_before);
    ps = process_exec("/bin/mmapfilecheck", 1, mmapfile_argv, &r);
    memory_get_stats(&mmapfile_mem_after);
    bool mmapfile_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && mmapfile_mem_after.free_frames >= mmapfile_mem_before.free_frames;
    check(mmapfile_ok, "ring3 /bin/mmapfilecheck validates demand-paged file-backed private mmap over VFS handles");
    if (!mmapfile_ok) detail_process_result("/bin/mmapfilecheck", ps, &r);

    const char *mmapshared_argv[] = { "/bin/mmapsharedcheck" };
    memory_stats_t mmapshared_mem_before;
    memory_stats_t mmapshared_mem_after;
    memory_get_stats(&mmapshared_mem_before);
    ps = process_exec("/bin/mmapsharedcheck", 1, mmapshared_argv, &r);
    memory_get_stats(&mmapshared_mem_after);
    bool mmapshared_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && mmapshared_mem_after.free_frames >= mmapshared_mem_before.free_frames;
    check(mmapshared_ok, "ring3 /bin/mmapsharedcheck validates anonymous MAP_SHARED VMAs inherited across fork");
    if (!mmapshared_ok) detail_process_result("/bin/mmapsharedcheck", ps, &r);

    const char *pipe_argv[] = { "/bin/pipecheck" };
    ps = process_exec("/bin/pipecheck", 1, pipe_argv, &r);
    bool pipe_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(pipe_ok, "ring3 /bin/pipecheck exercises pipes, stdio remap AURORA_STDIN AURORA_STDOUT AURORA_STDERR and fork+exec pipeline");
    if (!pipe_ok) detail_process_result("/bin/pipecheck", ps, &r);

    const char *fdremap_argv[] = { "/bin/fdremapcheck" };
    ps = process_exec("/bin/fdremapcheck", 1, fdremap_argv, &r);
    bool fdremap_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fdremap_ok, "ring3 /bin/fdremapcheck exercises dup2 fd remapping");
    if (!fdremap_ok) detail_process_result("/bin/fdremapcheck", ps, &r);

    const char *poll_argv[] = { "/bin/pollcheck" };
    ps = process_exec("/bin/pollcheck", 1, poll_argv, &r);
    bool poll_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(poll_ok, "ring3 /bin/pollcheck exercises pipe/file readiness");
    if (!poll_ok) detail_process_result("/bin/pollcheck", ps, &r);

    const char *stdcat_argv[] = { "/bin/stdcat", "/disk0/hello.txt" };
    ps = process_exec("/bin/stdcat", 2, stdcat_argv, &r);
    bool stdcat_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(stdcat_ok, "ring3 /bin/stdcat copies VFS file to stdout");
    if (!stdcat_ok) detail_process_result("/bin/stdcat", ps, &r);


    const char *sched_argv[] = { "/bin/schedcheck" };
    ps = process_exec("/bin/schedcheck", 1, sched_argv, &r);
    bool sched_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(sched_ok, "ring3 /bin/schedcheck exercises yield/sleep/schedinfo syscalls");
    if (!sched_ok) detail_process_result("/bin/schedcheck", ps, &r);

    const char *preempt_argv[] = { "/bin/preemptcheck" };
    ps = process_exec("/bin/preemptcheck", 1, preempt_argv, &r);
    bool preempt_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(preempt_ok, "ring3 /bin/preemptcheck reads preemption/timeslice telemetry");
    if (!preempt_ok) detail_process_result("/bin/preemptcheck", ps, &r);

    const char *fdcheck_argv[] = { "/bin/fdcheck" };
    ps = process_exec("/bin/fdcheck", 1, fdcheck_argv, &r);
    bool fdcheck_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fdcheck_ok, "ring3 /bin/fdcheck exercises dup/tell/fstat/fdinfo/readdir syscalls");
    if (!fdcheck_ok) detail_process_result("/bin/fdcheck", ps, &r);

    const char *iso_argv[] = { "/bin/isolate" };
    process_result_t iso_a;
    ps = process_exec("/bin/isolate", 1, iso_argv, &iso_a);
    bool iso_a_ok = ps == PROC_OK && !iso_a.faulted && iso_a.exit_code == 41;
    check(iso_a_ok, "first /bin/isolate gets zero-filled private BSS");
    if (!iso_a_ok) detail_process_result("/bin/isolate#1", ps, &iso_a);
    process_result_t iso_b;
    ps = process_exec("/bin/isolate", 1, iso_argv, &iso_b);
    bool iso_b_ok = ps == PROC_OK && !iso_b.faulted && iso_b.exit_code == 41 && iso_b.address_space_generation != iso_a.address_space_generation;
    check(iso_b_ok, "second /bin/isolate gets a distinct clean address space");
    if (!iso_b_ok) detail_process_result("/bin/isolate#2", ps, &iso_b);

    const char *term_argv[] = { "/bin/termcheck" };
    ps = process_exec("/bin/termcheck", 1, term_argv, &r);
    bool term_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(term_ok, "ring3 /bin/termcheck exercises TTY terminal ABI");
    if (!term_ok) detail_process_result("/bin/termcheck", ps, &r);

    const char *fd_argv[] = { "/bin/fdleak" };
    process_result_t fd_a;
    ps = process_exec("/bin/fdleak", 1, fd_argv, &fd_a);
    bool fd_a_ok = ps == PROC_OK && !fd_a.faulted && fd_a.exit_code == 0;
    check(fd_a_ok, "first /bin/fdleak can consume all per-process handles");
    if (!fd_a_ok) detail_process_result("/bin/fdleak#1", ps, &fd_a);
    process_result_t fd_b;
    ps = process_exec("/bin/fdleak", 1, fd_argv, &fd_b);
    bool fd_b_ok = ps == PROC_OK && !fd_b.faulted && fd_b.exit_code == 0;
    check(fd_b_ok, "second /bin/fdleak proves fd table reset on exit");
    if (!fd_b_ok) detail_process_result("/bin/fdleak#2", ps, &fd_b);

    const char *write_argv[] = { "/bin/writetest" };
    ps = process_exec("/bin/writetest", 1, write_argv, &r);
    bool write_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(write_ok, "ring3 /bin/writetest uses ramfs syscalls");
    if (!write_ok) detail_process_result("/bin/writetest", ps, &r);
    char buf[80];
    usize got = 0;
    memset(buf, 0, sizeof(buf));
    check(vfs_read("/tmp/user-writetest.txt", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && strstr(buf, "userland wrote") != 0, "kernel observes user-created ramfs file");

    const char *fs_argv[] = { "/bin/fscheck", "/disk0/hello.txt" };
    ps = process_exec("/bin/fscheck", 2, fs_argv, &r);
    bool fs_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fs_ok, "ring3 /bin/fscheck reads EXT4 through syscalls");
    if (!fs_ok) detail_process_result("/bin/fscheck", ps, &r);
    suite_end();
}

