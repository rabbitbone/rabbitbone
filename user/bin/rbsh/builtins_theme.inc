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

static int sh_print_all_program_matches(const char *name) {
    int found = 0;
    if (!name || !*name) return 0;
    if (sh_strchr((char *)name, '/')) {
        if (sh_path_exists(name, 1)) { sh_puts(name); sh_puts("\n"); return 1; }
        return 0;
    }
    const char *path = sh_path_value();
    while (*path) {
        const char *start = path;
        while (*path && *path != ':') ++path;
        au_usize len = (au_usize)(path - start);
        if (len > 0u && sh_join_path(sh_fs_path, sizeof(sh_fs_path), start, len, name)) {
            au_stat_t st;
            au_memset(&st, 0, sizeof(st));
            if (au_stat(sh_fs_path, &st) == 0 && st.type == SH_NODE_FILE) { sh_puts(sh_fs_path); sh_puts("\n"); found = 1; }
        }
        if (*path == ':') ++path;
    }
    return found;
}

static int sh_builtin_which(int argc, char **argv) {
    int all = 0;
    int i = 1;
    if (argc == 2 && sh_arg_is_help(argv[1])) { sh_puts("usage: which [-a] NAME...\n"); return 0; }
    if (i < argc && sh_streq(argv[i], "-a")) { all = 1; ++i; }
    if (i >= argc) { sh_err("usage: which [-a] NAME...\n"); return 1; }
    int rc = 0;
    for (; i < argc; ++i) {
        if (sh_is_builtin(argv[i])) { if (all) { sh_puts(argv[i]); sh_puts(": shell builtin\n"); } else { rc = 1; } }
        if (all) {
            if (!sh_print_all_program_matches(argv[i]) && !sh_is_builtin(argv[i])) rc = 1;
            continue;
        }
        char path[SH_PATH_MAX];
        if (!sh_resolve_program(path, sizeof(path), argv[i])) { rc = 1; continue; }
        sh_puts(path); sh_puts("\n");
    }
    return rc;
}

static int sh_is_builtin(const char *name);
static int sh_run_builtin(int argc, char **argv, int allow_exit);
static int sh_path_is_elf(const char *path);
static int sh_spawn_external_wait(sh_stage_t *stage);
