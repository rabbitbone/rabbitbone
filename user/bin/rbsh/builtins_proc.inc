static int sh_builtin_run(int argc, char **argv) {
    if (argc < 2) { sh_err("usage: run COMMAND [ARGS]\n"); return 1; }
    return sh_command_from_argv(argc - 1, argv + 1);
}

static int sh_builtin_spawn(int argc, char **argv) {
    if (argc < 2) { sh_err("usage: spawn PATH [ARGS]\n"); return 1; }
    unsigned int run_argc = (unsigned int)(argc - 1);
    if (run_argc > SH_ARG_MAX) return 1;
    if (sh_is_builtin(argv[1])) {
        au_i64 child = au_fork();
        if (child < 0) { sh_err("spawn: fork failed\n"); return 127; }
        if (child == 0) {
            int code = sh_run_builtin(argc - 1, argv + 1, 0);
            au_exit(code);
        }
        sh_last_async_pid = (unsigned int)child;
        sh_puts("spawned builtin pid="); sh_print_i64(child); sh_puts("\n");
        return 0;
    }
    char path[SH_PATH_MAX];
    char *run_argv[SH_ARG_MAX];
    if (!sh_resolve_program(path, sizeof(path), argv[1])) return 1;
    if (!sh_path_is_elf(path)) { sh_err("spawn: not executable: "); sh_write_fd((au_i64)RABBITBONE_STDERR, path); sh_err("\n"); return 126; }
    run_argv[0] = path;
    for (unsigned int i = 1; i < run_argc; ++i) run_argv[i] = argv[i + 1u];
    au_i64 pid = au_spawnv(path, run_argc, (const char *const *)run_argv);
    if (pid < 0) { sh_err("spawn: failed: "); sh_write_fd((au_i64)RABBITBONE_STDERR, path); sh_err("\n"); return 127; }
    sh_last_async_pid = (unsigned int)pid;
    sh_puts("spawned pid="); sh_print_i64(pid); sh_puts("\n");
    return 0;
}

static int sh_builtin_wait(int argc, char **argv) {
    unsigned int pid = 0;
    if (argc == 1 && sh_last_async_pid != 0) pid = sh_last_async_pid;
    else if (argc == 2 && sh_parse_u32(argv[1], &pid)) { }
    else { sh_err("usage: wait [PID]\n"); return 1; }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 r = au_wait(pid, &info);
    if (r < 0) { sh_err("wait: no completed child\n"); return 1; }
    sh_print_procinfo(&info);
    return info.exit_code;
}

static int sh_builtin_proc(int argc, char **argv) {
    unsigned int pid = 0;
    if (argc != 2 || !sh_parse_u32(argv[1], &pid)) { sh_err("usage: proc PID\n"); return 1; }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_procinfo(pid, &info) != 0) { sh_err("proc: not found\n"); return 1; }
    sh_print_procinfo(&info);
    return 0;
}

static int sh_builtin_procs(void) {
    unsigned int shown = 0;
    for (unsigned int pid = 1; pid <= SH_SCAN_PID_MAX; ++pid) {
        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        if (au_procinfo(pid, &info) == 0 && info.pid != 0) {
            sh_print_procinfo(&info);
            ++shown;
        }
    }
    if (!shown) sh_puts("procs: no visible processes\n");
    return 0;
}

static int sh_builtin_sched(void) {
    au_schedinfo_t s;
    au_memset(&s, 0, sizeof(s));
    if (au_schedinfo(&s) != 0) { sh_err("sched: unavailable\n"); return 1; }
    sh_puts("sched: cap="); sh_print_u64(s.queue_capacity);
    sh_puts(" queued="); sh_print_u64(s.queued);
    sh_puts(" running="); sh_print_u64(s.running);
    sh_puts(" completed="); sh_print_u64(s.completed);
    sh_puts(" failed="); sh_print_u64(s.failed);
    sh_puts(" dispatched="); sh_print_u64(s.total_dispatched);
    sh_puts(" yields="); sh_print_u64(s.total_yields);
    sh_puts(" sleeps="); sh_print_u64(s.total_sleeps);
    sh_puts(" preempt="); sh_print_u64(s.preempt_enabled);
    sh_puts(" quantum="); sh_print_u64(s.quantum_ticks);
    sh_puts(" timer="); sh_print_u64(s.total_timer_ticks);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_preempt(void) {
    au_preemptinfo_t p;
    au_memset(&p, 0, sizeof(p));
    if (au_preemptinfo(&p) != 0) { sh_err("preempt: unavailable\n"); return 1; }
    sh_puts("preempt: enabled="); sh_print_u64(p.enabled);
    sh_puts(" quantum="); sh_print_u64(p.quantum_ticks);
    sh_puts(" current_pid="); sh_print_u64(p.current_pid);
    sh_puts(" slice="); sh_print_u64(p.current_slice_ticks);
    sh_puts(" timer="); sh_print_u64(p.total_timer_ticks);
    sh_puts(" user="); sh_print_u64(p.user_ticks);
    sh_puts(" kernel="); sh_print_u64(p.kernel_ticks);
    sh_puts(" expirations="); sh_print_u64(p.total_preemptions);
    sh_puts(" last_tick="); sh_print_u64(p.last_preempt_ticks);
    sh_puts(" rip="); sh_print_hex64(p.last_preempt_rip);
    sh_puts("\n");
    return 0;
}

