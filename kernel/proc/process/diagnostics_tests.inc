void process_dump_last(void) {
    kprintf("last user process: pid=%u name=%s exit=%d pml4=%p asid=%llu entry=%p stack_top=%p pages=%llu ticks=%llu..%llu faulted=%u\n",
            last_result.pid, last_result.name, last_result.exit_code, (void *)(uptr)last_result.address_space,
            (unsigned long long)last_result.address_space_generation, (void *)(uptr)last_result.entry, (void *)(uptr)last_result.user_stack_top,
            (unsigned long long)last_result.mapped_pages, (unsigned long long)last_result.started_ticks, (unsigned long long)last_result.finished_ticks, last_result.faulted ? 1u : 0u);
    if (last_result.faulted) {
        kprintf("  fault vector=%llu rip=%p addr=%p\n", (unsigned long long)last_result.fault_vector,
                (void *)(uptr)last_result.fault_rip, (void *)(uptr)last_result.fault_addr);
    }
}


bool process_table_selftest(void) {
    usize before = process_table_count();
    const char *argv0[] = { "/bin/hello" };
    process_result_t r;
    process_status_t st = process_exec("/bin/hello", 1, argv0, &r);
    if (st != PROC_OK || r.pid == 0 || r.exit_code != 7 || r.faulted) return false;
    if (process_table_count() != before + 1u && process_table_count() != PROCESS_TABLE_CAP) return false;
    process_info_t info;
    if (!process_lookup(r.pid, &info)) return false;
    if (info.pid != r.pid || info.state != PROCESS_STATE_EXITED || info.exit_code != 7) return false;
    if (info.address_space_generation != r.address_space_generation || info.mapped_pages == 0) return false;
    if (info.started_ticks > info.finished_ticks) return false;
    if (process_lookup(0, &info)) return false;
    if (process_lookup(0xffffffffu, &info)) return false;
    u32 pid = 0;
    const char *spawn_argv[] = { "/bin/hello" };
    process_result_t spawned;
    process_status_t sst = process_spawn("/bin/hello", 1, spawn_argv, &pid, &spawned);
    if (sst != PROC_OK || pid == 0 || spawned.exit_code != 7) return false;
    if (!process_wait(pid, &info) || info.pid != pid || info.exit_code != 7 || info.state != PROCESS_STATE_EXITED) return false;
    if (process_wait(0xffffffffu, &info)) return false;
    return true;
}

bool process_selftest(void) {
    const char *argv0[] = { "/bin/hello" };
    process_result_t r1;
    process_status_t st = process_exec("/bin/hello", 1, argv0, &r1);
    if (st != PROC_OK || r1.faulted || r1.exit_code != 7 || r1.address_space == vmm_kernel_space()->pml4_physical) return false;

    const char *argv_reg[] = { "/bin/regtrash" };
    process_result_t rreg;
    st = process_exec("/bin/regtrash", 1, argv_reg, &rreg);
    if (st != PROC_OK || rreg.faulted || rreg.exit_code != 23) return false;
    if (rreg.address_space_generation == r1.address_space_generation) return false;

    const char *argv_bad[] = { "/bin/badptr" };
    process_result_t rbad;
    st = process_exec("/bin/badptr", 1, argv_bad, &rbad);
    if (st != PROC_OK || rbad.faulted || rbad.exit_code != 0) return false;

    const char *argv_badpath[] = { "/bin/badpath" };
    process_result_t rbadpath;
    st = process_exec("/bin/badpath", 1, argv_badpath, &rbadpath);
    if (st != PROC_OK || rbadpath.faulted || rbadpath.exit_code != 0) return false;

    const char *argv_stat[] = { "/bin/statcheck" };
    process_result_t rstat;
    st = process_exec("/bin/statcheck", 1, argv_stat, &rstat);
    if (st != PROC_OK || rstat.faulted || rstat.exit_code != 0) return false;

    const char *argv_iso[] = { "/bin/isolate" };
    process_result_t riso1;
    st = process_exec("/bin/isolate", 1, argv_iso, &riso1);
    if (st != PROC_OK || riso1.faulted || riso1.exit_code != 41) return false;
    process_result_t riso2;
    st = process_exec("/bin/isolate", 1, argv_iso, &riso2);
    if (st != PROC_OK || riso2.faulted || riso2.exit_code != 41) return false;
    if (riso1.address_space_generation == riso2.address_space_generation) return false;

    const char *argv_fd[] = { "/bin/fdleak" };
    process_result_t rfd1;
    st = process_exec("/bin/fdleak", 1, argv_fd, &rfd1);
    if (st != PROC_OK || rfd1.faulted || rfd1.exit_code != 0) return false;
    process_result_t rfd2;
    st = process_exec("/bin/fdleak", 1, argv_fd, &rfd2);
    if (st != PROC_OK || rfd2.faulted || rfd2.exit_code != 0) return false;

    const char *argv1[] = { "/bin/writetest" };
    process_result_t rw;
    st = process_exec("/bin/writetest", 1, argv1, &rw);
    if (st != PROC_OK || rw.faulted || rw.exit_code != 0) return false;
    const char *argv2[] = { "/bin/fscheck", "/disk0/hello.txt" };
    process_result_t rf;
    st = process_exec("/bin/fscheck", 2, argv2, &rf);
    if (st != PROC_OK || rf.faulted || rf.exit_code != 0) return false;
    const char *argv_exec[] = { "/bin/execcheck" };
    process_result_t rexec;
    st = process_exec("/bin/execcheck", 1, argv_exec, &rexec);
    if (st != PROC_OK || rexec.faulted || rexec.exit_code != 0 || strcmp(rexec.name, "/bin/fscheck") != 0) return false;
    const char *argv_execve[] = { "/bin/execvecheck" };
    process_result_t rexecve;
    st = process_exec("/bin/execvecheck", 1, argv_execve, &rexecve);
    if (st != PROC_OK || rexecve.faulted || rexecve.exit_code != 0 || strcmp(rexecve.name, "/bin/exectarget") != 0) return false;
    const char *argv_fdexec[] = { "/bin/execfdcheck" };
    process_result_t rfdexec;
    st = process_exec("/bin/execfdcheck", 1, argv_fdexec, &rfdexec);
    if (st != PROC_OK || rfdexec.faulted || rfdexec.exit_code != 0 || strcmp(rfdexec.name, "/bin/exectarget") != 0) return false;
    const char *argv_clo[] = { "/bin/execfdcheck", "cloexec" };
    process_result_t rclo;
    st = process_exec("/bin/execfdcheck", 2, argv_clo, &rclo);
    if (st != PROC_OK || rclo.faulted || rclo.exit_code != 0 || strcmp(rclo.name, "/bin/exectarget") != 0) return false;
    return true;
}
