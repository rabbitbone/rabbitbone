static void sh_prompt(int last_rc) {
    (void)last_rc;
    char cwd[SH_PATH_MAX];
    if (au_getcwd(cwd, sizeof(cwd)) < 0) sh_copy(cwd, sizeof(cwd), "?");
    sh_puts(cwd);
    sh_puts("$ ");
}

static int sh_read_line(char *line, au_usize cap) {
    au_usize len = 0;
    int discarding = 0;
    if (!line || cap < 2u) return -1;
    for (;;) {
        au_key_event_t ev;
        if (au_tty_readkey(&ev, AURORA_TTY_READ_NONBLOCK) != 0) return -1;
        if (ev.code == AURORA_KEY_NONE) {
            (void)au_sleep(1);
            continue;
        }
        if (ev.code == AURORA_KEY_ENTER || ev.ch == '\n') {
            sh_puts("\n");
            if (discarding) {
                sh_err("sh: line too long\n");
                len = 0;
                discarding = 0;
                continue;
            }
            line[len] = 0;
            return (int)len;
        }
        if (ev.code == AURORA_KEY_BACKSPACE || ev.ch == '\b') {
            if (!discarding && len > 0) {
                --len;
                sh_puts("\b");
            }
            continue;
        }
        if (ev.ch >= 32u && ev.ch <= 126u) {
            if (discarding) continue;
            if (len + 1u < cap) {
                line[len++] = (char)ev.ch;
                char out[2] = { (char)ev.ch, 0 };
                sh_puts(out);
            } else {
                discarding = 1;
            }
        }
    }
}

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
        sh_prompt(last_rc);
        int n = sh_read_line(line, sizeof(line));
        if (n < 0) { last_rc = 1; break; }
        if (n == 0) continue;
        last_rc = sh_run_pipeline(line);
    }
    (void)au_tty_setmode(old_mode);
    return last_rc;
}
