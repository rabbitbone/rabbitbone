typedef int (*sh_builtin_fn_t)(int argc, char **argv, int allow_exit);

typedef struct sh_builtin_entry {
    const char *name;
    sh_builtin_fn_t fn;
} sh_builtin_entry_t;

static int sh_cmd_help(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; sh_help(); return 0; }
static int sh_cmd_clear(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_clear(); }
static int sh_cmd_uname(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; sh_puts(AURORA_UNAME_TEXT); sh_puts("\n"); return 0; }
static int sh_cmd_path(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_path(argc, argv); }
static int sh_cmd_theme(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_theme(argc, argv); }
static int sh_cmd_which(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_which(argc, argv); }

static int sh_cmd_type(int argc, char **argv, int allow_exit) {
    (void)allow_exit;
    if (argc != 2) { sh_err("usage: type NAME\n"); return 1; }
    if (sh_is_builtin(argv[1])) { sh_puts(argv[1]); sh_puts(" is a shell builtin\n"); return 0; }
    return sh_builtin_which(argc, argv);
}

static int sh_cmd_exit(int argc, char **argv, int allow_exit) {
    unsigned int code = 0;
    if (argc > 1 && !sh_parse_u32(argv[1], &code)) code = 1;
    if (allow_exit) au_exit((int)code);
    return (int)code;
}

static int sh_cmd_crash(int argc, char **argv, int allow_exit) {
    (void)argc; (void)argv; (void)allow_exit;
    volatile unsigned long long *p = (volatile unsigned long long *)0;
    *p = 0x4155524f5241ull;
    return 255;
}

static int sh_cmd_echo(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_echo(argc, argv); }
static int sh_cmd_pid(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; au_i64 pid = au_getpid(); sh_print_i64(pid); sh_puts("\n"); return pid < 0 ? 1 : 0; }
static int sh_cmd_pwd(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; char cwd[SH_PATH_MAX]; if (au_getcwd(cwd, sizeof(cwd)) < 0) return 1; sh_puts(cwd); sh_puts("\n"); return 0; }
static int sh_cmd_cd(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_cd(argc, argv); }
static int sh_cmd_id(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_id(); }
static int sh_cmd_whoami(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_whoami(); }
static int sh_cmd_users(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_users(); }
static int sh_cmd_login(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_login(argc, argv); }
static int sh_cmd_su(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_su(argc, argv); }
static int sh_cmd_sudo(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_sudo(argc, argv); }
static int sh_cmd_ls(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_ls(argc, argv); }
static int sh_cmd_stat(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_stat(argc, argv); }
static int sh_cmd_chmod(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_chmod(argc, argv); }
static int sh_cmd_chown(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_chown(argc, argv); }
static int sh_cmd_cat(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_cat(argc, argv); }
static int sh_cmd_write(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_write_text(argc, argv, 0); }
static int sh_cmd_touch(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_write_text(argc, argv, 1); }
static int sh_cmd_mkdir(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_mkdir(argc, argv); }
static int sh_cmd_rm(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_rm(argc, argv); }
static int sh_cmd_mv(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_mv(argc, argv); }
static int sh_cmd_truncate(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_truncate(argc, argv); }
static int sh_cmd_link(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_link(argc, argv); }
static int sh_cmd_symlink(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_symlink(argc, argv); }
static int sh_cmd_readlink(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_readlink(argc, argv); }
static int sh_cmd_run(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_run(argc, argv); }
static int sh_cmd_spawn(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_spawn(argc, argv); }
static int sh_cmd_wait(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_wait(argc, argv); }
static int sh_cmd_procs(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_procs(); }
static int sh_cmd_proc(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_proc(argc, argv); }

static int sh_cmd_lastproc(int argc, char **argv, int allow_exit) {
    (void)argc; (void)argv; (void)allow_exit;
    if (!sh_last_async_pid) { sh_puts("lastproc: none\n"); return 1; }
    char pidbuf[16];
    char *av[2];
    sh_u64(sh_last_async_pid, pidbuf, sizeof(pidbuf));
    av[0] = "proc";
    av[1] = pidbuf;
    return sh_builtin_proc(2, av);
}

static int sh_cmd_sched(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_sched(); }
static int sh_cmd_preempt(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_preempt(); }
static int sh_cmd_ticks(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; au_i64 t = au_ticks(); sh_puts("ticks="); sh_print_i64(t); sh_puts("\n"); return t < 0 ? 1 : 0; }

static int sh_cmd_sleep(int argc, char **argv, int allow_exit) {
    (void)allow_exit;
    au_u64 t = 1;
    if (argc > 1 && !sh_parse_u64(argv[1], &t)) { sh_err("usage: sleep [TICKS]\n"); return 1; }
    return au_sleep(t) == 0 ? 0 : 1;
}

static int sh_cmd_yield(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return au_yield() == 0 ? 0 : 1; }
static int sh_cmd_count(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_count(argc, argv); }
static int sh_cmd_fdprobe(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_fdprobe(argc, argv); }
static int sh_cmd_fdinfo(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_fdinfo(argc, argv); }
static int sh_cmd_tty(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_tty(); }
static int sh_cmd_statvfs(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_statvfs(argc, argv); }
static int sh_cmd_mounts(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_mounts(); }
static int sh_cmd_sync(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return au_sync() == 0 ? 0 : 1; }
static int sh_cmd_fsync(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_fsync(argc, argv); }
static int sh_cmd_syscall(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_syscall(argc, argv); }
static int sh_cmd_elf(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_elf(argc, argv); }
static int sh_cmd_userbins(int argc, char **argv, int allow_exit) { (void)argc; (void)argv; (void)allow_exit; return sh_builtin_userbins(); }

static int sh_cmd_log(int argc, char **argv, int allow_exit) {
    (void)allow_exit;
    if (argc < 2) { sh_err("usage: log TEXT\n"); return 1; }
    return au_log(argv[1]) == 0 ? 0 : 1;
}

static int sh_cmd_kctl(int argc, char **argv, int allow_exit) { (void)allow_exit; return sh_builtin_kctl(argc, argv); }

static int sh_cmd_runq(int argc, char **argv, int allow_exit) {
    (void)argc; (void)argv; (void)allow_exit;
    (void)au_yield();
    sh_puts("runq: yielded once\n");
    return 0;
}

static int sh_cmd_schedtest(int argc, char **argv, int allow_exit) {
    (void)argc; (void)argv; (void)allow_exit;
    char *av[1];
    av[0] = "schedcheck";
    return sh_command_from_argv(1, av);
}

static const sh_builtin_entry_t sh_builtins[] = {
    { "help", sh_cmd_help }, { "clear", sh_cmd_clear }, { "uname", sh_cmd_uname }, { "path", sh_cmd_path },
    { "which", sh_cmd_which }, { "type", sh_cmd_type }, { "theme", sh_cmd_theme }, { "exit", sh_cmd_exit }, { "crash", sh_cmd_crash },
    { "echo", sh_cmd_echo }, { "pid", sh_cmd_pid }, { "pwd", sh_cmd_pwd }, { "cd", sh_cmd_cd },
    { "id", sh_cmd_id }, { "whoami", sh_cmd_whoami }, { "users", sh_cmd_users }, { "login", sh_cmd_login }, { "su", sh_cmd_su }, { "sudo", sh_cmd_sudo },
    { "ls", sh_cmd_ls }, { "stat", sh_cmd_stat }, { "chmod", sh_cmd_chmod }, { "chown", sh_cmd_chown }, { "cat", sh_cmd_cat },
    { "write", sh_cmd_write }, { "touch", sh_cmd_touch }, { "mkdir", sh_cmd_mkdir }, { "rm", sh_cmd_rm }, { "mv", sh_cmd_mv },
    { "truncate", sh_cmd_truncate }, { "link", sh_cmd_link }, { "ln", sh_cmd_link }, { "symlink", sh_cmd_symlink }, { "readlink", sh_cmd_readlink },
    { "run", sh_cmd_run }, { "spawn", sh_cmd_spawn }, { "qspawn", sh_cmd_spawn }, { "wait", sh_cmd_wait },
    { "procs", sh_cmd_procs }, { "ps", sh_cmd_procs }, { "proc", sh_cmd_proc }, { "lastproc", sh_cmd_lastproc },
    { "sched", sh_cmd_sched }, { "preempt", sh_cmd_preempt }, { "ticks", sh_cmd_ticks }, { "sleep", sh_cmd_sleep }, { "yield", sh_cmd_yield }, { "count", sh_cmd_count },
    { "fdprobe", sh_cmd_fdprobe }, { "fdinfo", sh_cmd_fdinfo }, { "tty", sh_cmd_tty }, { "statvfs", sh_cmd_statvfs }, { "mounts", sh_cmd_mounts },
    { "sync", sh_cmd_sync }, { "fsync", sh_cmd_fsync }, { "syscall", sh_cmd_syscall }, { "elf", sh_cmd_elf }, { "userbins", sh_cmd_userbins }, { "log", sh_cmd_log },
    { "mem", sh_cmd_kctl }, { "heap", sh_cmd_kctl }, { "vmm", sh_cmd_kctl }, { "ktest", sh_cmd_kctl }, { "logs", sh_cmd_kctl },
    { "acpi", sh_cmd_kctl }, { "apic", sh_cmd_kctl }, { "hpet", sh_cmd_kctl }, { "timer", sh_cmd_kctl }, { "smp", sh_cmd_kctl }, { "pci", sh_cmd_kctl }, { "lspci", sh_cmd_kctl }, { "disks", sh_cmd_kctl }, { "blk", sh_cmd_kctl }, { "ext4", sh_cmd_kctl }, { "panic", sh_cmd_kctl }, { "reboot", sh_cmd_kctl }, { "halt", sh_cmd_kctl },
    { "runq", sh_cmd_runq }, { "schedtest", sh_cmd_schedtest },
};

static const sh_builtin_entry_t *sh_find_builtin(const char *name) {
    if (!name) return (const sh_builtin_entry_t *)0;
    for (unsigned int i = 0; i < sizeof(sh_builtins) / sizeof(sh_builtins[0]); ++i) {
        if (sh_streq(name, sh_builtins[i].name)) return &sh_builtins[i];
    }
    return (const sh_builtin_entry_t *)0;
}

static int sh_is_builtin(const char *name) {
    return sh_find_builtin(name) != (const sh_builtin_entry_t *)0;
}

static int sh_run_builtin(int argc, char **argv, int allow_exit) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    const sh_builtin_entry_t *entry = sh_find_builtin(argv[0]);
    if (entry) return entry->fn(argc, argv, allow_exit);
    sh_err(argv[0]);
    sh_err(": unknown builtin\n");
    return 127;
}
