static void test_process_registry(void) {
    suite_begin("process/registry/contracts");
    usize before = process_table_count();
    check(before > 0, "process registry retained completed user processes");
    check(process_runtime_selftest(), "process runtime clear keeps per-CPU current buffer valid");
    check(process_table_selftest(), "process registry records completed exec lifecycle");
    process_result_t last;
    const char *argv[] = { "/bin/procstat" };
    process_status_t ps = process_exec("/bin/procstat", 1, argv, &last);
    check(ps == PROC_OK && last.exit_code == 0 && !last.faulted, "process registry supports procinfo user program after stress");
    process_info_t info;
    check(process_lookup(last.pid, &info) && info.pid == last.pid && info.state == PROCESS_STATE_EXITED && info.exit_code == 0, "process lookup returns completed pid metadata");
    u32 pid = 0;
    const char *spawn_argv[] = { "/bin/hello" };
    process_result_t spawned;
    ps = process_spawn("/bin/hello", 1, spawn_argv, &pid, &spawned);
    check(ps == PROC_OK && pid == spawned.pid && spawned.exit_code == 7 && !spawned.faulted, "process spawn records completed child pid");
    check(process_wait(pid, &info) && info.pid == pid && info.state == PROCESS_STATE_EXITED && info.exit_code == 7, "process wait returns completed child metadata");
    const char *spawnv_argv[] = { "/bin/fscheck", "/disk0/hello.txt" };
    syscall_result_t sv = syscall_dispatch(RABBITBONE_SYS_SPAWNV, (u64)(uptr)"/bin/fscheck", 2u, (u64)(uptr)spawnv_argv, 0, 0, 0);
    check(sv.error == 0 && sv.value > 0, "Rust-dispatched spawnv syscall accepts argv vector from kernel boundary");
    if (!sv.error && sv.value > 0) {
        check(process_wait((u32)sv.value, &info) && info.exit_code == 0 && info.state == PROCESS_STATE_EXITED, "process wait returns spawnv child metadata");
    } else {
        skip("process wait returns spawnv child metadata");
    }
    syscall_result_t sr = syscall_dispatch(RABBITBONE_SYS_WAIT, pid, (u64)(uptr)&info, 0, 0, 0, 0);
    check(sr.error == 0 && info.pid == pid && info.exit_code == 7, "Rust-dispatched wait syscall returns child metadata");
    check(strcmp(syscall_name(RABBITBONE_SYS_FORK), "fork") == 0, "Rust-dispatched fork syscall name is stable");
    check(strcmp(syscall_name(RABBITBONE_SYS_EXEC), "exec") == 0 && strcmp(syscall_name(RABBITBONE_SYS_EXECV), "execv") == 0 && strcmp(syscall_name(RABBITBONE_SYS_EXECVE), "execve") == 0 && strcmp(syscall_name(RABBITBONE_SYS_FDCTL), "fdctl") == 0 && strcmp(syscall_name(RABBITBONE_SYS_DUP2), "dup2") == 0 && strcmp(syscall_name(RABBITBONE_SYS_POLL), "poll") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_GETINFO), "tty_getinfo") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_SETMODE), "tty_setmode") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_READKEY), "tty_readkey") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_SCROLL), "tty_scroll") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_SETCURSOR), "tty_setcursor") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_CLEAR_LINE), "tty_clearline") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_CLEAR), "tty_clear") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TTY_CURSOR_VISIBLE), "tty_cursor_visible") == 0 && strcmp(syscall_name(RABBITBONE_SYS_BRK), "brk") == 0 && strcmp(syscall_name(RABBITBONE_SYS_SBRK), "sbrk") == 0 && strcmp(syscall_name(RABBITBONE_SYS_MMAP), "mmap") == 0 && strcmp(syscall_name(RABBITBONE_SYS_MUNMAP), "munmap") == 0 && strcmp(syscall_name(RABBITBONE_SYS_MPROTECT), "mprotect") == 0 && strcmp(syscall_name(RABBITBONE_SYS_KILL), "kill") == 0 && strcmp(syscall_name(RABBITBONE_SYS_SIGACTION), "sigaction") == 0 && strcmp(syscall_name(RABBITBONE_SYS_TCSETPGRP), "tcsetpgrp") == 0, "Rust-dispatched exec/fdctl/tty/memory/signal/job-control syscall names are stable");
    check(!process_lookup(0, &info) && !process_lookup(0xffffffffu, &info), "process lookup rejects invalid or unknown pid");
    check(process_table_count() >= before, "process registry count is monotonic until ring wraps");
    suite_end();
}


