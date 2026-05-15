static void sh_write_fd(au_i64 fd, const char *s) {
    if (!s) return;
    au_usize n = au_strlen(s);
    au_usize off = 0;
    while (off < n) {
        au_i64 wrote = au_write(fd, s + off, n - off);
        if (wrote <= 0) return;
        off += (au_usize)wrote;
    }
}

static void sh_write_n_fd(au_i64 fd, const char *s, au_usize n) {
    if (!s || n == 0) return;
    au_usize off = 0;
    while (off < n) {
        au_i64 wrote = au_write(fd, s + off, n - off);
        if (wrote <= 0) return;
        off += (au_usize)wrote;
    }
}

static void sh_puts(const char *s) { sh_write_fd((au_i64)RABBITBONE_STDOUT, s); }
static void sh_err(const char *s) { sh_write_fd((au_i64)RABBITBONE_STDERR, s); }

static int sh_streq(const char *a, const char *b) { return a && b && au_strcmp(a, b) == 0; }

static void sh_bzero(void *p, au_usize n) {
    volatile unsigned char *d = (volatile unsigned char *)p;
    while (n--) *d++ = 0;
}

static int sh_is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
static int sh_is_digit(char c) { return c >= '0' && c <= '9'; }
static int sh_is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int sh_is_name_start(char c) { return sh_is_alpha(c) || c == '_'; }
static int sh_is_name_char(char c) { return sh_is_name_start(c) || sh_is_digit(c); }



static int sh_is_option_end(const char *s) { return sh_streq(s, "--"); }

static int sh_arg_is_help(const char *s) { return sh_streq(s, "-h") || sh_streq(s, "--help"); }

static int sh_arg_is_version(const char *s) { return sh_streq(s, "-V") || sh_streq(s, "--version"); }

static int sh_is_dot_path(const char *s) {
    return sh_streq(s, ".") || sh_streq(s, "..") || sh_streq(s, "/") || sh_streq(s, "//");
}


static const char *sh_errname(au_i64 code) {
    if (code == RABBITBONE_ERR_NOENT) return "no such file or directory";
    if (code == RABBITBONE_ERR_NOMEM) return "out of memory";
    if (code == RABBITBONE_ERR_INVAL) return "invalid argument";
    if (code == RABBITBONE_ERR_IO) return "I/O error";
    if (code == RABBITBONE_ERR_NOTDIR) return "not a directory";
    if (code == RABBITBONE_ERR_ISDIR) return "is a directory";
    if (code == RABBITBONE_ERR_EXIST) return "already exists";
    if (code == RABBITBONE_ERR_PERM) return "permission denied";
    if (code == RABBITBONE_ERR_NOSPC) return "no space left";
    if (code == RABBITBONE_ERR_NOTEMPTY) return "directory not empty";
    if (code == RABBITBONE_ERR_UNSUPPORTED) return "unsupported";
    if (code == RABBITBONE_ERR_BUSY) return "busy";
    return "failed";
}

static void sh_perror2(const char *cmd, const char *path, au_i64 code) {
    if (cmd && *cmd) { sh_err(cmd); sh_err(": "); }
    if (path && *path) { sh_write_fd((au_i64)RABBITBONE_STDERR, path); sh_err(": "); }
    sh_err(sh_errname(code));
    sh_err("\n");
}

static int sh_stat_path(const char *path, au_stat_t *st, int follow) {
    if (!path || !st) return RABBITBONE_ERR_INVAL;
    au_memset(st, 0, sizeof(*st));
    au_i64 r = follow ? au_stat(path, st) : au_lstat(path, st);
    return r == 0 ? 0 : (int)r;
}

static int sh_path_exists(const char *path, int follow) {
    au_stat_t st;
    return sh_stat_path(path, &st, follow) == 0;
}

static void sh_print_byte_escaped(unsigned char c) {
    char tmp[5];
    if (c == '\n') { sh_puts("\\n"); return; }
    if (c == '\t') { sh_puts("\\t"); return; }
    if (c == '\r') { sh_puts("\\r"); return; }
    if (c == '\\') { sh_puts("\\\\"); return; }
    if (c >= 32u && c <= 126u) { tmp[0] = (char)c; tmp[1] = 0; sh_puts(tmp); return; }
    static const char hex[] = "0123456789abcdef";
    tmp[0] = '\\'; tmp[1] = 'x'; tmp[2] = hex[(c >> 4u) & 0xfu]; tmp[3] = hex[c & 0xfu]; tmp[4] = 0;
    sh_puts(tmp);
}

static void sh_print_shell_word(const char *s) {
    if (!s) { sh_puts("''"); return; }
    int simple = *s != 0;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (!(sh_is_name_char(c) || c == '/' || c == '.' || c == '-' || c == ':' || c == '+')) { simple = 0; break; }
    }
    if (simple) { sh_puts(s); return; }
    sh_puts("'");
    for (const char *p = s; *p; ++p) {
        if (*p == '\'') sh_puts("'\\''");
        else { char b[2]; b[0] = *p; b[1] = 0; sh_puts(b); }
    }
    sh_puts("'");
}

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

static int sh_copy(char *dst, au_usize cap, const char *src) {
    if (!dst || !cap) return 0;
    dst[0] = 0;
    if (!src) return 0;
    au_usize n = au_strlen(src);
    if (n >= cap) return 0;
    au_memcpy(dst, src, n + 1u);
    return 1;
}

static int sh_copy_n(char *dst, au_usize cap, const char *src, au_usize n) {
    if (!dst || !cap) return 0;
    dst[0] = 0;
    if (!src || n >= cap) return 0;
    au_memcpy(dst, src, n);
    dst[n] = 0;
    return 1;
}

static int sh_strneq(const char *a, const char *b, au_usize n) {
    if (!a || !b) return 0;
    for (au_usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int sh_append_char(char *dst, au_usize cap, au_usize *off, char c) {
    if (!dst || !off || *off + 1u >= cap) return 0;
    dst[(*off)++] = c;
    dst[*off] = 0;
    return 1;
}

static int sh_append_n(char *dst, au_usize cap, au_usize *off, const char *src, au_usize n) {
    if (!dst || !off || !src || *off + n >= cap) return 0;
    if (n) au_memcpy(dst + *off, src, n);
    *off += n;
    dst[*off] = 0;
    return 1;
}

static int sh_append_str(char *dst, au_usize cap, au_usize *off, const char *src) {
    return sh_append_n(dst, cap, off, src ? src : "", src ? au_strlen(src) : 0u);
}

static void sh_u64(au_u64 v, char *out, au_usize cap) {
    char tmp[24];
    au_usize n = 0;
    if (!out || cap == 0) return;
    out[0] = 0;
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
    out[0] = 0;
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
    sh_i64(v, buf, sizeof(buf));
    sh_puts(buf);
}

static void sh_print_u64(au_u64 v) {
    char buf[32];
    sh_u64(v, buf, sizeof(buf));
    sh_puts(buf);
}

static void sh_print_hex64(au_u64 v) {
    char buf[32];
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

static int sh_var_name_ok(const char *name) {
    if (!name || !sh_is_name_start(name[0])) return 0;
    for (const char *p = name + 1; *p; ++p) if (!sh_is_name_char(*p)) return 0;
    return 1;
}

static int sh_assignment_token(const char *s, char *name, au_usize name_cap, const char **value_out) {
    if (!s || !name || !name_cap || !value_out || !sh_is_name_start(s[0])) return 0;
    const char *eq = s;
    while (*eq && *eq != '=') {
        if (!sh_is_name_char(*eq)) return 0;
        ++eq;
    }
    if (*eq != '=') return 0;
    if (!sh_copy_n(name, name_cap, s, (au_usize)(eq - s))) return 0;
    *value_out = eq + 1;
    return 1;
}

static int sh_var_find(const char *name) {
    if (!name) return -1;
    for (unsigned int i = 0; i < SH_VAR_MAX; ++i) {
        if (sh_vars[i].name[0] && sh_streq(sh_vars[i].name, name)) return (int)i;
    }
    return -1;
}

static int sh_var_slot(const char *name) {
    int found = sh_var_find(name);
    if (found >= 0) return found;
    for (unsigned int i = 0; i < SH_VAR_MAX; ++i) if (!sh_vars[i].name[0]) return (int)i;
    return -1;
}

static int sh_var_set(const char *name, const char *value, unsigned int exported) {
    if (!sh_var_name_ok(name)) return 0;
    int idx = sh_var_slot(name);
    if (idx < 0) return 0;
    if (!sh_copy(sh_vars[idx].name, sizeof(sh_vars[idx].name), name)) return 0;
    if (!sh_copy(sh_vars[idx].value, sizeof(sh_vars[idx].value), value ? value : "")) return 0;
    if (exported) sh_vars[idx].exported = 1;
    return 1;
}

static int sh_var_unset(const char *name) {
    int idx = sh_var_find(name);
    if (idx < 0) return 0;
    au_memset(&sh_vars[idx], 0, sizeof(sh_vars[idx]));
    return 1;
}

static const char *sh_user_name(void) {
    static char user[RABBITBONE_USER_NAME_MAX];
    au_credinfo_t c;
    au_memset(&c, 0, sizeof(c));
    if (au_cred(RABBITBONE_CRED_OP_GET, &c, 0, 0) == 0 && c.user[0]) {
        sh_copy(user, sizeof(user), c.euid == RABBITBONE_UID_ROOT ? "root" : c.user);
        return user;
    }
    return "user";
}

static const char *sh_home_for_user(const char *user) {
    if (user && sh_streq(user, "rabbitbone")) return "/home/rabbitbone";
    if (user && sh_streq(user, "guest")) return "/tmp";
    if (user && sh_streq(user, "root")) return "/";
    return "/";
}

static const char *sh_var_get(const char *name) {
    static char dyn[SH_TOKEN_MAX];
    if (!name) return "";
    if (sh_streq(name, "?")) { sh_i64((au_i64)sh_last_status, dyn, sizeof(dyn)); return dyn; }
    if (sh_streq(name, "$")) { sh_u64(sh_shell_pid, dyn, sizeof(dyn)); return dyn; }
    if (sh_streq(name, "PWD")) { if (au_getcwd(dyn, sizeof(dyn)) < 0) sh_copy(dyn, sizeof(dyn), "/"); return dyn; }
    if (sh_streq(name, "USER")) return sh_user_name();
    if (sh_streq(name, "HOME")) return sh_home_for_user(sh_user_name());
    int idx = sh_var_find(name);
    if (idx >= 0) return sh_vars[idx].value;
    return "";
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
    const char *v = sh_var_get("PATH");
    if (v && *v) return v;
    sh_load_path_env();
    return sh_path_env[0] ? sh_path_env : SH_DEFAULT_PATH;
}

static int sh_set_path_value(const char *value) {
    if (!value || !*value) return 0;
    if (!sh_copy(sh_path_env, sizeof(sh_path_env), value)) return 0;
    sh_path_env_loaded = 1;
    return sh_var_set("PATH", value, 1);
}

static void sh_refresh_base_env(void) {
    sh_load_path_env();
    if (sh_var_find("PATH") < 0) (void)sh_var_set("PATH", sh_path_env, 1);
    (void)sh_var_set("SHELL", "/disk0/bin/rbsh", 1);
    (void)sh_var_set("TERM", "rabbitbone", 1);
}

static int sh_build_env(char storage[RABBITBONE_ENV_MAX][SH_ENV_ENTRY_MAX], const char *envp[RABBITBONE_ENV_MAX], const sh_stage_t *stage) {
    unsigned int n = 0;
    sh_refresh_base_env();
    for (unsigned int i = 0; i < SH_VAR_MAX && n < RABBITBONE_ENV_MAX; ++i) {
        if (!sh_vars[i].name[0] || !sh_vars[i].exported) continue;
        au_usize off = 0;
        storage[n][0] = 0;
        if (!sh_append_str(storage[n], SH_ENV_ENTRY_MAX, &off, sh_vars[i].name)) continue;
        if (!sh_append_char(storage[n], SH_ENV_ENTRY_MAX, &off, '=')) continue;
        if (!sh_append_str(storage[n], SH_ENV_ENTRY_MAX, &off, sh_vars[i].value)) continue;
        envp[n] = storage[n];
        ++n;
    }
    if (stage) {
        for (unsigned int i = 0; i < stage->assign_count && n < RABBITBONE_ENV_MAX; ++i) {
            au_usize off = 0;
            storage[n][0] = 0;
            if (!sh_append_str(storage[n], SH_ENV_ENTRY_MAX, &off, stage->assign_name[i])) continue;
            if (!sh_append_char(storage[n], SH_ENV_ENTRY_MAX, &off, '=')) continue;
            if (!sh_append_str(storage[n], SH_ENV_ENTRY_MAX, &off, stage->assign_value[i])) continue;
            envp[n] = storage[n];
            ++n;
        }
    }
    return (int)n;
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
