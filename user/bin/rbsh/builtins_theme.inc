static const char *sh_theme_name(unsigned int id) {
    if (id == RABBITBONE_THEME_LEGACY) return "legacy";
    if (id == RABBITBONE_THEME_BLACK) return "black";
    return "unknown";
}

static int sh_theme_id(const char *name, unsigned int *out) {
    if (!name || !out) return 0;
    if (sh_streq(name, "legacy")) { *out = RABBITBONE_THEME_LEGACY; return 1; }
    if (sh_streq(name, "black")) { *out = RABBITBONE_THEME_BLACK; return 1; }
    return 0;
}

static int sh_builtin_theme(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && sh_streq(argv[1], "show"))) {
        au_i64 id = au_theme(RABBITBONE_THEME_OP_GET, 0);
        if (id < 0) { sh_err("theme: syscall failed\n"); return 1; }
        sh_puts("theme="); sh_puts(sh_theme_name((unsigned int)id)); sh_puts("\n");
        sh_puts("available: legacy black\n");
        return 0;
    }
    if (argc == 2) {
        unsigned int id = 0;
        if (!sh_theme_id(argv[1], &id)) { sh_err("usage: theme [show|legacy|black]\n"); return 1; }
        au_i64 r = au_theme(RABBITBONE_THEME_OP_SET, id);
        if (r < 0) { sh_err("theme: set failed\n"); return 1; }
        (void)au_tty_clear();
        sh_puts("theme="); sh_puts(sh_theme_name(id)); sh_puts("\n");
        return 0;
    }
    sh_err("usage: theme [show|legacy|black]\n");
    return 1;
}

static int sh_builtin_which(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: which NAME\n"); return 1; }
    char path[SH_PATH_MAX];
    if (!sh_resolve_program(path, sizeof(path), argv[1])) return 1;
    au_stat_t st;
    au_memset(&st, 0, sizeof(st));
    if (au_stat(path, &st) != 0) { sh_err("which: not found\n"); return 1; }
    sh_puts(path); sh_puts("\n");
    return 0;
}

static int sh_is_builtin(const char *name);
static int sh_run_builtin(int argc, char **argv, int allow_exit);
static int sh_path_is_elf(const char *path);
static int sh_spawn_external_wait(sh_stage_t *stage);
