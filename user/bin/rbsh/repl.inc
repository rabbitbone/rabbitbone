static void sh_print_version(void) {
    sh_puts("rbsh 0.2 for ");
    sh_puts(RABBITBONE_UNAME_TEXT);
    sh_puts("\n");
}

static void sh_print_cli_usage(void) {
    sh_puts("usage: sh [-il] [--login USER] [--no-banner] [-c COMMAND]\n");
}

static int sh_apply_login_user(const char *user) {
    if (!user || !*user) return 1;
    if (au_cred(RABBITBONE_CRED_OP_SET_USER, user, 0, 0) != 0) return 1;
    char home[SH_PATH_MAX];
    sh_user_home(user, home, sizeof(home));
    (void)au_chdir(home);
    return 0;
}

int main(int argc, char **argv) {
    unsigned int old_mode = RABBITBONE_TTY_MODE_CANON | RABBITBONE_TTY_MODE_ECHO;
    int force_interactive = 0;
    int no_banner = 0;
    const char *login_user = 0;
    const char *command = 0;

    sh_shell_pid = (unsigned int)au_getpid();
    sh_refresh_base_env();

    for (int i = 1; i < argc; ++i) {
        if (sh_is_option_end(argv[i])) break;
        if (sh_arg_is_help(argv[i])) { sh_print_cli_usage(); return 0; }
        if (sh_arg_is_version(argv[i])) { sh_print_version(); return 0; }
        if (sh_streq(argv[i], "-i") || sh_streq(argv[i], "--interactive")) { force_interactive = 1; continue; }
        if (sh_streq(argv[i], "-l")) { sh_login_shell = 1; continue; }
        if (sh_streq(argv[i], "--no-banner")) { no_banner = 1; continue; }
        if (sh_streq(argv[i], "--login")) {
            sh_login_shell = 1;
            if (i + 1 >= argc) { sh_err("sh: --login requires USER\n"); return 2; }
            login_user = argv[++i];
            continue;
        }
        if (sh_streq(argv[i], "-c")) {
            if (i + 1 >= argc) { sh_err("sh: -c requires COMMAND\n"); return 2; }
            command = argv[++i];
            break;
        }
        sh_err("sh: unknown option: "); sh_write_fd((au_i64)RABBITBONE_STDERR, argv[i]); sh_err("\n");
        sh_print_cli_usage();
        return 2;
    }

    if (login_user && sh_apply_login_user(login_user) != 0) {
        sh_err("sh: login setup failed\n");
        return 1;
    }

    if (command) {
        if (!sh_copy(sh_exec_line, sizeof(sh_exec_line), command)) { sh_err("sh: command too long\n"); return 2; }
        return sh_run_line(sh_exec_line);
    }

    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) old_mode = ti.mode;
    sh_interactive_shell = 1;
    if (force_interactive || sh_interactive_shell) (void)au_tty_setmode(RABBITBONE_TTY_MODE_RAW);
    if (!no_banner) {
        sh_puts("Rabbitbone");
        if (sh_login_shell) sh_puts(" login");
        sh_puts("\n");
    }

    int last_rc = 0;
    for (;;) {
        int n = sh_read_line(sh_repl_line, sizeof(sh_repl_line));
        if (n == -2) break;
        if (n < 0) { last_rc = 1; break; }
        if (n == 0) continue;
        last_rc = sh_run_line(sh_repl_line);
    }
    (void)au_tty_setmode(old_mode);
    return last_rc;
}
