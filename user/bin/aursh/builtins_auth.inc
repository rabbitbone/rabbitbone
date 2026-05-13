static int sh_read_secret(const char *prompt, char *out, au_usize cap) {
    au_usize len = 0;
    if (!out || cap < 2u) return 0;
    sh_puts(prompt ? prompt : "password: ");
    for (;;) {
        au_key_event_t ev;
        if (au_tty_readkey(&ev, AURORA_TTY_READ_NONBLOCK) != 0) return 0;
        if (ev.code == AURORA_KEY_NONE) { (void)au_sleep(1); continue; }
        if (ev.code == AURORA_KEY_ENTER || ev.ch == '\n') { out[len] = 0; sh_puts("\n"); return 1; }
        if (ev.code == AURORA_KEY_BACKSPACE || ev.ch == '\b') { if (len) --len; continue; }
        if (ev.ch >= 32u && ev.ch <= 126u && len + 1u < cap) out[len++] = (char)ev.ch;
    }
}

static int sh_get_cred(au_credinfo_t *cred) {
    if (!cred) return 0;
    au_memset(cred, 0, sizeof(*cred));
    return au_cred(AURORA_CRED_OP_GET, cred, 0, 0) == 0;
}

static int sh_user_info_by_index(unsigned int idx, au_userinfo_t *info) {
    if (!info) return 0;
    au_memset(info, 0, sizeof(*info));
    return au_cred(AURORA_CRED_OP_USERINFO, info, 0, idx) == 0;
}

static int sh_user_info_by_name(const char *name, au_userinfo_t *out) {
    if (!name || !out) return 0;
    for (unsigned int i = 0; i < 16u; ++i) {
        au_userinfo_t u;
        if (!sh_user_info_by_index(i, &u)) break;
        if (sh_streq(u.user, name)) { *out = u; return 1; }
    }
    return 0;
}

static void sh_user_home(const char *user, char *out, au_usize cap) {
    if (!out || cap == 0) return;
    if (user && sh_streq(user, "aurora")) sh_copy(out, cap, "/home/aurora");
    else if (user && sh_streq(user, "guest")) sh_copy(out, cap, "/tmp");
    else sh_copy(out, cap, "/");
}

static int sh_builtin_id(void) {
    au_credinfo_t c;
    if (!sh_get_cred(&c)) { sh_err("id: unavailable\n"); return 1; }
    sh_puts("uid="); sh_print_u64(c.uid); sh_puts("("); sh_puts(c.user); sh_puts(")");
    sh_puts(" euid="); sh_print_u64(c.euid);
    sh_puts(" gid="); sh_print_u64(c.gid);
    sh_puts(" egid="); sh_print_u64(c.egid);
    sh_puts(" admin="); sh_print_u64(c.is_admin);
    sh_puts(" sudo_cached="); sh_print_u64(c.sudo_cached);
    sh_puts(" sudo_persistent="); sh_print_u64(c.sudo_persistent);
    sh_puts(" ttl="); sh_print_u64(c.sudo_ttl);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_whoami(void) {
    au_credinfo_t c;
    if (!sh_get_cred(&c)) return 1;
    if (c.euid == AURORA_UID_ROOT) sh_puts("root\n");
    else { sh_puts(c.user); sh_puts("\n"); }
    return 0;
}

static int sh_builtin_users(void) {
    for (unsigned int i = 0; i < 16u; ++i) {
        au_userinfo_t u;
        if (!sh_user_info_by_index(i, &u)) break;
        sh_puts(u.user); sh_puts(" uid="); sh_print_u64(u.uid);
        sh_puts(" gid="); sh_print_u64(u.gid);
        sh_puts(" admin="); sh_print_u64(u.is_admin);
        sh_puts("\n");
    }
    return 0;
}

static int sh_login_as(const char *user, int ask_password) {
    char password[64];
    if (!user || !*user) return 1;
    if (ask_password) {
        char prompt[80];
        au_usize off = 0;
        prompt[0] = 0;
        const char *a = "password for ";
        au_usize an = au_strlen(a), un = au_strlen(user);
        if (an + un + 3u < sizeof(prompt)) {
            au_memcpy(prompt + off, a, an); off += an;
            au_memcpy(prompt + off, user, un); off += un;
            prompt[off++] = ':'; prompt[off++] = ' '; prompt[off] = 0;
        } else sh_copy(prompt, sizeof(prompt), "password: ");
        if (!sh_read_secret(prompt, password, sizeof(password))) return 1;
        if (au_cred(AURORA_CRED_OP_LOGIN, user, password, 0) != 0) { sh_err("login: authentication failed\n"); return 1; }
    } else {
        if (au_cred(AURORA_CRED_OP_SET_USER, user, 0, 0) != 0) { sh_err("login: set user failed\n"); return 1; }
    }
    char home[SH_PATH_MAX];
    sh_user_home(user, home, sizeof(home));
    (void)au_chdir(home);
    return 0;
}

static int sh_builtin_login(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: login USER\n"); return 1; }
    return sh_login_as(argv[1], 1);
}

static int sh_builtin_su(int argc, char **argv) {
    const char *user = argc > 1 ? argv[1] : "root";
    au_credinfo_t c;
    if (!sh_get_cred(&c)) return 1;
    return sh_login_as(user, c.euid == AURORA_UID_ROOT ? 0 : 1);
}

static int sh_parse_owner(const char *spec, unsigned int *uid, unsigned int *gid) {
    char name[AURORA_USER_NAME_MAX];
    au_usize n = 0;
    if (!spec || !uid || !gid) return 0;
    const char *colon = spec;
    while (*colon && *colon != ':') ++colon;
    n = (au_usize)(colon - spec);
    if (n == 0 || n >= sizeof(name)) return 0;
    au_memcpy(name, spec, n); name[n] = 0;
    au_u64 num = 0;
    au_userinfo_t u;
    if (sh_parse_u64(name, &num)) { *uid = (unsigned int)num; *gid = *uid; }
    else if (sh_user_info_by_name(name, &u)) { *uid = u.uid; *gid = u.gid; }
    else return 0;
    if (*colon == ':') {
        const char *g = colon + 1;
        if (*g) {
            if (sh_parse_u64(g, &num)) *gid = (unsigned int)num;
            else if (sh_user_info_by_name(g, &u)) *gid = u.gid;
            else return 0;
        }
    }
    return 1;
}

static int sh_builtin_chmod(int argc, char **argv) {
    unsigned int mode = 0;
    if (argc != 3 || !sh_parse_octal_mode(argv[1], &mode)) { sh_err("usage: chmod MODE PATH\n"); return 1; }
    if (au_chmod(argv[2], mode) != 0) { sh_err("chmod: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_chown(int argc, char **argv) {
    unsigned int uid = 0, gid = 0;
    if (argc != 3 || !sh_parse_owner(argv[1], &uid, &gid)) { sh_err("usage: chown USER[:GROUP] PATH\n"); return 1; }
    if (au_chown(argv[2], uid, gid) != 0) { sh_err("chown: failed\n"); return 1; }
    return 0;
}

static int sh_sudo_ensure(unsigned int flags, int non_interactive) {
    if (au_sudo(AURORA_SUDO_OP_VALIDATE, 0, flags) == 0) return 0;
    if (non_interactive) { sh_err("sudo: a password is required\n"); return 1; }
    char pass[64];
    if (!sh_read_secret("sudo password: ", pass, sizeof(pass))) return 1;
    if (au_sudo(AURORA_SUDO_OP_VALIDATE, pass, flags) != 0) { sh_err("sudo: authentication failed\n"); return 1; }
    return 0;
}

static int sh_command_from_argv(int argc, char **argv);

static int sh_builtin_sudo(int argc, char **argv) {
    int non_interactive = 0;
    int timeout_set = 0;
    const char *target_user = 0;
    int i = 1;
    if (argc == 1) { sh_err("usage: sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]\n"); return 1; }
    while (i < argc && argv[i][0] == '-') {
        if (sh_streq(argv[i], "-h") || sh_streq(argv[i], "--help")) {
            sh_puts("usage: sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]\n");
            return 0;
        }
        if (sh_streq(argv[i], "-V") || sh_streq(argv[i], "--version")) { sh_puts("Aurora sudo 0.1\n"); return 0; }
        if (sh_streq(argv[i], "-n") || sh_streq(argv[i], "--non-interactive")) { non_interactive = 1; ++i; continue; }
        if (sh_streq(argv[i], "-v") || sh_streq(argv[i], "--validate")) return sh_sudo_ensure(0, non_interactive);
        if (sh_streq(argv[i], "-k") || sh_streq(argv[i], "--reset-timestamp")) return au_sudo(AURORA_SUDO_OP_INVALIDATE, 0, 0) == 0 ? 0 : 1;
        if (sh_streq(argv[i], "-K") || sh_streq(argv[i], "--kill-timestamp")) return au_sudo(AURORA_SUDO_OP_INVALIDATE, 0, 0) == 0 ? 0 : 1;
        if (sh_streq(argv[i], "-l") || sh_streq(argv[i], "--list")) {
            if (sh_sudo_ensure(0, non_interactive) != 0) return 1;
            sh_puts("User may run any command as root in this Aurora session.\n");
            return 0;
        }
        if (sh_streq(argv[i], "-T")) {
            au_u64 ttl = 0;
            if (i + 1 >= argc || !sh_parse_u64(argv[i + 1], &ttl)) { sh_err("sudo: usage: sudo -T TICKS [CMD...]\n"); return 1; }
            if (au_sudo(AURORA_SUDO_OP_SET_TIMEOUT, 0, ttl) != 0) return 1;
            timeout_set = 1;
            i += 2;
            continue;
        }
        if (sh_streq(argv[i], "-u")) {
            if (i + 1 >= argc) { sh_err("sudo: -u requires USER\n"); return 1; }
            target_user = argv[i + 1];
            i += 2;
            continue;
        }
        if (sh_streq(argv[i], "-s") || sh_streq(argv[i], "-i") || sh_streq(argv[i], "--shell") || sh_streq(argv[i], "--login")) {
            if (sh_sudo_ensure(AURORA_SUDO_FLAG_ACTIVATE | AURORA_SUDO_FLAG_PERSIST, non_interactive) != 0) return 1;
            if (sh_streq(argv[i], "-i") || sh_streq(argv[i], "--login")) (void)au_chdir("/");
            sh_puts("sudo: root session active; use 'sudo -k' to drop it\n");
            return 0;
        }
        break;
    }
    if (i >= argc) return timeout_set ? 0 : sh_sudo_ensure(0, non_interactive);

    au_credinfo_t old;
    if (!sh_get_cred(&old)) return 1;
    if (sh_sudo_ensure(AURORA_SUDO_FLAG_ACTIVATE, non_interactive) != 0) return 1;
    if (target_user) {
        au_userinfo_t u;
        if (!sh_user_info_by_name(target_user, &u)) { sh_err("sudo: unknown user\n"); (void)au_cred(AURORA_CRED_OP_SET_EUID, 0, 0, old.euid); return 1; }
        if (au_cred(AURORA_CRED_OP_SET_EUID, 0, 0, u.uid) != 0) { sh_err("sudo: cannot switch effective user\n"); (void)au_cred(AURORA_CRED_OP_SET_EUID, 0, 0, old.euid); return 1; }
    }
    int rc = sh_command_from_argv(argc - i, argv + i);
    (void)au_cred(AURORA_CRED_OP_SET_EUID, 0, 0, old.euid);
    return rc;
}

