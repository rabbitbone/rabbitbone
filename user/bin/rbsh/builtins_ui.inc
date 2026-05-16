static void sh_help_general(void) {
    sh_puts("rbsh: Rabbitbone shell\n");
    sh_puts("usage: sh [-il] [--login USER] [-c COMMAND]\n\n");
    sh_puts("grammar: ;  &&  ||  |  &  quotes  $VAR  ${VAR}  $?  $$  ~  # comments\n");
    sh_puts("redirs : <  >  >>  1>  2>  2>>  2>&1  1>&2  &>  &>>\n\n");
    sh_puts("builtins:\n");
    sh_puts("  help clear uname info path which type set export unset env exit\n");
    sh_puts("  echo printf true false test pid pwd cd id whoami users login su sudo\n");
    sh_puts("  ls ll stat cat write touch mkdir rm mv cp link ln symlink readlink truncate\n");
    sh_puts("  run spawn wait jobs ps proc last sched preempt ticks sleep yield\n");
    sh_puts("  fdprobe fdinfo tty statvfs mounts sync fsync syscall elf userbins log\n");
    sh_puts("  mem heap vmm ktest logs boot disks blk net pci acpi apic hpet timer smp ext4\n");
    sh_puts("network: net dhcp, ping HOST|A.B.C.D, netctl dns NAME, ip A.B.C.D/PREFIX gw A.B.C.D dns A.B.C.D\nkeys: Tab complete, Up/Down history, Home/End/Delete, PageUp/PageDown, Ctrl+L, Ctrl+D, Ctrl+C.\n");
}

static void sh_help_command(const char *cmd) {
    if (!cmd || !*cmd) { sh_help_general(); return; }
    if (sh_streq(cmd, "ls")) sh_puts("usage: ls [-aAldi1F] [PATH...]\n");
    else if (sh_streq(cmd, "cat")) sh_puts("usage: cat [-nbsE] [FILE...]\n");
    else if (sh_streq(cmd, "mkdir")) sh_puts("usage: mkdir [-pv] PATH...\n");
    else if (sh_streq(cmd, "rm")) sh_puts("usage: rm [-frv] PATH...\n");
    else if (sh_streq(cmd, "mv")) sh_puts("usage: mv [-fnv] OLD NEW\n");
    else if (sh_streq(cmd, "cp")) sh_puts("usage: cp [-fnv] SRC DST\n");
    else if (sh_streq(cmd, "touch")) sh_puts("usage: touch [-cv] PATH...\n");
    else if (sh_streq(cmd, "write") || sh_streq(cmd, "put")) sh_puts("usage: write [-a] PATH TEXT...\n");
    else if (sh_streq(cmd, "cd")) sh_puts("usage: cd [PATH|-]\n");
    else if (sh_streq(cmd, "pwd")) sh_puts("usage: pwd [-L|-P]\n");
    else if (sh_streq(cmd, "echo")) sh_puts("usage: echo [-n] [-e|-E] [ARG...]\n");
    else if (sh_streq(cmd, "printf")) sh_puts("usage: printf FORMAT [ARG...]\n");
    else if (sh_streq(cmd, "test") || sh_streq(cmd, "[")) sh_puts("usage: test [-e PATH|-f PATH|-d PATH|-n STR|-z STR|STR = STR|STR != STR]\n");
    else if (sh_streq(cmd, "which")) sh_puts("usage: which [-a] NAME...\n");
    else if (sh_streq(cmd, "type")) sh_puts("usage: type [-a] NAME...\n");
    else if (sh_streq(cmd, "set")) sh_puts("usage: set [NAME=VALUE...]\n");
    else if (sh_streq(cmd, "export")) sh_puts("usage: export [NAME|NAME=VALUE...]\n");
    else if (sh_streq(cmd, "unset")) sh_puts("usage: unset NAME...\n");
    else if (sh_streq(cmd, "path")) sh_puts("usage: path [VALUE]\n");
    else if (sh_streq(cmd, "net") || sh_streq(cmd, "netctl")) sh_puts("usage: net [status|dhcp|ping HOST|A.B.C.D|arp A.B.C.D|dns NAME|ip A.B.C.D/PREFIX gw A.B.C.D dns A.B.C.D]\n");
    else if (sh_streq(cmd, "ping")) sh_puts("usage: ping HOST|A.B.C.D\n");
    else if (sh_streq(cmd, "dhcp") || sh_streq(cmd, "ifup")) sh_puts("usage: dhcp\n");
    else if (sh_streq(cmd, "ip")) sh_puts("usage: ip A.B.C.D/PREFIX [gw A.B.C.D] [dns A.B.C.D]\n");
    else if (sh_streq(cmd, "arp")) sh_puts("usage: arp A.B.C.D\n");
    else if (sh_streq(cmd, "sudo")) sh_puts("usage: sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]\n");
    else { sh_err("help: no detailed help for "); sh_write_fd((au_i64)RABBITBONE_STDERR, cmd); sh_err("\n"); }
}

static int sh_builtin_help(int argc, char **argv) {
    if (argc == 1) { sh_help_general(); return 0; }
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_help_general(); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) sh_puts("\n");
        sh_help_command(argv[i]);
    }
    return rc;
}

static int sh_builtin_clear(void) {
    if (au_tty_clear() != 0) { sh_err("clear: tty clear failed\n"); return 1; }
    return 0;
}

static int sh_builtin_path(int argc, char **argv) {
    if (argc == 1) {
        sh_puts("PATH="); sh_puts(sh_path_value()); sh_puts("\n");
        return 0;
    }
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: path [VALUE]\n"); return 0; }
    if (argc == 2) {
        if (!sh_set_path_value(argv[1])) { sh_err("path: invalid value\n"); return 1; }
        sh_puts("PATH="); sh_puts(sh_path_value()); sh_puts("\n");
        return 0;
    }
    sh_err("usage: path [VALUE]\n");
    return 1;
}

static void sh_print_var_line(const sh_var_t *v, int exported_only) {
    if (!v || !v->name[0]) return;
    if (exported_only && !v->exported) return;
    sh_puts(v->name); sh_puts("="); sh_puts(v->value); sh_puts("\n");
}

static int sh_builtin_setvars(int argc, char **argv) {
    if (argc == 1) {
        for (unsigned int i = 0; i < SH_VAR_MAX; ++i) sh_print_var_line(&sh_vars[i], 0);
        return 0;
    }
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: set [NAME=VALUE...]\n"); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        char name[SH_VAR_NAME_MAX];
        const char *value = 0;
        if (!sh_assignment_token(argv[i], name, sizeof(name), &value)) {
            sh_err("set: expected NAME=VALUE: "); sh_write_fd((au_i64)RABBITBONE_STDERR, argv[i]); sh_err("\n");
            rc = 1;
            continue;
        }
        if (!sh_var_set(name, value, 0)) rc = 1;
        if (sh_streq(name, "PATH")) (void)sh_set_path_value(value);
    }
    return rc;
}

static int sh_builtin_export(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && sh_streq(argv[1], "-p"))) {
        for (unsigned int i = 0; i < SH_VAR_MAX; ++i) sh_print_var_line(&sh_vars[i], 1);
        return 0;
    }
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: export [-p] [NAME|NAME=VALUE...]\n"); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (sh_is_option_end(argv[i])) continue;
        char name[SH_VAR_NAME_MAX];
        const char *value = 0;
        if (sh_assignment_token(argv[i], name, sizeof(name), &value)) {
            if (!sh_var_set(name, value, 1)) rc = 1;
            if (sh_streq(name, "PATH")) (void)sh_set_path_value(value);
            continue;
        }
        if (!sh_var_name_ok(argv[i])) { sh_err("export: invalid name: "); sh_write_fd((au_i64)RABBITBONE_STDERR, argv[i]); sh_err("\n"); rc = 1; continue; }
        int idx = sh_var_find(argv[i]);
        if (idx >= 0) sh_vars[idx].exported = 1;
        else if (!sh_var_set(argv[i], "", 1)) rc = 1;
    }
    return rc;
}

static int sh_builtin_unset(int argc, char **argv) {
    if (argc < 2) { sh_err("usage: unset NAME...\n"); return 1; }
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: unset NAME...\n"); return 0; }
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (sh_is_option_end(argv[i])) continue;
        if (!sh_var_name_ok(argv[i])) { sh_err("unset: invalid name: "); sh_write_fd((au_i64)RABBITBONE_STDERR, argv[i]); sh_err("\n"); rc = 1; continue; }
        if (sh_streq(argv[i], "PATH")) sh_copy(sh_path_env, sizeof(sh_path_env), SH_DEFAULT_PATH);
        (void)sh_var_unset(argv[i]);
    }
    return rc;
}

static int sh_builtin_env(int argc, char **argv) {
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: env [-0]\n"); return 0; }
    int nul = 0;
    if (argc > 2 || (argc == 2 && !sh_streq(argv[1], "-0"))) { sh_err("usage: env [-0]\n"); return 1; }
    if (argc == 2) nul = 1;
    sh_refresh_base_env();
    for (unsigned int i = 0; i < SH_VAR_MAX; ++i) {
        if (!sh_vars[i].name[0] || !sh_vars[i].exported) continue;
        sh_puts(sh_vars[i].name); sh_puts("="); sh_puts(sh_vars[i].value); if (nul) { char z = 0; sh_write_n_fd((au_i64)RABBITBONE_STDOUT, &z, 1u); } else sh_puts("\n");
    }
    (void)argv;
    return 0;
}
