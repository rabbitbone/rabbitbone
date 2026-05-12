#include <aurora_sys.h>

#define SH_LINE_MAX 256u
#define SH_TOKENS_MAX 32u
#define SH_STAGES_MAX 6u
#define SH_ARG_MAX 16u
#define SH_PATH_MAX AURORA_PATH_MAX
#define SH_PATH_ENV "/bin:/sbin"
#define SH_SCAN_PID_MAX 256u

#define SH_NODE_FILE 1u
#define SH_NODE_DIR 2u
#define SH_NODE_DEV 3u
#define SH_NODE_SYMLINK 4u

static unsigned int sh_last_async_pid = 0;

static void sh_write_fd(au_i64 fd, const char *s) {
    if (!s) return;
    (void)au_write(fd, s, au_strlen(s));
}

static void sh_puts(const char *s) { sh_write_fd((au_i64)AURORA_STDOUT, s); }
static void sh_err(const char *s) { sh_write_fd((au_i64)AURORA_STDERR, s); }

static int sh_streq(const char *a, const char *b) { return au_strcmp(a, b) == 0; }

static int sh_startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static char *sh_strchr(char *s, int c) {
    if (!s) return (char *)0;
    while (*s) {
        if (*s == (char)c) return s;
        ++s;
    }
    return c == 0 ? s : (char *)0;
}

static const char *sh_basename(const char *path) {
    const char *base = path;
    if (!path) return "";
    for (const char *p = path; *p; ++p) {
        if (*p == '/') base = p + 1;
    }
    return base;
}

static int sh_copy(char *dst, au_usize cap, const char *src) {
    au_usize n = au_strlen(src);
    if (!dst || !cap || !src || n + 1u > cap) return 0;
    au_memcpy(dst, src, n + 1u);
    return 1;
}


static void sh_u64(au_u64 v, char *out, au_usize cap) {
    char tmp[24];
    au_usize n = 0;
    if (!out || cap == 0) return;
    if (v == 0) {
        if (cap > 1u) { out[0] = '0'; out[1] = 0; }
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    au_usize i = 0;
    while (n && i + 1u < cap) out[i++] = tmp[--n];
    out[i] = 0;
}

static void sh_i64(au_i64 v, char *out, au_usize cap) {
    if (!out || cap == 0) return;
    if (v < 0) {
        out[0] = '-';
        au_u64 mag = (au_u64)(-(v + 1)) + 1u;
        sh_u64(mag, out + 1, cap > 1u ? cap - 1u : 0u);
    } else {
        sh_u64((au_u64)v, out, cap);
    }
}

static void sh_hex64(au_u64 v, char *out, au_usize cap) {
    static const char hexdigits[] = "0123456789abcdef";
    if (!out || cap < 3u) return;
    out[0] = '0';
    out[1] = 'x';
    unsigned int started = 0;
    au_usize o = 2u;
    for (int i = 60; i >= 0; i -= 4) {
        unsigned int d = (unsigned int)((v >> (unsigned int)i) & 0xfu);
        if (d || started || i == 0) {
            if (o + 1u >= cap) break;
            out[o++] = hexdigits[d];
            started = 1;
        }
    }
    out[o] = 0;
}

static void sh_print_i64(au_i64 v) {
    char buf[32];
    buf[0] = 0;
    sh_i64(v, buf, sizeof(buf));
    sh_puts(buf);
}

static void sh_print_u64(au_u64 v) {
    char buf[32];
    buf[0] = 0;
    sh_u64(v, buf, sizeof(buf));
    sh_puts(buf);
}

static void sh_print_hex64(au_u64 v) {
    char buf[32];
    buf[0] = 0;
    sh_hex64(v, buf, sizeof(buf));
    sh_puts(buf);
}

static int sh_parse_u64(const char *s, au_u64 *out) {
    au_u64 v = 0;
    if (!s || !*s || !out) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        au_u64 d = (au_u64)(*s - '0');
        if (v > (0xffffffffffffffffull - d) / 10ull) return 0;
        v = v * 10ull + d;
        ++s;
    }
    *out = v;
    return 1;
}

static int sh_parse_u32(const char *s, unsigned int *out) {
    au_u64 v = 0;
    if (!sh_parse_u64(s, &v) || v > 0xffffffffull) return 0;
    *out = (unsigned int)v;
    return 1;
}

static const char *sh_node_type(unsigned int type) {
    if (type == SH_NODE_DIR) return "dir";
    if (type == SH_NODE_DEV) return "dev";
    if (type == SH_NODE_SYMLINK) return "link";
    return "file";
}

static char sh_type_char(unsigned int type) {
    if (type == SH_NODE_DIR) return 'd';
    if (type == SH_NODE_SYMLINK) return 'l';
    if (type == SH_NODE_DEV) return 'c';
    return '-';
}

static void sh_mode_text(unsigned int mode, unsigned int type, char out[11]) {
    static const unsigned int bits[9] = { 0400u, 0200u, 0100u, 0040u, 0020u, 0010u, 0004u, 0002u, 0001u };
    static const char chars[9] = { 'r', 'w', 'x', 'r', 'w', 'x', 'r', 'w', 'x' };
    if (!out) return;
    out[0] = sh_type_char(type);
    for (unsigned int i = 0; i < 9u; ++i) out[i + 1u] = (mode & bits[i]) ? chars[i] : '-';
    out[10] = 0;
}

static void sh_print_mode(unsigned int mode, unsigned int type) {
    char buf[11];
    sh_mode_text(mode, type, buf);
    sh_puts(buf);
}

static int sh_parse_octal_mode(const char *s, unsigned int *out) {
    unsigned int v = 0;
    if (!s || !*s || !out) return 0;
    while (*s) {
        if (*s < '0' || *s > '7') return 0;
        v = (v << 3u) | (unsigned int)(*s - '0');
        if (v > 07777u) return 0;
        ++s;
    }
    *out = v;
    return 1;
}

static int sh_join_path(char *dst, au_usize cap, const char *dir, au_usize dir_len, const char *name) {
    au_usize nn = au_strlen(name);
    au_usize need = dir_len + 1u + nn + 1u;
    if (!dst || !dir || !name || need > cap) return 0;
    au_memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    au_memcpy(dst + dir_len + 1u, name, nn + 1u);
    return 1;
}

static int sh_resolve_program(char *dst, au_usize cap, const char *name) {
    const char *path = SH_PATH_ENV;
    if (!name || !*name || !dst || cap == 0) return 0;
    if (sh_strchr((char *)name, '/')) return sh_copy(dst, cap, name);

    while (*path) {
        const char *start = path;
        while (*path && *path != ':') ++path;
        au_usize len = (au_usize)(path - start);
        if (len > 0u && sh_join_path(dst, cap, start, len, name)) {
            au_stat_t st;
            au_memset(&st, 0, sizeof(st));
            if (au_stat(dst, &st) == 0 && st.type == SH_NODE_FILE) return 1;
        }
        if (*path == ':') ++path;
    }

    return sh_join_path(dst, cap, "/bin", 4u, name);
}

typedef struct sh_stage {
    char *argv[SH_ARG_MAX];
    unsigned int argc;
    char path[SH_PATH_MAX];
    char in_path[SH_PATH_MAX];
    char out_path[SH_PATH_MAX];
    char err_path[SH_PATH_MAX];
} sh_stage_t;

static int sh_split_pipeline(char *line, char **parts, unsigned int *count) {
    unsigned int n = 0;
    char quote = 0;
    char *start = line;
    if (!line || !parts || !count) return 0;
    for (char *p = line; ; ++p) {
        char c = *p;
        if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) {
            quote = quote ? 0 : c;
        }
        if ((c == '|' && quote == 0) || c == 0) {
            if (n >= SH_STAGES_MAX) return 0;
            parts[n++] = start;
            if (c == 0) break;
            *p = 0;
            start = p + 1;
        }
    }
    *count = n;
    return quote == 0;
}

static int sh_tokenize(char *text, char **tokens, unsigned int *count) {
    char *r = text;
    char *w = text;
    unsigned int n = 0;
    if (!text || !tokens || !count) return 0;

    while (*r) {
        while (*r == ' ' || *r == '\t') ++r;
        if (!*r) break;
        if (n >= SH_TOKENS_MAX) return 0;

        tokens[n++] = w;
        char quote = 0;
        while (*r) {
            char c = *r;
            if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) {
                quote = quote ? 0 : c;
                ++r;
                continue;
            }
            if (!quote && (c == ' ' || c == '\t')) {
                ++r;
                break;
            }
            *w++ = *r++;
        }
        if (quote) return 0;
        *w++ = 0;
    }

    *count = n;
    return 1;
}

static int sh_parse_stage(char *text, sh_stage_t *stage) {
    char *tokens[SH_TOKENS_MAX];
    unsigned int ntok = 0;
    if (!text || !stage) return 0;
    au_memset(stage, 0, sizeof(*stage));
    if (!sh_tokenize(text, tokens, &ntok)) return 0;
    if (ntok == 0) return 0;
    for (unsigned int i = 0; i < ntok; ++i) {
        char *t = tokens[i];
        if (sh_streq(t, "<") || sh_streq(t, ">") || sh_streq(t, "2>")) {
            if (i + 1u >= ntok) return 0;
            char *dst = sh_streq(t, "<") ? stage->in_path : (sh_streq(t, ">") ? stage->out_path : stage->err_path);
            if (!sh_copy(dst, SH_PATH_MAX, tokens[++i])) return 0;
            continue;
        }
        if (t[0] == '>' && t[1]) {
            if (!sh_copy(stage->out_path, SH_PATH_MAX, t + 1)) return 0;
            continue;
        }
        if (t[0] == '<' && t[1]) {
            if (!sh_copy(stage->in_path, SH_PATH_MAX, t + 1)) return 0;
            continue;
        }
        if (t[0] == '2' && t[1] == '>' && t[2]) {
            if (!sh_copy(stage->err_path, SH_PATH_MAX, t + 2)) return 0;
            continue;
        }
        if (stage->argc >= SH_ARG_MAX) return 0;
        stage->argv[stage->argc++] = t;
    }
    if (stage->argc == 0) return 0;
    if (!sh_resolve_program(stage->path, sizeof(stage->path), stage->argv[0])) return 0;
    return 1;
}

static int sh_open_redirs(const sh_stage_t *stage) {
    if (!stage) return 1;
    if (stage->in_path[0]) {
        au_i64 fd = au_open2(stage->in_path, AURORA_O_RDONLY);
        if (fd < 0) { sh_err("sh: cannot open input: "); sh_write_fd((au_i64)AURORA_STDERR, stage->in_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDIN, 0) != (au_i64)AURORA_STDIN) { sh_err("sh: dup2 input failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    if (stage->out_path[0]) {
        au_i64 fd = au_open2(stage->out_path, AURORA_O_WRONLY | AURORA_O_CREAT | AURORA_O_TRUNC);
        if (fd < 0) { sh_err("sh: cannot open output: "); sh_write_fd((au_i64)AURORA_STDERR, stage->out_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDOUT, 0) != (au_i64)AURORA_STDOUT) { sh_err("sh: dup2 output failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    if (stage->err_path[0]) {
        au_i64 fd = au_open2(stage->err_path, AURORA_O_WRONLY | AURORA_O_CREAT | AURORA_O_TRUNC);
        if (fd < 0) { sh_err("sh: cannot open stderr: "); sh_write_fd((au_i64)AURORA_STDERR, stage->err_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDERR, 0) != (au_i64)AURORA_STDERR) { sh_err("sh: dup2 stderr failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    return 1;
}

static void sh_print_procinfo(const au_procinfo_t *info) {
    if (!info) return;
    sh_puts("pid="); sh_print_u64(info->pid);
    sh_puts(" state="); sh_print_u64(info->state);
    sh_puts(" exit="); sh_print_i64(info->exit_code);
    sh_puts(" status="); sh_print_i64(info->status);
    sh_puts(" uid="); sh_print_u64(info->uid);
    sh_puts(" euid="); sh_print_u64(info->euid);
    sh_puts(" pages="); sh_print_u64(info->mapped_pages);
    sh_puts(" asid="); sh_print_u64(info->address_space_generation);
    sh_puts(" name="); sh_puts(info->name);
    if (info->faulted) {
        sh_puts(" faulted=1 vector="); sh_print_u64(info->fault_vector);
        sh_puts(" rip="); sh_print_hex64(info->fault_rip);
        sh_puts(" addr="); sh_print_hex64(info->fault_addr);
    }
    sh_puts("\n");
}

static void sh_help(void) {
    sh_puts("Aurora userland shell (/bin/sh). prompt: <cwd>$\n");
    sh_puts("PATH search: bare command -> /bin/name, then /sbin/name. Use 'path', 'which NAME', 'type NAME'.\n");
    sh_puts("builtins:\n");
    sh_puts("  help clear uname path which type theme exit crash echo pid pwd cd id whoami users login su sudo\n");
    sh_puts("  ls [-l] [PATH] stat chmod chown cat write touch mkdir rm mv truncate link ln symlink readlink\n");
    sh_puts("  run spawn qspawn wait runq procs ps proc lastproc sched schedtest preempt ticks sleep yield count\n");
    sh_puts("  fdprobe fdinfo tty statvfs mounts sync fsync syscall elf userbins log\n");
    sh_puts("  mem heap vmm ktest logs disks ext4 panic reboot halt\n");
    sh_puts("syntax: cmd [args] | cmd, redirects: < file, > file, 2> file, quotes: '...' or \"...\"\n");
    sh_puts("themes: theme, theme show, theme legacy, theme black\nusers: login USER, su [USER], sudo [-v|-k|-K|-l|-s|-i|-n|-T TICKS|-u USER] [CMD...]\n");
    sh_puts("external /bin: hello fscheck writetest regtrash badptr badpath statcheck procstat spawncheck schedcheck preemptcheck fdcheck isolate fdleak forkcheck procctl execcheck execfdcheck execfdchild execvecheck exectarget pipecheck fdremapcheck pollcheck stdcat termcheck aursh sh\n");
    sh_puts("kernel ABI: info commands are direct syscalls; panic/reboot/halt require sudo/root.\n");
}

static int sh_builtin_clear(void) {
    sh_puts("\x1b[2J\x1b[H");
    return 0;
}

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


static const char *sh_theme_name(unsigned int id) {
    if (id == AURORA_THEME_LEGACY) return "legacy";
    if (id == AURORA_THEME_BLACK) return "black";
    return "unknown";
}

static int sh_theme_id(const char *name, unsigned int *out) {
    if (!name || !out) return 0;
    if (sh_streq(name, "legacy")) { *out = AURORA_THEME_LEGACY; return 1; }
    if (sh_streq(name, "black")) { *out = AURORA_THEME_BLACK; return 1; }
    return 0;
}

static int sh_builtin_theme(int argc, char **argv) {
    if (argc == 1 || (argc == 2 && sh_streq(argv[1], "show"))) {
        au_i64 id = au_theme(AURORA_THEME_OP_GET, 0);
        if (id < 0) { sh_err("theme: syscall failed\n"); return 1; }
        sh_puts("theme="); sh_puts(sh_theme_name((unsigned int)id)); sh_puts("\n");
        sh_puts("available: legacy black\n");
        return 0;
    }
    if (argc == 2) {
        unsigned int id = 0;
        if (!sh_theme_id(argv[1], &id)) { sh_err("usage: theme [show|legacy|black]\n"); return 1; }
        au_i64 r = au_theme(AURORA_THEME_OP_SET, id);
        if (r < 0) { sh_err("theme: set failed\n"); return 1; }
        sh_puts("\x1b[2J\x1b[H");
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

static int sh_builtin_run(int argc, char **argv) {
    if (argc < 2) { sh_err("usage: run PATH [ARGS]\n"); return 1; }
    char path[SH_PATH_MAX];
    char *run_argv[SH_ARG_MAX];
    unsigned int run_argc = (unsigned int)(argc - 1);
    if (run_argc > SH_ARG_MAX) return 1;
    if (!sh_resolve_program(path, sizeof(path), argv[1])) return 1;
    if (!sh_path_is_elf(path)) { sh_err("run: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, path); sh_err("\n"); return 126; }
    run_argv[0] = path;
    for (unsigned int i = 1; i < run_argc; ++i) run_argv[i] = argv[i + 1u];
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 r = au_spawnv_wait(path, run_argc, (const char *const *)run_argv, &info);
    if (r < 0) {
        sh_err("run: spawn failed: ");
        sh_write_fd((au_i64)AURORA_STDERR, path);
        sh_err("\n");
        return 127;
    }
    sh_print_procinfo(&info);
    return info.exit_code;
}

static int sh_builtin_spawn(int argc, char **argv) {
    if (argc < 2) { sh_err("usage: spawn PATH [ARGS]\n"); return 1; }
    unsigned int run_argc = (unsigned int)(argc - 1);
    if (run_argc > SH_ARG_MAX) return 1;
    if (sh_is_builtin(argv[1])) {
        au_i64 child = au_fork();
        if (child < 0) { sh_err("spawn: fork failed\n"); return 127; }
        if (child == 0) {
            int code = sh_run_builtin(argc - 1, argv + 1, 0);
            au_exit(code);
        }
        sh_last_async_pid = (unsigned int)child;
        sh_puts("spawned builtin pid="); sh_print_i64(child); sh_puts("\n");
        return 0;
    }
    char path[SH_PATH_MAX];
    char *run_argv[SH_ARG_MAX];
    if (!sh_resolve_program(path, sizeof(path), argv[1])) return 1;
    if (!sh_path_is_elf(path)) { sh_err("spawn: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, path); sh_err("\n"); return 126; }
    run_argv[0] = path;
    for (unsigned int i = 1; i < run_argc; ++i) run_argv[i] = argv[i + 1u];
    au_i64 pid = au_spawnv(path, run_argc, (const char *const *)run_argv);
    if (pid < 0) { sh_err("spawn: failed: "); sh_write_fd((au_i64)AURORA_STDERR, path); sh_err("\n"); return 127; }
    sh_last_async_pid = (unsigned int)pid;
    sh_puts("spawned pid="); sh_print_i64(pid); sh_puts("\n");
    return 0;
}

static int sh_builtin_wait(int argc, char **argv) {
    unsigned int pid = 0;
    if (argc == 1 && sh_last_async_pid != 0) pid = sh_last_async_pid;
    else if (argc == 2 && sh_parse_u32(argv[1], &pid)) { }
    else { sh_err("usage: wait [PID]\n"); return 1; }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 r = au_wait(pid, &info);
    if (r < 0) { sh_err("wait: no completed child\n"); return 1; }
    sh_print_procinfo(&info);
    return info.exit_code;
}

static int sh_builtin_proc(int argc, char **argv) {
    unsigned int pid = 0;
    if (argc != 2 || !sh_parse_u32(argv[1], &pid)) { sh_err("usage: proc PID\n"); return 1; }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_procinfo(pid, &info) != 0) { sh_err("proc: not found\n"); return 1; }
    sh_print_procinfo(&info);
    return 0;
}

static int sh_builtin_procs(void) {
    unsigned int shown = 0;
    for (unsigned int pid = 1; pid <= SH_SCAN_PID_MAX; ++pid) {
        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        if (au_procinfo(pid, &info) == 0 && info.pid != 0) {
            sh_print_procinfo(&info);
            ++shown;
        }
    }
    if (!shown) sh_puts("procs: no visible processes\n");
    return 0;
}

static int sh_builtin_sched(void) {
    au_schedinfo_t s;
    au_memset(&s, 0, sizeof(s));
    if (au_schedinfo(&s) != 0) { sh_err("sched: unavailable\n"); return 1; }
    sh_puts("sched: cap="); sh_print_u64(s.queue_capacity);
    sh_puts(" queued="); sh_print_u64(s.queued);
    sh_puts(" running="); sh_print_u64(s.running);
    sh_puts(" completed="); sh_print_u64(s.completed);
    sh_puts(" failed="); sh_print_u64(s.failed);
    sh_puts(" dispatched="); sh_print_u64(s.total_dispatched);
    sh_puts(" yields="); sh_print_u64(s.total_yields);
    sh_puts(" sleeps="); sh_print_u64(s.total_sleeps);
    sh_puts(" preempt="); sh_print_u64(s.preempt_enabled);
    sh_puts(" quantum="); sh_print_u64(s.quantum_ticks);
    sh_puts(" timer="); sh_print_u64(s.total_timer_ticks);
    sh_puts("\n");
    return 0;
}

static int sh_builtin_preempt(void) {
    au_preemptinfo_t p;
    au_memset(&p, 0, sizeof(p));
    if (au_preemptinfo(&p) != 0) { sh_err("preempt: unavailable\n"); return 1; }
    sh_puts("preempt: enabled="); sh_print_u64(p.enabled);
    sh_puts(" quantum="); sh_print_u64(p.quantum_ticks);
    sh_puts(" current_pid="); sh_print_u64(p.current_pid);
    sh_puts(" slice="); sh_print_u64(p.current_slice_ticks);
    sh_puts(" timer="); sh_print_u64(p.total_timer_ticks);
    sh_puts(" user="); sh_print_u64(p.user_ticks);
    sh_puts(" kernel="); sh_print_u64(p.kernel_ticks);
    sh_puts(" expirations="); sh_print_u64(p.total_preemptions);
    sh_puts(" last_tick="); sh_print_u64(p.last_preempt_ticks);
    sh_puts(" rip="); sh_print_hex64(p.last_preempt_rip);
    sh_puts("\n");
    return 0;
}

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


static int sh_builtin_mounts(void) {
    const char *paths[] = { "/", "/bin", "/sbin", "/dev", "/etc" };
    unsigned int printed = 0;
    for (unsigned int i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        au_statvfs_t st;
        au_memset(&st, 0, sizeof(st));
        if (au_statvfs(paths[i], &st) != 0) continue;
        int duplicate = 0;
        for (unsigned int j = 0; j < i; ++j) {
            au_statvfs_t prev;
            au_memset(&prev, 0, sizeof(prev));
            if (au_statvfs(paths[j], &prev) == 0 && prev.fs_id == st.fs_id && au_strcmp(prev.mount_path, st.mount_path) == 0) duplicate = 1;
        }
        if (duplicate) continue;
        sh_puts(st.mount_path);
        sh_puts(" type="); sh_puts(st.fs_name);
        sh_puts(" fsid="); sh_print_u64(st.fs_id);
        sh_puts(" block="); sh_print_u64(st.block_size);
        sh_puts(" total="); sh_print_u64(st.total_blocks);
        sh_puts(" free="); sh_print_u64(st.free_blocks);
        sh_puts(" flags="); sh_print_u64(st.flags);
        sh_puts("\n");
        ++printed;
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
    const char *bins[] = {
        "/bin/hello", "/bin/fscheck", "/bin/writetest", "/bin/regtrash", "/bin/badptr", "/bin/badpath",
        "/bin/statcheck", "/bin/procstat", "/bin/spawncheck", "/bin/schedcheck", "/bin/preemptcheck",
        "/bin/fdcheck", "/bin/isolate", "/bin/fdleak", "/bin/forkcheck", "/bin/procctl", "/bin/execcheck",
        "/bin/execfdcheck", "/bin/execfdchild", "/bin/execvecheck", "/bin/exectarget", "/bin/pipecheck",
        "/bin/fdremapcheck", "/bin/pollcheck", "/bin/stdcat", "/bin/termcheck", "/bin/sh", "/bin/aursh", "/sbin/init"
    };
    int rc = 0;
    for (unsigned int i = 0; i < sizeof(bins) / sizeof(bins[0]); ++i) {
        au_stat_t st;
        au_memset(&st, 0, sizeof(st));
        if (au_stat(bins[i], &st) == 0) {
            sh_puts("ok   "); sh_puts(bins[i]); sh_puts(" size="); sh_print_u64(st.size); sh_puts("\n");
        } else {
            sh_puts("miss "); sh_puts(bins[i]); sh_puts("\n"); rc = 1;
        }
    }
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

static int sh_path_is_elf(const char *path) {
    unsigned char head[4];
    au_i64 fd = au_open(path);
    if (fd < 0) return 0;
    au_i64 got = au_read(fd, head, sizeof(head));
    (void)au_close(fd);
    return got == 4 && head[0] == 0x7fu && head[1] == 'E' && head[2] == 'L' && head[3] == 'F';
}

static int sh_kctl_op_for(const char *cmd, unsigned int *op) {
    if (!cmd || !op) return 0;
    if (sh_streq(cmd, "mem")) *op = AURORA_KCTL_OP_MEM;
    else if (sh_streq(cmd, "heap")) *op = AURORA_KCTL_OP_HEAP;
    else if (sh_streq(cmd, "vmm")) *op = AURORA_KCTL_OP_VMM;
    else if (sh_streq(cmd, "ktest")) *op = AURORA_KCTL_OP_KTEST;
    else if (sh_streq(cmd, "logs")) *op = AURORA_KCTL_OP_LOGS;
    else if (sh_streq(cmd, "disks")) *op = AURORA_KCTL_OP_DISKS;
    else if (sh_streq(cmd, "ext4")) *op = AURORA_KCTL_OP_EXT4;
    else if (sh_streq(cmd, "panic")) *op = AURORA_KCTL_OP_PANIC;
    else if (sh_streq(cmd, "reboot")) *op = AURORA_KCTL_OP_REBOOT;
    else if (sh_streq(cmd, "halt")) *op = AURORA_KCTL_OP_HALT;
    else return 0;
    return 1;
}

static int sh_builtin_kctl(int argc, char **argv) {
    if (argc <= 0 || !argv || !argv[0]) return 1;
    unsigned int op = 0;
    if (!sh_kctl_op_for(argv[0], &op)) return 127;
    const char *arg = 0;
    if (op == AURORA_KCTL_OP_PANIC) arg = argc > 1 ? argv[1] : "shell panic command";
    if ((op == AURORA_KCTL_OP_REBOOT || op == AURORA_KCTL_OP_HALT) && argc > 1) {
        sh_err(argv[0]);
        sh_err(": no arguments expected\n");
        return 2;
    }
    char out[AURORA_KCTL_OUT_MAX];
    au_memset(out, 0, sizeof(out));
    au_i64 r = au_kctl(op, out, sizeof(out), arg);
    if (r < 0) {
        sh_err(argv[0]);
        if (r == -8) sh_err(": permission denied; use sudo\n");
        else if (r == -11) sh_err(": unsupported by kernel\n");
        else sh_err(": syscall failed\n");
        return r == -8 ? 126 : 1;
    }
    if (out[0]) sh_puts(out);
    return 0;
}

static int sh_is_builtin(const char *name) {
    return sh_streq(name, "help") || sh_streq(name, "clear") || sh_streq(name, "uname") || sh_streq(name, "path") ||
           sh_streq(name, "which") || sh_streq(name, "type") || sh_streq(name, "theme") || sh_streq(name, "exit") || sh_streq(name, "crash") ||
           sh_streq(name, "echo") || sh_streq(name, "pid") || sh_streq(name, "pwd") || sh_streq(name, "cd") ||
           sh_streq(name, "id") || sh_streq(name, "whoami") || sh_streq(name, "users") || sh_streq(name, "login") || sh_streq(name, "su") || sh_streq(name, "sudo") ||
           sh_streq(name, "ls") || sh_streq(name, "stat") || sh_streq(name, "chmod") || sh_streq(name, "chown") || sh_streq(name, "cat") || sh_streq(name, "write") ||
           sh_streq(name, "touch") || sh_streq(name, "mkdir") || sh_streq(name, "rm") || sh_streq(name, "mv") ||
           sh_streq(name, "truncate") || sh_streq(name, "link") || sh_streq(name, "ln") || sh_streq(name, "symlink") ||
           sh_streq(name, "readlink") || sh_streq(name, "run") || sh_streq(name, "spawn") || sh_streq(name, "wait") ||
           sh_streq(name, "procs") || sh_streq(name, "ps") || sh_streq(name, "proc") || sh_streq(name, "lastproc") ||
           sh_streq(name, "sched") || sh_streq(name, "preempt") || sh_streq(name, "ticks") || sh_streq(name, "sleep") ||
           sh_streq(name, "yield") || sh_streq(name, "fdprobe") || sh_streq(name, "fdinfo") || sh_streq(name, "tty") ||
           sh_streq(name, "statvfs") || sh_streq(name, "sync") || sh_streq(name, "fsync") || sh_streq(name, "syscall") ||
           sh_streq(name, "elf") || sh_streq(name, "userbins") || sh_streq(name, "log") || sh_streq(name, "mem") ||
           sh_streq(name, "heap") || sh_streq(name, "vmm") || sh_streq(name, "ktest") || sh_streq(name, "logs") ||
           sh_streq(name, "disks") || sh_streq(name, "mounts") || sh_streq(name, "ext4") || sh_streq(name, "qspawn") ||
           sh_streq(name, "count") ||
           sh_streq(name, "runq") || sh_streq(name, "schedtest") || sh_streq(name, "panic") || sh_streq(name, "reboot") ||
           sh_streq(name, "halt");
}

static int sh_run_builtin(int argc, char **argv, int allow_exit) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    if (sh_streq(argv[0], "help")) { sh_help(); return 0; }
    if (sh_streq(argv[0], "clear")) return sh_builtin_clear();
    if (sh_streq(argv[0], "uname")) { sh_puts(AURORA_UNAME_TEXT); sh_puts("\n"); return 0; }
    if (sh_streq(argv[0], "path")) { sh_puts("PATH="); sh_puts(SH_PATH_ENV); sh_puts("\n"); return 0; }
    if (sh_streq(argv[0], "theme")) return sh_builtin_theme(argc, argv);
    if (sh_streq(argv[0], "which")) return sh_builtin_which(argc, argv);
    if (sh_streq(argv[0], "type")) {
        if (argc != 2) { sh_err("usage: type NAME\n"); return 1; }
        if (sh_is_builtin(argv[1])) { sh_puts(argv[1]); sh_puts(" is a shell builtin\n"); return 0; }
        return sh_builtin_which(argc, argv);
    }
    if (sh_streq(argv[0], "exit")) {
        unsigned int code = 0;
        if (argc > 1 && !sh_parse_u32(argv[1], &code)) code = 1;
        if (allow_exit) au_exit((int)code);
        return (int)code;
    }
    if (sh_streq(argv[0], "crash")) {
        volatile unsigned long long *p = (volatile unsigned long long *)0;
        *p = 0x4155524f5241ull;
        return 255;
    }
    if (sh_streq(argv[0], "echo")) return sh_builtin_echo(argc, argv);
    if (sh_streq(argv[0], "pid")) { au_i64 pid = au_getpid(); sh_print_i64(pid); sh_puts("\n"); return pid < 0 ? 1 : 0; }
    if (sh_streq(argv[0], "pwd")) { char cwd[SH_PATH_MAX]; if (au_getcwd(cwd, sizeof(cwd)) < 0) return 1; sh_puts(cwd); sh_puts("\n"); return 0; }
    if (sh_streq(argv[0], "cd")) return sh_builtin_cd(argc, argv);
    if (sh_streq(argv[0], "id")) return sh_builtin_id();
    if (sh_streq(argv[0], "whoami")) return sh_builtin_whoami();
    if (sh_streq(argv[0], "users")) return sh_builtin_users();
    if (sh_streq(argv[0], "login")) return sh_builtin_login(argc, argv);
    if (sh_streq(argv[0], "su")) return sh_builtin_su(argc, argv);
    if (sh_streq(argv[0], "sudo")) return sh_builtin_sudo(argc, argv);
    if (sh_streq(argv[0], "ls")) return sh_builtin_ls(argc, argv);
    if (sh_streq(argv[0], "stat")) return sh_builtin_stat(argc, argv);
    if (sh_streq(argv[0], "chmod")) return sh_builtin_chmod(argc, argv);
    if (sh_streq(argv[0], "chown")) return sh_builtin_chown(argc, argv);
    if (sh_streq(argv[0], "cat")) return sh_builtin_cat(argc, argv);
    if (sh_streq(argv[0], "write")) return sh_builtin_write_text(argc, argv, 0);
    if (sh_streq(argv[0], "touch")) return sh_builtin_write_text(argc, argv, 1);
    if (sh_streq(argv[0], "mkdir")) return sh_builtin_mkdir(argc, argv);
    if (sh_streq(argv[0], "rm")) return sh_builtin_rm(argc, argv);
    if (sh_streq(argv[0], "mv")) return sh_builtin_mv(argc, argv);
    if (sh_streq(argv[0], "truncate")) return sh_builtin_truncate(argc, argv);
    if (sh_streq(argv[0], "link") || sh_streq(argv[0], "ln")) return sh_builtin_link(argc, argv);
    if (sh_streq(argv[0], "symlink")) return sh_builtin_symlink(argc, argv);
    if (sh_streq(argv[0], "readlink")) return sh_builtin_readlink(argc, argv);
    if (sh_streq(argv[0], "run")) return sh_builtin_run(argc, argv);
    if (sh_streq(argv[0], "spawn") || sh_streq(argv[0], "qspawn")) return sh_builtin_spawn(argc, argv);
    if (sh_streq(argv[0], "wait")) return sh_builtin_wait(argc, argv);
    if (sh_streq(argv[0], "procs") || sh_streq(argv[0], "ps")) return sh_builtin_procs();
    if (sh_streq(argv[0], "proc")) return sh_builtin_proc(argc, argv);
    if (sh_streq(argv[0], "lastproc")) { if (!sh_last_async_pid) { sh_puts("lastproc: none\n"); return 1; } char pidbuf[16]; char *av[2]; sh_u64(sh_last_async_pid, pidbuf, sizeof(pidbuf)); av[0] = "proc"; av[1] = pidbuf; return sh_builtin_proc(2, av); }
    if (sh_streq(argv[0], "sched")) return sh_builtin_sched();
    if (sh_streq(argv[0], "preempt")) return sh_builtin_preempt();
    if (sh_streq(argv[0], "ticks")) { au_i64 t = au_ticks(); sh_puts("ticks="); sh_print_i64(t); sh_puts("\n"); return t < 0 ? 1 : 0; }
    if (sh_streq(argv[0], "sleep")) { au_u64 t = 1; if (argc > 1 && !sh_parse_u64(argv[1], &t)) { sh_err("usage: sleep [TICKS]\n"); return 1; } return au_sleep(t) == 0 ? 0 : 1; }
    if (sh_streq(argv[0], "yield")) return au_yield() == 0 ? 0 : 1;
    if (sh_streq(argv[0], "count")) return sh_builtin_count(argc, argv);
    if (sh_streq(argv[0], "fdprobe")) return sh_builtin_fdprobe(argc, argv);
    if (sh_streq(argv[0], "fdinfo")) return sh_builtin_fdinfo(argc, argv);
    if (sh_streq(argv[0], "tty")) return sh_builtin_tty();
    if (sh_streq(argv[0], "statvfs")) return sh_builtin_statvfs(argc, argv);
    if (sh_streq(argv[0], "mounts")) return sh_builtin_mounts();
    if (sh_streq(argv[0], "sync")) return au_sync() == 0 ? 0 : 1;
    if (sh_streq(argv[0], "fsync")) return sh_builtin_fsync(argc, argv);
    if (sh_streq(argv[0], "syscall")) return sh_builtin_syscall(argc, argv);
    if (sh_streq(argv[0], "elf")) return sh_builtin_elf(argc, argv);
    if (sh_streq(argv[0], "userbins")) return sh_builtin_userbins();
    if (sh_streq(argv[0], "log")) { if (argc < 2) { sh_err("usage: log TEXT\n"); return 1; } return au_log(argv[1]) == 0 ? 0 : 1; }
    if (sh_streq(argv[0], "mem") || sh_streq(argv[0], "heap") || sh_streq(argv[0], "vmm") || sh_streq(argv[0], "ktest") ||
        sh_streq(argv[0], "logs") || sh_streq(argv[0], "disks") || sh_streq(argv[0], "ext4") || sh_streq(argv[0], "panic") ||
        sh_streq(argv[0], "reboot") || sh_streq(argv[0], "halt")) return sh_builtin_kctl(argc, argv);
    if (sh_streq(argv[0], "runq")) { (void)au_yield(); sh_puts("runq: scheduler is timer/async driven in userland; yielded once\n"); return 0; }
    if (sh_streq(argv[0], "schedtest")) { char *av[2]; av[0] = "run"; av[1] = "schedcheck"; return sh_builtin_run(2, av); }
    sh_err(argv[0]);
    sh_err(": unknown builtin\n");
    return 127;
}

static int sh_command_from_argv(int argc, char **argv) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    if (sh_is_builtin(argv[0])) return sh_run_builtin(argc, argv, 0);
    sh_stage_t stage;
    au_memset(&stage, 0, sizeof(stage));
    if ((unsigned int)argc > SH_ARG_MAX) return 1;
    for (int i = 0; i < argc; ++i) stage.argv[i] = argv[i];
    stage.argc = (unsigned int)argc;
    if (!sh_resolve_program(stage.path, sizeof(stage.path), argv[0])) return 127;
    if (!sh_path_is_elf(stage.path)) { sh_err("sh: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, stage.path); sh_err("\n"); return 126; }
    return sh_spawn_external_wait(&stage);
}

static int sh_spawn_external_wait(sh_stage_t *stage) {
    if (!stage) return 1;
    if (!sh_path_is_elf(stage->path)) { sh_err("sh: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, stage->path); sh_err("\n"); return 126; }
    stage->argv[0] = stage->path;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 r = au_spawnv_wait(stage->path, stage->argc, (const char *const *)stage->argv, &info);
    if (r < 0) {
        sh_err("sh: command not found: ");
        sh_write_fd((au_i64)AURORA_STDERR, sh_basename(stage->path));
        sh_err("\n");
        return 127;
    }
    return info.exit_code;
}

static void sh_close_pipe_table(unsigned int pipes[SH_STAGES_MAX - 1u][2], unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) {
        if (pipes[i][0] > AURORA_STDERR) (void)au_close((au_i64)pipes[i][0]);
        if (pipes[i][1] > AURORA_STDERR) (void)au_close((au_i64)pipes[i][1]);
    }
}

static int sh_child_run_stage(sh_stage_t *stage) {
    if (!stage) return 127;
    if (!sh_open_redirs(stage)) return 126;
    if (sh_is_builtin(stage->argv[0])) return sh_run_builtin((int)stage->argc, stage->argv, 0);
    stage->argv[0] = stage->path;
    au_i64 r = au_execv(stage->path, stage->argc, (const char *const *)stage->argv);
    sh_err("sh: exec failed: ");
    sh_write_fd((au_i64)AURORA_STDERR, stage->path);
    sh_err("\n");
    return r < 0 ? 127 : 126;
}

static int sh_run_pipeline(char *line) {
    char *parts[SH_STAGES_MAX];
    unsigned int stages_n = 0;
    sh_stage_t stages[SH_STAGES_MAX];
    if (!sh_split_pipeline(line, parts, &stages_n)) { sh_err("sh: invalid pipeline\n"); return 2; }
    if (stages_n == 0) return 0;
    for (unsigned int i = 0; i < stages_n; ++i) {
        if (!sh_parse_stage(parts[i], &stages[i])) { sh_err("sh: parse error\n"); return 2; }
        if (i > 0 && stages[i].in_path[0]) { sh_err("sh: < conflicts with pipe\n"); return 2; }
        if (i + 1u < stages_n && stages[i].out_path[0]) { sh_err("sh: > conflicts with pipe\n"); return 2; }
    }

    if (stages_n == 1 && !stages[0].in_path[0] && !stages[0].out_path[0] && !stages[0].err_path[0]) {
        if (sh_is_builtin(stages[0].argv[0])) return sh_run_builtin((int)stages[0].argc, stages[0].argv, 1);
        return sh_spawn_external_wait(&stages[0]);
    }

    unsigned int pipes[SH_STAGES_MAX - 1u][2];
    au_memset(pipes, 0, sizeof(pipes));
    for (unsigned int i = 0; i + 1u < stages_n; ++i) {
        if (au_pipe(pipes[i]) != 0) {
            sh_close_pipe_table(pipes, i);
            sh_err("sh: pipe failed\n");
            return 1;
        }
    }

    unsigned int pids[SH_STAGES_MAX];
    au_memset(pids, 0, sizeof(pids));
    unsigned int spawned = 0;
    for (; spawned < stages_n; ++spawned) {
        au_i64 child = au_fork();
        if (child < 0) {
            sh_err("sh: fork failed\n");
            break;
        }
        if (child == 0) {
            int code;
            if (spawned > 0) {
                if (au_dup2((au_i64)pipes[spawned - 1u][0], (au_i64)AURORA_STDIN, 0) != (au_i64)AURORA_STDIN) au_exit(125);
            }
            if (spawned + 1u < stages_n) {
                if (au_dup2((au_i64)pipes[spawned][1], (au_i64)AURORA_STDOUT, 0) != (au_i64)AURORA_STDOUT) au_exit(125);
            }
            sh_close_pipe_table(pipes, stages_n - 1u);
            code = sh_child_run_stage(&stages[spawned]);
            au_exit(code);
        }
        pids[spawned] = (unsigned int)child;
    }
    sh_close_pipe_table(pipes, stages_n - 1u);

    int rc = 1;
    for (unsigned int i = 0; i < spawned; ++i) {
        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        if (au_wait(pids[i], &info) == 0) rc = info.exit_code;
        else rc = 1;
    }
    return rc;
}

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
