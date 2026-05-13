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
    if (!dst || !cap || !src) return 0;
    au_usize n = au_strlen(src);
    if (n + 1u > cap) return 0;
    au_memcpy(dst, src, n + 1u);
    return 1;
}

static int sh_copy_n(char *dst, au_usize cap, const char *src, au_usize n) {
    if (!dst || !cap || !src || n + 1u > cap) return 0;
    au_memcpy(dst, src, n);
    dst[n] = 0;
    return 1;
}

static int sh_strneq(const char *a, const char *b, au_usize n) {
    if (!a || !b) return 0;
    for (au_usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
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
    if (!dst || !dir || !name || cap == 0 || dir_len == 0) return 0;
    if (dir_len == 1u && dir[0] == '/') {
        if (nn + 2u > cap) return 0;
        dst[0] = '/';
        au_memcpy(dst + 1u, name, nn + 1u);
        return 1;
    }
    au_usize need = dir_len + 1u + nn + 1u;
    if (need > cap) return 0;
    au_memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    au_memcpy(dst + dir_len + 1u, name, nn + 1u);
    return 1;
}

static int sh_parse_profile_path(const char *buf, au_usize n, char *out, au_usize cap) {
    if (!buf || !out || cap == 0) return 0;
    au_usize i = 0;
    while (i < n) {
        while (i < n && (buf[i] == '\n' || buf[i] == '\r')) ++i;
        au_usize line_start = i;
        while (i < n && buf[i] != '\n' && buf[i] != '\r') ++i;
        au_usize line_end = i;
        au_usize p = line_start;
        while (p < line_end && (buf[p] == ' ' || buf[p] == '\t')) ++p;
        if (line_end >= p + 5u && sh_strneq(buf + p, "PATH=", 5u)) {
            p += 5u;
            while (p < line_end && (buf[p] == ' ' || buf[p] == '\t')) ++p;
            char quote = 0;
            if (p < line_end && (buf[p] == '\'' || buf[p] == '"')) quote = buf[p++];
            au_usize value_start = p;
            au_usize value_end = line_end;
            if (quote) {
                value_end = value_start;
                while (value_end < line_end && buf[value_end] != quote) ++value_end;
            } else {
                while (value_end > value_start && (buf[value_end - 1u] == ' ' || buf[value_end - 1u] == '\t')) --value_end;
            }
            if (value_end > value_start) return sh_copy_n(out, cap, buf + value_start, value_end - value_start);
            return 0;
        }
    }
    return 0;
}

static void sh_load_path_env(void) {
    if (sh_path_env_loaded) return;
    sh_path_env_loaded = 1;
    sh_copy(sh_path_env, sizeof(sh_path_env), SH_DEFAULT_PATH);

    char buf[SH_PATH_PARSE_BUF];
    au_memset(buf, 0, sizeof(buf));
    au_i64 fd = au_open(SH_ENV_FILE);
    if (fd < 0) return;
    au_i64 got = au_read(fd, buf, sizeof(buf) - 1u);
    (void)au_close(fd);
    if (got <= 0) return;
    buf[(au_usize)got] = 0;
    char parsed[SH_PATH_MAX];
    au_memset(parsed, 0, sizeof(parsed));
    if (sh_parse_profile_path(buf, (au_usize)got, parsed, sizeof(parsed))) sh_copy(sh_path_env, sizeof(sh_path_env), parsed);
}

static const char *sh_path_value(void) {
    sh_load_path_env();
    return sh_path_env[0] ? sh_path_env : SH_DEFAULT_PATH;
}

static int sh_set_path_value(const char *value) {
    if (!value || !*value) return 0;
    if (!sh_copy(sh_path_env, sizeof(sh_path_env), value)) return 0;
    sh_path_env_loaded = 1;
    return 1;
}

static int sh_resolve_program(char *dst, au_usize cap, const char *name) {
    if (!name || !*name || !dst || cap == 0) return 0;
    if (sh_strchr((char *)name, '/')) {
        au_stat_t st;
        au_memset(&st, 0, sizeof(st));
        if (au_stat(name, &st) == 0 && st.type == SH_NODE_FILE) return sh_copy(dst, cap, name);
        return 0;
    }

    const char *path = sh_path_value();
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
    dst[0] = 0;
    return 0;
}
