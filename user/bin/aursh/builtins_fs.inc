static int sh_ls_join_path(const char *dir, const char *name, char *out, au_usize cap) {
    if (!dir || !name || !out || cap == 0) return 0;
    if (sh_streq(dir, "/")) {
        if (2u + au_strlen(name) > cap) return 0;
        out[0] = '/';
        sh_copy(out + 1, cap - 1u, name);
        return 1;
    }
    au_usize len = au_strlen(dir);
    au_usize nlen = au_strlen(name);
    if (len == 0) return 0;
    if (len + 1u + nlen + 1u > cap) return 0;
    au_memcpy(out, dir, len);
    out[len] = '/';
    sh_copy(out + len + 1u, cap - len - 1u, name);
    return 1;
}

static void sh_ls_print_name(const char *name, unsigned int type) {
    sh_puts(name);
    if (type == SH_NODE_DIR) sh_puts("/");
    sh_puts("\n");
}

static void sh_ls_print_long(const char *name, const au_stat_t *st, const au_dirent_t *de) {
    unsigned int type = st ? st->type : de->type;
    sh_puts(name);
    if (type == SH_NODE_DIR) sh_puts("/");
    if (st) {
        sh_puts("  size="); sh_print_u64(st->size);
        sh_puts("  type="); sh_puts(sh_node_type(st->type));
        sh_puts("  owner="); sh_print_u64(st->uid); sh_puts(":"); sh_print_u64(st->gid);
        sh_puts("  mode="); sh_print_mode(st->mode, st->type);
    } else {
        sh_puts("  size="); sh_print_u64(de->size);
        sh_puts("  type="); sh_puts(sh_node_type(de->type));
    }
    sh_puts("\n");
}

static int sh_builtin_ls(int argc, char **argv) {
    const char *path = ".";
    int long_view = 0;
    int one_per_line = 1;
    for (int i = 1; i < argc; ++i) {
        if (sh_streq(argv[i], "-l") || sh_streq(argv[i], "--long")) long_view = 1;
        else if (sh_streq(argv[i], "-1")) one_per_line = 1;
        else if (argv[i][0] == '-') { sh_err("usage: ls [-l] [PATH]\n"); return 1; }
        else path = argv[i];
    }
    (void)one_per_line;
    au_i64 fd = au_open2(path, AURORA_O_DIRECTORY);
    if (fd < 0) {
        sh_err("ls: open failed: ");
        sh_write_fd((au_i64)AURORA_STDERR, path);
        sh_err("\n");
        return 1;
    }
    for (au_u64 i = 0; ; ++i) {
        au_dirent_t de;
        au_memset(&de, 0, sizeof(de));
        au_i64 got = au_readdir(fd, i, &de);
        if (got < 0) { sh_err("ls: readdir failed\n"); (void)au_close(fd); return 1; }
        if (got == 0) break;

        char child_path[SH_PATH_MAX];
        au_stat_t st;
        au_memset(&st, 0, sizeof(st));
        int have_stat = 0;
        if (sh_ls_join_path(path, de.name, child_path, sizeof(child_path))) {
            have_stat = au_stat(child_path, &st) == 0;
        }

        if (long_view) sh_ls_print_long(de.name, have_stat ? &st : 0, &de);
        else sh_ls_print_name(de.name, have_stat ? st.type : de.type);
    }
    return au_close(fd) == 0 ? 0 : 1;
}

static int sh_builtin_stat(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: stat PATH\n"); return 1; }
    au_stat_t st;
    au_memset(&st, 0, sizeof(st));
    if (au_stat(argv[1], &st) != 0) { sh_err("stat: failed: "); sh_write_fd((au_i64)AURORA_STDERR, argv[1]); sh_err("\n"); return 1; }
    sh_puts(argv[1]);
    sh_puts(": type="); sh_puts(sh_node_type(st.type));
    sh_puts(" size="); sh_print_u64(st.size);
    sh_puts(" mode="); sh_print_u64(st.mode); sh_puts(" "); sh_print_mode(st.mode, st.type);
    sh_puts(" owner="); sh_print_u64(st.uid); sh_puts(":"); sh_print_u64(st.gid);
    sh_puts(" inode="); sh_print_u64(st.inode);
    sh_puts(" fs="); sh_print_u64(st.fs_id);
    sh_puts(" nlink="); sh_print_u64(st.nlink);
    sh_puts("\n");
    return 0;
}

static int sh_waitable_input(au_i64 in) {
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(in, &fi) != 0) return 0;
    return sh_startswith(fi.path, "pipe:") || sh_startswith(fi.path, "console:");
}

static int sh_copy_fd_to_stdout(au_i64 in) {
    char buf[192];
    int waitable = sh_waitable_input(in);
    for (;;) {
        au_i64 got = au_read(in, buf, sizeof(buf));
        if (got < 0) return 1;
        if (got == 0) {
            if (!waitable) return 0;
            au_i64 ev = au_poll(in, AURORA_POLL_READ | AURORA_POLL_HUP);
            if (ev < 0) return 1;
            if ((ev & AURORA_POLL_HUP) != 0 && (ev & AURORA_POLL_READ) == 0) return 0;
            if ((ev & AURORA_POLL_READ) == 0) { (void)au_sleep(1); continue; }
            continue;
        }
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write((au_i64)AURORA_STDOUT, buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 1;
            off += wrote;
        }
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

static int sh_copy_regular_file_to_stdout(au_i64 fd, const char *path, au_u64 size) {
    unsigned char buf[192];
    au_i64 got = au_read(fd, buf, sizeof(buf));
    if (got < 0) return 1;
    if (sh_buffer_is_binary(buf, (au_usize)got)) {
        sh_err("cat: ");
        sh_write_fd((au_i64)AURORA_STDERR, path ? path : "file");
        sh_err(": binary file, not dumping to terminal; use 'stat' or 'elf'\n");
        (void)size;
        return 1;
    }
    if (got > 0) {
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write((au_i64)AURORA_STDOUT, buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 1;
            off += wrote;
        }
    }
    return sh_copy_fd_to_stdout(fd);
}

static int sh_builtin_cat(int argc, char **argv) {
    if (argc <= 1) return sh_copy_fd_to_stdout((au_i64)AURORA_STDIN);
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        au_stat_t st;
        au_memset(&st, 0, sizeof(st));
        if (au_stat(argv[i], &st) != 0) {
            sh_err("cat: stat failed: ");
            sh_write_fd((au_i64)AURORA_STDERR, argv[i]);
            sh_err("\n");
            rc = 1;
            continue;
        }
        if (st.type != SH_NODE_FILE) {
            sh_err("cat: not a regular file: ");
            sh_write_fd((au_i64)AURORA_STDERR, argv[i]);
            sh_err("\n");
            rc = 1;
            continue;
        }
        au_i64 fd = au_open(argv[i]);
        if (fd < 0) {
            sh_err("cat: open failed: ");
            sh_write_fd((au_i64)AURORA_STDERR, argv[i]);
            sh_err("\n");
            rc = 1;
            continue;
        }
        int r = sh_copy_regular_file_to_stdout(fd, argv[i], st.size);
        if (au_close(fd) != 0 && r == 0) r = 1;
        if (r != 0) rc = r;
    }
    return rc;
}

static int sh_builtin_echo(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        if (i > 1) sh_puts(" ");
        sh_puts(argv[i]);
    }
    sh_puts("\n");
    return 0;
}

static int sh_builtin_cd(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    au_i64 r = au_chdir(path);
    if (r < 0) {
        sh_err("cd: failed: ");
        sh_write_fd((au_i64)AURORA_STDERR, path);
        sh_err("\n");
        return 1;
    }
    return 0;
}

static int sh_builtin_write_text(int argc, char **argv, int create) {
    if (argc < 2 || (create && argc < 2)) { sh_err(create ? "usage: touch PATH [TEXT...]\n" : "usage: write PATH TEXT...\n"); return 1; }
    if (!create && argc < 3) { sh_err("usage: write PATH TEXT...\n"); return 1; }
    char text[SH_LINE_MAX];
    text[0] = 0;
    au_usize off = 0;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) {
            if (off + 1u >= sizeof(text)) return 1;
            text[off++] = ' ';
        }
        au_usize n = au_strlen(argv[i]);
        if (off + n + 1u > sizeof(text)) { sh_err(create ? "touch: text too long\n" : "write: text too long\n"); return 1; }
        au_memcpy(text + off, argv[i], n);
        off += n;
        text[off] = 0;
    }
    if (create) {
        au_i64 r = au_create(argv[1], text, off);
        if (r != 0) { sh_err("touch: create failed\n"); return 1; }
        return 0;
    }
    au_i64 fd = au_open2(argv[1], AURORA_O_WRONLY | AURORA_O_TRUNC);
    if (fd < 0) { sh_err("write: open failed\n"); return 1; }
    au_i64 wr = off ? au_write(fd, text, off) : 0;
    int rc = (wr < 0 || (au_usize)wr != off) ? 1 : 0;
    if (au_close(fd) != 0 && rc == 0) rc = 1;
    if (rc) sh_err("write: failed\n");
    return rc;
}

static int sh_builtin_mkdir(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: mkdir PATH\n"); return 1; }
    if (au_mkdir(argv[1]) != 0) { sh_err("mkdir: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_rm(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: rm PATH\n"); return 1; }
    if (au_unlink(argv[1]) != 0) { sh_err("rm: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_mv(int argc, char **argv) {
    if (argc != 3) { sh_err("usage: mv OLD NEW\n"); return 1; }
    if (au_rename(argv[1], argv[2]) != 0) { sh_err("mv: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_truncate(int argc, char **argv) {
    au_u64 size = 0;
    if (argc != 3) { sh_err("usage: truncate PATH SIZE\n"); return 1; }
    if (!sh_parse_u64(argv[2], &size)) { sh_err("truncate: invalid size\n"); return 1; }
    if (au_truncate(argv[1], size) != 0) { sh_err("truncate: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_link(int argc, char **argv) {
    if (argc != 3) { sh_err("usage: link OLD NEW\n"); return 1; }
    if (au_link(argv[1], argv[2]) != 0) { sh_err("link: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_symlink(int argc, char **argv) {
    if (argc != 3) { sh_err("usage: symlink TARGET LINKPATH\n"); return 1; }
    if (au_symlink(argv[1], argv[2]) != 0) { sh_err("symlink: failed\n"); return 1; }
    return 0;
}

static int sh_builtin_readlink(int argc, char **argv) {
    char out[SH_PATH_MAX];
    if (argc != 2) { sh_err("usage: readlink PATH\n"); return 1; }
    au_memset(out, 0, sizeof(out));
    au_i64 r = au_readlink(argv[1], out, sizeof(out) - 1u);
    if (r < 0) { sh_err("readlink: failed\n"); return 1; }
    out[(au_usize)r] = 0;
    sh_puts(out); sh_puts("\n");
    return 0;
}


