static int sh_builtin_fdprobe(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: fdprobe PATH\n"); return 1; }
    au_i64 fd = au_open(argv[1]);
    if (fd < 0) { sh_err("fdprobe: open failed\n"); return 1; }
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(fd, &fi) != 0) { sh_err("fdprobe: fdinfo failed\n"); (void)au_close(fd); return 1; }
    sh_puts("fd="); sh_print_i64(fd);
    sh_puts(" type="); sh_print_u64(fi.type);
    sh_puts(" offset="); sh_print_u64(fi.offset);
    sh_puts(" size="); sh_print_u64(fi.size);
    sh_puts(" inode="); sh_print_u64(fi.inode);
    sh_puts(" fs="); sh_print_u64(fi.fs_id);
    sh_puts(" flags="); sh_print_u64(fi.flags);
    sh_puts(" open_flags="); sh_print_u64(fi.open_flags);
    sh_puts(" path="); sh_puts(fi.path);
    sh_puts("\n");
    (void)au_close(fd);
    return 0;
}

static int sh_builtin_fdinfo(int argc, char **argv) {
    unsigned int fd = 0;
    if (argc != 2 || !sh_parse_u32(argv[1], &fd)) { sh_err("usage: fdinfo FD\n"); return 1; }
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo((au_i64)fd, &fi) != 0) { sh_err("fdinfo: failed\n"); return 1; }
    sh_puts("fd="); sh_print_u64(fd);
    sh_puts(" type="); sh_print_u64(fi.type);
    sh_puts(" offset="); sh_print_u64(fi.offset);
    sh_puts(" size="); sh_print_u64(fi.size);
    sh_puts(" path="); sh_puts(fi.path);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_tty(void) {
    au_ttyinfo_t t;
    au_memset(&t, 0, sizeof(t));
    if (au_tty_getinfo(&t) != 0) { sh_err("tty: unavailable\n"); return 1; }
    sh_puts("tty: rows="); sh_print_u64(t.rows);
    sh_puts(" cols="); sh_print_u64(t.cols);
    sh_puts(" cursor="); sh_print_u64(t.cursor_row); sh_puts(","); sh_print_u64(t.cursor_col);
    sh_puts(" mode="); sh_print_u64(t.mode);
    sh_puts(" pending="); sh_print_u64(t.pending_keys);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_statvfs(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/";
    au_statvfs_t st;
    au_memset(&st, 0, sizeof(st));
    if (au_statvfs(path, &st) != 0) { sh_err("statvfs: failed\n"); return 1; }
    sh_puts("statvfs: fs="); sh_puts(st.fs_name);
    sh_puts(" mount="); sh_puts(st.mount_path);
    sh_puts(" block="); sh_print_u64(st.block_size);
    sh_puts(" total_blocks="); sh_print_u64(st.total_blocks);
    sh_puts(" free_blocks="); sh_print_u64(st.free_blocks);
    sh_puts(" inodes="); sh_print_u64(st.total_inodes);
    sh_puts(" free_inodes="); sh_print_u64(st.free_inodes);
    sh_puts(" flags="); sh_print_u64(st.flags);
    sh_puts(" max_name="); sh_print_u64(st.max_name_len);
    sh_puts("\n");
    return 0;
}


typedef struct sh_seen_mount {
    unsigned int fs_id;
    char mount_path[SH_PATH_MAX];
} sh_seen_mount_t;

static int sh_mount_seen_contains(const sh_seen_mount_t *seen, unsigned int count, const au_statvfs_t *st) {
    if (!seen || !st) return 0;
    for (unsigned int i = 0; i < count; ++i) {
        if (seen[i].fs_id == st->fs_id && sh_streq(seen[i].mount_path, st->mount_path)) return 1;
    }
    return 0;
}

static void sh_mount_print(const au_statvfs_t *st) {
    sh_puts(st->mount_path);
    sh_puts(" type="); sh_puts(st->fs_name);
    sh_puts(" fsid="); sh_print_u64(st->fs_id);
    sh_puts(" block="); sh_print_u64(st->block_size);
    sh_puts(" total="); sh_print_u64(st->total_blocks);
    sh_puts(" free="); sh_print_u64(st->free_blocks);
    sh_puts(" flags="); sh_print_u64(st->flags);
    sh_puts("\n");
}

static int sh_mount_record(const char *path, sh_seen_mount_t *seen, unsigned int *count, unsigned int cap) {
    au_statvfs_t st;
    au_memset(&st, 0, sizeof(st));
    if (!path || !seen || !count || au_statvfs(path, &st) != 0) return 0;
    if (sh_mount_seen_contains(seen, *count, &st)) return 0;
    sh_mount_print(&st);
    if (*count < cap) {
        seen[*count].fs_id = (unsigned int)st.fs_id;
        sh_copy(seen[*count].mount_path, sizeof(seen[*count].mount_path), st.mount_path);
        ++*count;
    }
    return 1;
}

static int sh_builtin_mounts(void) {
    sh_seen_mount_t seen[SH_STAGES_MAX * 4u];
    unsigned int count = 0;
    unsigned int printed = 0;
    au_memset(seen, 0, sizeof(seen));
    printed += (unsigned int)sh_mount_record("/", seen, &count, sizeof(seen) / sizeof(seen[0]));

    au_i64 fd = au_open2("/", AURORA_O_DIRECTORY);
    if (fd >= 0) {
        for (au_u64 i = 0; ; ++i) {
            au_dirent_t de;
            au_memset(&de, 0, sizeof(de));
            au_i64 got = au_readdir(fd, i, &de);
            if (got <= 0) break;
            char path[SH_PATH_MAX];
            if (sh_ls_join_path("/", de.name, path, sizeof(path))) {
                printed += (unsigned int)sh_mount_record(path, seen, &count, sizeof(seen) / sizeof(seen[0]));
            }
        }
        (void)au_close(fd);
    }

    if (!printed) { sh_err("mounts: unavailable\n"); return 1; }
    return 0;
}

static int sh_builtin_fsync(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: fsync PATH\n"); return 1; }
    au_i64 fd = au_open2(argv[1], AURORA_O_RDWR);
    if (fd < 0) { sh_err("fsync: open failed\n"); return 1; }
    int rc = au_fsync(fd) == 0 ? 0 : 1;
    if (au_close(fd) != 0 && rc == 0) rc = 1;
    if (rc) sh_err("fsync: failed\n");
    return rc;
}

static int sh_builtin_syscall(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "/etc/version";
    au_i64 fd = au_open(path);
    if (fd < 0) { sh_err("syscall: open failed\n"); return 1; }
    char buf[80];
    au_memset(buf, 0, sizeof(buf));
    au_i64 got = au_read(fd, buf, sizeof(buf) - 1u);
    if (got < 0) { sh_err("syscall: read failed\n"); (void)au_close(fd); return 1; }
    buf[(au_usize)got] = 0;
    sh_puts("syscall read("); sh_puts(path); sh_puts("): "); sh_puts(buf); sh_puts("\n");
    (void)au_close(fd);
    return 0;
}

typedef struct sh_elf64_ehdr {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    au_u64 entry;
    au_u64 phoff;
    au_u64 shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} sh_elf64_ehdr_t;

static int sh_builtin_elf(int argc, char **argv) {
    if (argc != 2) { sh_err("usage: elf PATH\n"); return 1; }
    sh_elf64_ehdr_t h;
    au_memset(&h, 0, sizeof(h));
    au_i64 fd = au_open(argv[1]);
    if (fd < 0) { sh_err("elf: open failed\n"); return 1; }
    au_i64 got = au_read(fd, &h, sizeof(h));
    (void)au_close(fd);
    if (got != (au_i64)sizeof(h) || h.ident[0] != 0x7fu || h.ident[1] != 'E' || h.ident[2] != 'L' || h.ident[3] != 'F') {
        sh_err("elf: invalid ELF64 header\n"); return 1;
    }
    sh_puts("elf: entry="); sh_print_hex64(h.entry);
    sh_puts(" phoff="); sh_print_u64(h.phoff);
    sh_puts(" phnum="); sh_print_u64(h.phnum);
    sh_puts(" phentsize="); sh_print_u64(h.phentsize);
    sh_puts(" shoff="); sh_print_u64(h.shoff);
    sh_puts(" shnum="); sh_print_u64(h.shnum);
    sh_puts(" machine="); sh_print_u64(h.machine);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_userbins(void) {
    const char *path = sh_path_value();
    unsigned int shown = 0;
    int rc = 0;
    while (*path) {
        const char *start = path;
        while (*path && *path != ':') ++path;
        au_usize len = (au_usize)(path - start);
        if (len > 0u) {
            char dir[SH_PATH_MAX];
            if (sh_copy_n(dir, sizeof(dir), start, len)) {
                au_i64 fd = au_open2(dir, AURORA_O_DIRECTORY);
                if (fd >= 0) {
                    for (au_u64 i = 0; ; ++i) {
                        au_dirent_t de;
                        au_memset(&de, 0, sizeof(de));
                        au_i64 got = au_readdir(fd, i, &de);
                        if (got < 0) { rc = 1; break; }
                        if (got == 0) break;
                        char full[SH_PATH_MAX];
                        au_stat_t st;
                        au_memset(&st, 0, sizeof(st));
                        if (!sh_ls_join_path(dir, de.name, full, sizeof(full))) continue;
                        if (au_stat(full, &st) != 0 || st.type != SH_NODE_FILE) continue;
                        if (!sh_path_is_elf(full)) continue;
                        sh_puts("ok   "); sh_puts(full); sh_puts(" size="); sh_print_u64(st.size); sh_puts("\n");
                        ++shown;
                    }
                    if (au_close(fd) != 0) rc = 1;
                }
            }
        }
        if (*path == ':') ++path;
    }
    if (!shown) { sh_err("userbins: no executable ELF files found in PATH\n"); return 1; }
    return rc;
}

static int sh_builtin_count(int argc, char **argv) {
    au_u64 n = 10;
    au_u64 delay = 10;
    if (argc > 1 && !sh_parse_u64(argv[1], &n)) { sh_err("usage: count [N] [DELAY_TICKS]\n"); return 1; }
    if (argc > 2 && !sh_parse_u64(argv[2], &delay)) { sh_err("usage: count [N] [DELAY_TICKS]\n"); return 1; }
    au_i64 pid = au_getpid();
    for (au_u64 i = 0; i < n; ++i) {
        sh_puts("count pid="); sh_print_i64(pid);
        sh_puts(" i="); sh_print_u64(i + 1u);
        sh_puts("/"); sh_print_u64(n);
        sh_puts(" ticks="); sh_print_i64(au_ticks());
        sh_puts("\n");
        if (delay) (void)au_sleep(delay);
        else (void)au_yield();
    }
    return 0;
}


