static char sh_fs_buf[512];
static char sh_fs_path[SH_PATH_MAX];

static int sh_ls_join_path(const char *dir, const char *name, char *out, au_usize cap) {
    if (!dir || !name || !out || cap == 0) return 0;
    if (sh_streq(dir, "/")) {
        if (2u + au_strlen(name) > cap) return 0;
        out[0] = '/';
        return sh_copy(out + 1u, cap - 1u, name);
    }
    return sh_join_path(out, cap, dir, au_strlen(dir), name);
}

typedef struct sh_ls_opts {
    int long_view;
    int all;
    int almost_all;
    int classify;
    int directory;
    int show_inode;
    int one_per_line;
    int multi_path;
} sh_ls_opts_t;

static int sh_ls_hidden_skip(const char *name, const sh_ls_opts_t *opts) {
    if (!name || !opts) return 1;
    if (opts->all) return 0;
    if (opts->almost_all && (!sh_streq(name, ".") && !sh_streq(name, ".."))) return 0;
    return name[0] == '.';
}

static void sh_ls_print_suffix(unsigned int type, const sh_ls_opts_t *opts) {
    if (!opts || !opts->classify) return;
    if (type == SH_NODE_DIR) sh_puts("/");
    else if (type == SH_NODE_SYMLINK) sh_puts("@");
    else if (type == SH_NODE_DEV) sh_puts("%");
}

static void sh_ls_print_short(const char *name, unsigned int type, const sh_ls_opts_t *opts) {
    sh_puts(name);
    sh_ls_print_suffix(type, opts);
    sh_puts("\n");
}

static void sh_ls_print_long(const char *name, const au_stat_t *st, const au_dirent_t *de, const sh_ls_opts_t *opts) {
    unsigned int type = st ? st->type : de->type;
    unsigned int mode = st ? st->mode : 0;
    sh_print_mode(mode, type);
    sh_puts(" ");
    sh_print_u64(st ? st->nlink : 1u);
    sh_puts(" ");
    sh_print_u64(st ? st->uid : 0u);
    sh_puts(":");
    sh_print_u64(st ? st->gid : 0u);
    sh_puts(" ");
    sh_print_u64(st ? st->size : de->size);
    sh_puts(" ");
    if (opts && opts->show_inode) { sh_print_u64(st ? st->inode : de->inode); sh_puts(" "); }
    sh_puts(name);
    sh_ls_print_suffix(type, opts);
    sh_puts("\n");
}

static int sh_ls_one_path(const char *path, const sh_ls_opts_t *opts) {
    au_stat_t st;
    int sr = sh_stat_path(path, &st, 1);
    if (sr != 0) { sh_perror2("ls", path, sr); return 1; }
    if (opts->directory || st.type != SH_NODE_DIR) {
        au_dirent_t fake;
        au_memset(&fake, 0, sizeof(fake));
        fake.type = st.type;
        fake.size = st.size;
        fake.inode = st.inode;
        if (opts->long_view) sh_ls_print_long(path, &st, &fake, opts);
        else sh_ls_print_short(path, st.type, opts);
        return 0;
    }

    au_i64 fd = au_open2(path, RABBITBONE_O_DIRECTORY);
    if (fd < 0) { sh_perror2("ls", path, fd); return 1; }
    if (opts->multi_path) { sh_puts(path); sh_puts(":\n"); }
    int rc = 0;
    for (au_u64 i = 0; ; ++i) {
        au_dirent_t de;
        au_memset(&de, 0, sizeof(de));
        au_i64 got = au_readdir(fd, i, &de);
        if (got < 0) { sh_perror2("ls", path, got); rc = 1; break; }
        if (got == 0) break;
        if (sh_ls_hidden_skip(de.name, opts)) continue;

        au_stat_t child_st;
        au_memset(&child_st, 0, sizeof(child_st));
        int have_stat = 0;
        if (sh_ls_join_path(path, de.name, sh_fs_path, sizeof(sh_fs_path))) have_stat = au_stat(sh_fs_path, &child_st) == 0;
        if (opts->long_view) sh_ls_print_long(de.name, have_stat ? &child_st : 0, &de, opts);
        else sh_ls_print_short(de.name, have_stat ? child_st.type : de.type, opts);
    }
    if (au_close(fd) != 0 && rc == 0) rc = 1;
    return rc;
}

static int sh_builtin_ls(int argc, char **argv) {
    sh_ls_opts_t opts;
    au_memset(&opts, 0, sizeof(opts));
    opts.one_per_line = 1;
    const char *paths[SH_ARG_MAX];
    unsigned int path_count = 0;
    int parse_opts = 1;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) {
            sh_puts("usage: ls [-aAldi1F] [PATH...]\n");
            return 0;
        }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'a') opts.all = 1;
                else if (*p == 'A') opts.almost_all = 1;
                else if (*p == 'l') opts.long_view = 1;
                else if (*p == 'd') opts.directory = 1;
                else if (*p == 'i') opts.show_inode = 1;
                else if (*p == '1') opts.one_per_line = 1;
                else if (*p == 'F') opts.classify = 1;
                else { sh_err("ls: unknown option -- "); char b[2] = { *p, 0 }; sh_err(b); sh_err("\n"); return 2; }
            }
            continue;
        }
        if (path_count >= SH_ARG_MAX) { sh_err("ls: too many paths\n"); return 2; }
        paths[path_count++] = arg;
    }
    if (path_count == 0) paths[path_count++] = ".";
    opts.multi_path = path_count > 1u;
    int rc = 0;
    for (unsigned int i = 0; i < path_count; ++i) {
        if (i > 0 && opts.multi_path) sh_puts("\n");
        int r = sh_ls_one_path(paths[i], &opts);
        if (r != 0) rc = r;
    }
    return rc;
}

static int sh_builtin_stat(int argc, char **argv) {
    int follow = 1;
    int i = 1;
    if (argc > 1 && sh_arg_is_help(argv[1])) { sh_puts("usage: stat [-L] PATH...\n"); return 0; }
    if (argc > 1 && sh_streq(argv[1], "-L")) { follow = 1; i = 2; }
    if (i >= argc) { sh_err("usage: stat [-L] PATH...\n"); return 1; }
    int rc = 0;
    for (; i < argc; ++i) {
        au_stat_t st;
        int sr = sh_stat_path(argv[i], &st, follow);
        if (sr != 0) { sh_perror2("stat", argv[i], sr); rc = 1; continue; }
        sh_puts(argv[i]);
        sh_puts(": type="); sh_puts(sh_node_type(st.type));
        sh_puts(" size="); sh_print_u64(st.size);
        sh_puts(" mode="); sh_print_u64(st.mode); sh_puts(" "); sh_print_mode(st.mode, st.type);
        sh_puts(" owner="); sh_print_u64(st.uid); sh_puts(":"); sh_print_u64(st.gid);
        sh_puts(" inode="); sh_print_u64(st.inode);
        sh_puts(" fs="); sh_print_u64(st.fs_id);
        sh_puts(" nlink="); sh_print_u64(st.nlink);
        sh_puts("\n");
    }
    return rc;
}

static int sh_buffer_is_binary(const unsigned char *buf, au_usize n);

static int sh_waitable_input(au_i64 in) {
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(in, &fi) != 0) return 0;
    return sh_startswith(fi.path, "pipe:") || sh_startswith(fi.path, "console:");
}

typedef struct sh_cat_state {
    int number_all;
    int number_nonblank;
    int squeeze_blank;
    int show_ends;
    au_u64 line_no;
    int at_line_start;
    int previous_blank;
    int suppressing_blank;
} sh_cat_state_t;

static void sh_cat_print_line_no(au_u64 n) {
    char buf[24];
    sh_u64(n, buf, sizeof(buf));
    au_usize len = au_strlen(buf);
    for (au_usize i = len; i < 6u; ++i) sh_puts(" ");
    sh_puts(buf);
    sh_puts("  ");
}

static int sh_cat_write_data(const unsigned char *buf, au_usize n, sh_cat_state_t *st) {
    if (!buf || !st) return 1;
    for (au_usize i = 0; i < n; ++i) {
        unsigned char c = buf[i];
        if (st->at_line_start) {
            int blank = c == '\n';
            if (blank && st->squeeze_blank && st->previous_blank) { st->suppressing_blank = 1; continue; }
            st->suppressing_blank = 0;
            if (st->number_all || (st->number_nonblank && !blank)) sh_cat_print_line_no(st->line_no++);
        }
        if (c == '\n') {
            if (st->show_ends) sh_puts("$");
            sh_puts("\n");
            st->previous_blank = st->at_line_start ? 1 : 0;
            st->at_line_start = 1;
            continue;
        }
        st->previous_blank = 0;
        st->at_line_start = 0;
        if (c == '\t' || (c >= 32u && c <= 126u)) {
            char b[2]; b[0] = (char)c; b[1] = 0; sh_puts(b);
        } else {
            sh_print_byte_escaped(c);
        }
    }
    return 0;
}

static int sh_cat_copy_fd(au_i64 in, const char *path, au_u64 size, int regular, sh_cat_state_t *state) {
    int waitable = sh_waitable_input(in);
    au_i64 got = au_read(in, sh_fs_buf, sizeof(sh_fs_buf));
    if (got < 0) return 1;
    if (regular && sh_buffer_is_binary((const unsigned char *)sh_fs_buf, (au_usize)got)) {
        sh_err("cat: "); sh_write_fd((au_i64)RABBITBONE_STDERR, path ? path : "file");
        sh_err(": binary file, not dumping to terminal; use 'stat' or 'elf'\n");
        (void)size;
        return 1;
    }
    if (got > 0 && sh_cat_write_data((const unsigned char *)sh_fs_buf, (au_usize)got, state) != 0) return 1;
    for (;;) {
        got = au_read(in, sh_fs_buf, sizeof(sh_fs_buf));
        if (got < 0) return 1;
        if (got == 0) {
            if (!waitable) return 0;
            au_i64 ev = au_poll(in, RABBITBONE_POLL_READ | RABBITBONE_POLL_HUP);
            if (ev < 0) return 1;
            if ((ev & RABBITBONE_POLL_HUP) != 0 && (ev & RABBITBONE_POLL_READ) == 0) return 0;
            if ((ev & RABBITBONE_POLL_READ) == 0) { (void)au_sleep(1); continue; }
            continue;
        }
        if (sh_cat_write_data((const unsigned char *)sh_fs_buf, (au_usize)got, state) != 0) return 1;
    }
}

static int sh_buffer_is_binary(const unsigned char *buf, au_usize n) {
    if (!buf || n == 0) return 0;
    if (n >= 4u && buf[0] == 0x7fu && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F') return 1;
    unsigned int suspicious = 0;
    for (au_usize i = 0; i < n; ++i) {
        unsigned char c = buf[i];
        if (c == 0u) return 1;
        if (c < 32u && c != '\n' && c != '\r' && c != '\t' && c != '\b') ++suspicious;
    }
    return suspicious > n / 16u;
}

static int sh_builtin_cat(int argc, char **argv) {
    sh_cat_state_t state;
    au_memset(&state, 0, sizeof(state));
    state.line_no = 1;
    state.at_line_start = 1;
    int parse_opts = 1;
    int first_path = 1;
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: cat [-nbsE] [FILE...]\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            if (arg[1] == 0) { }
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'n') state.number_all = 1;
                else if (*p == 'b') state.number_nonblank = 1;
                else if (*p == 's') state.squeeze_blank = 1;
                else if (*p == 'E') state.show_ends = 1;
                else { sh_err("cat: unknown option -- "); char b[2] = { *p, 0 }; sh_err(b); sh_err("\n"); return 2; }
            }
            continue;
        }
        first_path = 0;
        if (sh_streq(arg, "-")) {
            if (sh_cat_copy_fd((au_i64)RABBITBONE_STDIN, "stdin", 0, 0, &state) != 0) rc = 1;
            continue;
        }
        au_stat_t st;
        int sr = sh_stat_path(arg, &st, 1);
        if (sr != 0) { sh_perror2("cat", arg, sr); rc = 1; continue; }
        if (st.type != SH_NODE_FILE) { sh_perror2("cat", arg, RABBITBONE_ERR_ISDIR); rc = 1; continue; }
        au_i64 fd = au_open(arg);
        if (fd < 0) { sh_perror2("cat", arg, fd); rc = 1; continue; }
        int r = sh_cat_copy_fd(fd, arg, st.size, 1, &state);
        if (au_close(fd) != 0 && r == 0) r = 1;
        if (r != 0) rc = r;
    }
    if (first_path) return sh_cat_copy_fd((au_i64)RABBITBONE_STDIN, "stdin", 0, 0, &state);
    return rc;
}

static int sh_echo_escape(const char *s) {
    for (const char *p = s; p && *p; ++p) {
        if (*p != '\\') { char b[2] = { *p, 0 }; sh_puts(b); continue; }
        ++p;
        if (!*p) { sh_puts("\\"); break; }
        if (*p == 'n') sh_puts("\n");
        else if (*p == 't') sh_puts("\t");
        else if (*p == 'r') sh_puts("\r");
        else if (*p == '\\') sh_puts("\\");
        else if (*p == 'c') return 1;
        else { sh_puts("\\"); char b[2] = { *p, 0 }; sh_puts(b); }
    }
    return 0;
}

static int sh_builtin_echo(int argc, char **argv) {
    int newline = 1;
    int escapes = 0;
    int i = 1;
    for (; i < argc; ++i) {
        if (sh_streq(argv[i], "-n")) { newline = 0; continue; }
        if (sh_streq(argv[i], "-e")) { escapes = 1; continue; }
        if (sh_streq(argv[i], "-E")) { escapes = 0; continue; }
        break;
    }
    for (; i < argc; ++i) {
        if (i > 1) sh_puts(" ");
        if (escapes) { if (sh_echo_escape(argv[i])) return 0; }
        else sh_puts(argv[i]);
    }
    if (newline) sh_puts("\n");
    return 0;
}

static int sh_builtin_pwd(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (sh_arg_is_help(argv[i])) { sh_puts("usage: pwd [-L|-P]\n"); return 0; }
        if (!sh_streq(argv[i], "-L") && !sh_streq(argv[i], "-P")) { sh_err("usage: pwd [-L|-P]\n"); return 2; }
    }
    char cwd[SH_PATH_MAX];
    if (au_getcwd(cwd, sizeof(cwd)) < 0) return 1;
    sh_puts(cwd); sh_puts("\n");
    return 0;
}

static int sh_builtin_cd(int argc, char **argv) {
    const char *path = 0;
    int print_new = 0;
    if (argc > 2) { sh_err("usage: cd [PATH|-]\n"); return 1; }
    if (argc == 1) path = sh_var_get("HOME");
    else if (sh_arg_is_help(argv[1])) { sh_puts("usage: cd [PATH|-]\n"); return 0; }
    else if (sh_streq(argv[1], "-")) { path = sh_var_get("OLDPWD"); print_new = 1; if (!path || !*path) { sh_err("cd: OLDPWD not set\n"); return 1; } }
    else path = argv[1];
    char old[SH_PATH_MAX];
    if (au_getcwd(old, sizeof(old)) < 0) sh_copy(old, sizeof(old), "");
    au_i64 r = au_chdir(path && *path ? path : "/");
    if (r < 0) { sh_perror2("cd", path, r); return 1; }
    char now[SH_PATH_MAX];
    if (au_getcwd(now, sizeof(now)) == 0) {
        (void)sh_var_set("OLDPWD", old, 1);
        (void)sh_var_set("PWD", now, 1);
        if (print_new) { sh_puts(now); sh_puts("\n"); }
    }
    return 0;
}

static int sh_mkdir_p(const char *path, int verbose) {
    if (!path || !*path) return RABBITBONE_ERR_INVAL;
    if (sh_copy(sh_fs_path, sizeof(sh_fs_path), path) == 0) return RABBITBONE_ERR_INVAL;
    au_usize len = au_strlen(sh_fs_path);
    while (len > 1u && sh_fs_path[len - 1u] == '/') sh_fs_path[--len] = 0;
    for (au_usize i = 1u; sh_fs_path[i]; ++i) {
        if (sh_fs_path[i] != '/') continue;
        sh_fs_path[i] = 0;
        if (!sh_path_exists(sh_fs_path, 1)) {
            au_i64 r = au_mkdir(sh_fs_path);
            if (r != 0) return (int)r;
            if (verbose) { sh_puts("mkdir: created '"); sh_puts(sh_fs_path); sh_puts("'\n"); }
        }
        sh_fs_path[i] = '/';
    }
    au_stat_t st;
    int sr = sh_stat_path(sh_fs_path, &st, 1);
    if (sr == 0) return st.type == SH_NODE_DIR ? 0 : RABBITBONE_ERR_EXIST;
    au_i64 r = au_mkdir(sh_fs_path);
    if (r == 0 && verbose) { sh_puts("mkdir: created '"); sh_puts(sh_fs_path); sh_puts("'\n"); }
    return (int)r;
}

static int sh_builtin_mkdir(int argc, char **argv) {
    int parents = 0;
    int verbose = 0;
    int parse_opts = 1;
    int rc = 0;
    int paths = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: mkdir [-pv] PATH...\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'p') parents = 1;
                else if (*p == 'v') verbose = 1;
                else { sh_err("mkdir: unknown option\n"); return 2; }
            }
            continue;
        }
        ++paths;
        int r = parents ? sh_mkdir_p(arg, verbose) : (int)au_mkdir(arg);
        if (r != 0) { sh_perror2("mkdir", arg, r); rc = 1; }
        else if (verbose && !parents) { sh_puts("mkdir: created '"); sh_puts(arg); sh_puts("'\n"); }
    }
    if (!paths) { sh_err("usage: mkdir [-pv] PATH...\n"); return 1; }
    return rc;
}

static int sh_rm_recursive(const char *path, int recursive, int force, int verbose, unsigned int depth) {
    if (!path || !*path) return RABBITBONE_ERR_INVAL;
    if (sh_is_dot_path(path)) return RABBITBONE_ERR_PERM;
    au_stat_t st;
    int sr = sh_stat_path(path, &st, 0);
    if (sr != 0) return force && sr == RABBITBONE_ERR_NOENT ? 0 : sr;
    if (st.type == SH_NODE_DIR) {
        if (!recursive) return RABBITBONE_ERR_ISDIR;
        if (depth > 32u) return RABBITBONE_ERR_INVAL;
        au_i64 fd = au_open2(path, RABBITBONE_O_DIRECTORY);
        if (fd < 0) return (int)fd;
        for (au_u64 i = 0; ; ++i) {
            au_dirent_t de;
            au_memset(&de, 0, sizeof(de));
            au_i64 got = au_readdir(fd, i, &de);
            if (got < 0) { (void)au_close(fd); return (int)got; }
            if (got == 0) break;
            if (sh_streq(de.name, ".") || sh_streq(de.name, "..")) continue;
            char child_path[SH_PATH_MAX];
            if (!sh_ls_join_path(path, de.name, child_path, sizeof(child_path))) { (void)au_close(fd); return RABBITBONE_ERR_INVAL; }
            int r = sh_rm_recursive(child_path, recursive, force, verbose, depth + 1u);
            if (r != 0) { (void)au_close(fd); return r; }
        }
        (void)au_close(fd);
    }
    au_i64 r = au_unlink(path);
    if (r == 0 && verbose) { sh_puts("removed '"); sh_puts(path); sh_puts("'\n"); }
    return (int)r;
}

static int sh_builtin_rm(int argc, char **argv) {
    int recursive = 0;
    int force = 0;
    int verbose = 0;
    int parse_opts = 1;
    int paths = 0;
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: rm [-frv] PATH...\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'r' || *p == 'R') recursive = 1;
                else if (*p == 'f') force = 1;
                else if (*p == 'v') verbose = 1;
                else { sh_err("rm: unknown option\n"); return 2; }
            }
            continue;
        }
        ++paths;
        int r = sh_rm_recursive(arg, recursive, force, verbose, 0);
        if (r != 0) { if (!force) sh_perror2("rm", arg, r); rc = 1; }
    }
    if (!paths && !force) { sh_err("usage: rm [-frv] PATH...\n"); return 1; }
    return rc;
}

static int sh_builtin_mv(int argc, char **argv) {
    int no_clobber = 0;
    int verbose = 0;
    int parse_opts = 1;
    const char *src = 0;
    const char *dst = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: mv [-nv] OLD NEW\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'n') no_clobber = 1;
                else if (*p == 'f') no_clobber = 0;
                else if (*p == 'v') verbose = 1;
                else { sh_err("mv: unknown option\n"); return 2; }
            }
            continue;
        }
        if (!src) src = arg;
        else if (!dst) dst = arg;
        else { sh_err("usage: mv [-nv] OLD NEW\n"); return 1; }
    }
    if (!src || !dst) { sh_err("usage: mv [-nv] OLD NEW\n"); return 1; }
    if (no_clobber && sh_path_exists(dst, 0)) return 0;
    au_i64 r = au_rename(src, dst);
    if (r != 0) { sh_perror2("mv", src, r); return 1; }
    if (verbose) { sh_puts("renamed '"); sh_puts(src); sh_puts("' -> '"); sh_puts(dst); sh_puts("'\n"); }
    return 0;
}

static int sh_copy_file_data(au_i64 in, au_i64 out) {
    for (;;) {
        au_i64 got = au_read(in, sh_fs_buf, sizeof(sh_fs_buf));
        if (got < 0) return 1;
        if (got == 0) return 0;
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write(out, sh_fs_buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 1;
            off += wrote;
        }
    }
}

static int sh_builtin_cp(int argc, char **argv) {
    int force = 0;
    int no_clobber = 0;
    int verbose = 0;
    int parse_opts = 1;
    const char *src = 0;
    const char *dst = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: cp [-fnv] SRC DST\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'f') force = 1;
                else if (*p == 'n') no_clobber = 1;
                else if (*p == 'v') verbose = 1;
                else { sh_err("cp: unknown option\n"); return 2; }
            }
            continue;
        }
        if (!src) src = arg;
        else if (!dst) dst = arg;
        else { sh_err("usage: cp [-fnv] SRC DST\n"); return 1; }
    }
    if (!src || !dst) { sh_err("usage: cp [-fnv] SRC DST\n"); return 1; }
    au_stat_t st;
    int sr = sh_stat_path(src, &st, 1);
    if (sr != 0) { sh_perror2("cp", src, sr); return 1; }
    if (st.type != SH_NODE_FILE) { sh_perror2("cp", src, RABBITBONE_ERR_ISDIR); return 1; }
    if (no_clobber && sh_path_exists(dst, 0)) return 0;
    if (force) (void)au_unlink(dst);
    au_i64 in = au_open(src);
    if (in < 0) { sh_perror2("cp", src, in); return 1; }
    au_i64 out = au_open2(dst, RABBITBONE_O_WRONLY | RABBITBONE_O_CREAT | RABBITBONE_O_TRUNC);
    if (out < 0) { (void)au_close(in); sh_perror2("cp", dst, out); return 1; }
    int rc = sh_copy_file_data(in, out);
    if (au_close(in) != 0 && rc == 0) rc = 1;
    if (au_close(out) != 0 && rc == 0) rc = 1;
    if (rc) { sh_err("cp: copy failed\n"); return 1; }
    if (verbose) { sh_puts("'"); sh_puts(src); sh_puts("' -> '"); sh_puts(dst); sh_puts("'\n"); }
    return 0;
}

static int sh_builtin_touch(int argc, char **argv) {
    int no_create = 0;
    int verbose = 0;
    int parse_opts = 1;
    int paths = 0;
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: touch [-cv] PATH...\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'c') no_create = 1;
                else if (*p == 'v') verbose = 1;
                else { sh_err("touch: unknown option\n"); return 2; }
            }
            continue;
        }
        ++paths;
        if (sh_path_exists(arg, 1)) { if (verbose) { sh_puts("touch: retained '"); sh_puts(arg); sh_puts("'\n"); } continue; }
        if (no_create) continue;
        au_i64 r = au_create(arg, "", 0);
        if (r != 0) { sh_perror2("touch", arg, r); rc = 1; }
        else if (verbose) { sh_puts("touch: created '"); sh_puts(arg); sh_puts("'\n"); }
    }
    if (!paths) { sh_err("usage: touch [-cv] PATH...\n"); return 1; }
    return rc;
}

static int sh_builtin_write_text(int argc, char **argv, int create) {
    if (create) return sh_builtin_touch(argc, argv);
    int append = 0;
    int no_newline = 1;
    int parse_opts = 1;
    int path_i = 0;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (parse_opts && sh_is_option_end(arg)) { parse_opts = 0; continue; }
        if (parse_opts && sh_arg_is_help(arg)) { sh_puts("usage: write [-an] PATH TEXT...\n"); return 0; }
        if (parse_opts && arg[0] == '-' && arg[1]) {
            for (const char *p = arg + 1; *p; ++p) {
                if (*p == 'a') append = 1;
                else if (*p == 'n') no_newline = 1;
                else if (*p == 'N') no_newline = 0;
                else { sh_err("write: unknown option\n"); return 2; }
            }
            continue;
        }
        path_i = i;
        break;
    }
    if (path_i == 0 || path_i + 1 >= argc) { sh_err("usage: write [-a] PATH TEXT...\n"); return 1; }
    au_usize off = 0;
    sh_fs_buf[0] = 0;
    for (int i = path_i + 1; i < argc; ++i) {
        if (i > path_i + 1) { if (!sh_append_char(sh_fs_buf, sizeof(sh_fs_buf), &off, ' ')) { sh_err("write: text too long\n"); return 1; } }
        if (!sh_append_str(sh_fs_buf, sizeof(sh_fs_buf), &off, argv[i])) { sh_err("write: text too long\n"); return 1; }
    }
    if (!no_newline && !sh_append_char(sh_fs_buf, sizeof(sh_fs_buf), &off, '\n')) { sh_err("write: text too long\n"); return 1; }
    unsigned int flags = RABBITBONE_O_WRONLY | RABBITBONE_O_CREAT | (append ? RABBITBONE_O_APPEND : RABBITBONE_O_TRUNC);
    au_i64 fd = au_open2(argv[path_i], flags);
    if (fd < 0) { sh_perror2("write", argv[path_i], fd); return 1; }
    au_i64 wr = off ? au_write(fd, sh_fs_buf, off) : 0;
    int rc = (wr < 0 || (au_usize)wr != off) ? 1 : 0;
    if (au_close(fd) != 0 && rc == 0) rc = 1;
    if (rc) sh_err("write: failed\n");
    return rc;
}

static int sh_builtin_truncate(int argc, char **argv) {
    au_u64 size = 0;
    if (argc > 1 && sh_arg_is_help(argv[1])) { sh_puts("usage: truncate PATH SIZE\n"); return 0; }
    if (argc != 3) { sh_err("usage: truncate PATH SIZE\n"); return 1; }
    if (!sh_parse_u64(argv[2], &size)) { sh_err("truncate: invalid size\n"); return 1; }
    au_i64 r = au_truncate(argv[1], size);
    if (r != 0) { sh_perror2("truncate", argv[1], r); return 1; }
    return 0;
}

static int sh_builtin_link(int argc, char **argv) {
    if (argc > 1 && sh_arg_is_help(argv[1])) { sh_puts("usage: link OLD NEW\n"); return 0; }
    if (argc != 3) { sh_err("usage: link OLD NEW\n"); return 1; }
    au_i64 r = au_link(argv[1], argv[2]);
    if (r != 0) { sh_perror2("link", argv[1], r); return 1; }
    return 0;
}

static int sh_builtin_symlink(int argc, char **argv) {
    if (argc > 1 && sh_arg_is_help(argv[1])) { sh_puts("usage: symlink TARGET LINKPATH\n"); return 0; }
    if (argc != 3) { sh_err("usage: symlink TARGET LINKPATH\n"); return 1; }
    au_i64 r = au_symlink(argv[1], argv[2]);
    if (r != 0) { sh_perror2("symlink", argv[2], r); return 1; }
    return 0;
}

static int sh_builtin_readlink(int argc, char **argv) {
    char out[SH_PATH_MAX];
    if (argc > 1 && sh_arg_is_help(argv[1])) { sh_puts("usage: readlink PATH\n"); return 0; }
    if (argc != 2) { sh_err("usage: readlink PATH\n"); return 1; }
    au_memset(out, 0, sizeof(out));
    au_i64 r = au_readlink(argv[1], out, sizeof(out) - 1u);
    if (r < 0) { sh_perror2("readlink", argv[1], r); return 1; }
    out[(au_usize)r] = 0;
    sh_puts(out); sh_puts("\n");
    return 0;
}
