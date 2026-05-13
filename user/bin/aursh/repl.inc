int main(int argc, char **argv) {
    unsigned int old_mode = AURORA_TTY_MODE_CANON | AURORA_TTY_MODE_ECHO;
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) old_mode = ti.mode;
    (void)au_tty_setmode(AURORA_TTY_MODE_RAW);
    if (argc >= 3 && sh_streq(argv[1], "--login")) {
        (void)au_cred(AURORA_CRED_OP_SET_USER, argv[2], 0, 0);
        char home[SH_PATH_MAX];
        sh_user_home(argv[2], home, sizeof(home));
        (void)au_chdir(home);
    }
    au_i64 pid = au_getpid();
    sh_puts("sh pid=");
    sh_print_i64(pid);
    sh_puts(". Type 'help'.\n");

    int last_rc = 0;
    for (;;) {
        char line[SH_LINE_MAX];
        int n = sh_read_line(line, sizeof(line));
        if (n == -2) break;
        if (n < 0) { last_rc = 1; break; }
        if (n == 0) continue;
        last_rc = sh_run_pipeline(line);
    }
    (void)au_tty_setmode(old_mode);
    return last_rc;
}
