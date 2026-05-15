#include <rabbitbone_sys.h>

#define RN_LINE_MAX 512u
#define RN_LINES_MAX 1024u
#define RN_PATH_MAX RABBITBONE_PATH_MAX
#define RN_MSG_MAX 128u
#define RN_PROMPT_MAX 192u
#define RN_CLIP_MAX RN_LINE_MAX
#define RN_LOG_PATH "/tmp/nano.log"
#define RN_NODE_FILE 1u
#define RN_NODE_DIR 2u
#define RN_SAVE_SUFFIX ".nano.tmp"

#if defined(__clang__) || defined(__GNUC__)
#define RN_NOINLINE __attribute__((noinline))
#else
#define RN_NOINLINE
#endif

typedef struct rn_editor {
    char path[RN_PATH_MAX];
    char lines[RN_LINES_MAX][RN_LINE_MAX];
    unsigned short len[RN_LINES_MAX];
    unsigned int line_count;
    unsigned int cy;
    unsigned int cx;
    unsigned int rowoff;
    unsigned int coloff;
    unsigned int rows;
    unsigned int cols;
    unsigned int dirty;
    unsigned int final_newline;
    unsigned int quit_confirm;
    char msg[RN_MSG_MAX];
    char clip[RN_CLIP_MAX];
    unsigned short clip_len;
} rn_editor_t;

static rn_editor_t rn;

static void rn_set_msg(const char *s);

static au_i64 rn_write_all(au_i64 fd, const char *s, au_usize n) {
    au_usize off = 0;
    while (off < n) {
        au_i64 wrote = au_write(fd, s + off, n - off);
        if (wrote < 0) return wrote;
        if (wrote == 0) return RABBITBONE_ERR_IO;
        off += (au_usize)wrote;
    }
    return 0;
}

static void rn_write_fd(au_i64 fd, const char *s, au_usize n) {
    (void)rn_write_all(fd, s, n);
}

static void rn_puts_fd(au_i64 fd, const char *s) {
    rn_write_fd(fd, s, au_strlen(s));
}

static void rn_puts(const char *s) { rn_puts_fd((au_i64)RABBITBONE_STDOUT, s); }
static void rn_err(const char *s) { rn_puts_fd((au_i64)RABBITBONE_STDERR, s); }

static int rn_streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a || *b) if (*a++ != *b++) return 0;
    return 1;
}

static int rn_copy(char *dst, au_usize cap, const char *src) {
    au_usize i = 0;
    if (!dst || cap == 0) return 0;
    if (!src) src = "";
    while (src[i] && i + 1u < cap) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = 0;
    return src[i] == 0;
}

static void rn_u64(char *out, au_usize cap, au_u64 v) {
    char tmp[32];
    au_usize n = 0;
    if (!out || cap == 0) return;
    if (v == 0) {
        rn_copy(out, cap, "0");
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    au_usize j = 0;
    while (n && j + 1u < cap) out[j++] = tmp[--n];
    out[j] = 0;
}

static void rn_i64(char *out, au_usize cap, au_i64 v) {
    if (!out || cap == 0) return;
    if (v < 0) {
        if (cap < 2u) { out[0] = 0; return; }
        out[0] = '-';
        au_u64 mag = (au_u64)(-(v + 1)) + 1u;
        rn_u64(out + 1u, cap - 1u, mag);
        return;
    }
    rn_u64(out, cap, (au_u64)v);
}

static au_usize rn_append(char *dst, au_usize cap, const char *src) {
    au_usize off = dst ? au_strlen(dst) : 0u;
    if (!dst || cap == 0 || off >= cap) return off;
    if (!src) src = "";
    for (au_usize i = 0; src[i] && off + 1u < cap; ++i) dst[off++] = src[i];
    dst[off] = 0;
    return off;
}

static const char *rn_error_name(au_i64 e) {
    switch (e) {
    case RABBITBONE_ERR_NOENT: return "not found";
    case RABBITBONE_ERR_NOMEM: return "no memory";
    case RABBITBONE_ERR_INVAL: return "invalid path";
    case RABBITBONE_ERR_IO: return "I/O error";
    case RABBITBONE_ERR_NOTDIR: return "parent is not directory";
    case RABBITBONE_ERR_ISDIR: return "is directory";
    case RABBITBONE_ERR_EXIST: return "exists";
    case RABBITBONE_ERR_PERM: return "permission denied";
    case RABBITBONE_ERR_NOSPC: return "no space";
    case RABBITBONE_ERR_NOTEMPTY: return "not empty";
    case RABBITBONE_ERR_UNSUPPORTED: return "unsupported";
    case RABBITBONE_ERR_BUSY: return "busy";
    default: return "error";
    }
}

static void rn_make_error(char *out, au_usize cap, const char *prefix, const char *path, au_i64 err) {
    char num[32];
    if (!out || cap == 0) return;
    out[0] = 0;
    rn_append(out, cap, prefix ? prefix : "failed");
    rn_append(out, cap, ": ");
    rn_append(out, cap, rn_error_name(err));
    if (path && path[0]) {
        rn_append(out, cap, ": ");
        rn_append(out, cap, path);
    }
    rn_append(out, cap, " (");
    rn_i64(num, sizeof(num), err);
    rn_append(out, cap, num);
    rn_append(out, cap, ")");
}

static void rn_set_error(const char *prefix, const char *path, au_i64 err) {
    char msg[RN_MSG_MAX];
    rn_make_error(msg, sizeof(msg), prefix, path, err);
    rn_set_msg(msg);
}

static void rn_log_event(const char *op, const char *path, au_i64 err) {
    char line[RN_PATH_MAX + 128u];
    char num[32];
    line[0] = 0;
    rn_append(line, sizeof(line), "nano ");
    rn_append(line, sizeof(line), op ? op : "op");
    rn_append(line, sizeof(line), " path=\"");
    rn_append(line, sizeof(line), path ? path : "");
    rn_append(line, sizeof(line), "\"");
    if (err < 0) {
        rn_append(line, sizeof(line), " err=");
        rn_i64(num, sizeof(num), err);
        rn_append(line, sizeof(line), num);
        rn_append(line, sizeof(line), " ");
        rn_append(line, sizeof(line), rn_error_name(err));
    } else {
        rn_append(line, sizeof(line), " ok");
    }
    rn_append(line, sizeof(line), "\n");
    au_i64 fd = au_open2(RN_LOG_PATH, RABBITBONE_O_WRONLY | RABBITBONE_O_CREAT | RABBITBONE_O_APPEND);
    if (fd >= 0) {
        (void)rn_write_all(fd, line, au_strlen(line));
        (void)au_close(fd);
    }
}

static void rn_set_msg(const char *s) {
    (void)rn_copy(rn.msg, sizeof(rn.msg), s ? s : "");
}

static void rn_measure(void) {
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0 && ti.rows > 2u && ti.cols > 10u) {
        rn.rows = ti.rows;
        rn.cols = ti.cols;
    } else {
        rn.rows = 25u;
        rn.cols = 80u;
    }
}

static unsigned int rn_body_rows(void) {
    return rn.rows > 3u ? rn.rows - 3u : 1u;
}

static unsigned int rn_message_row(void) {
    return rn.rows > 2u ? rn.rows - 2u : rn.rows - 1u;
}

static unsigned int rn_keybar_row(void) {
    return rn.rows - 1u;
}

static unsigned int rn_line_len(unsigned int row) {
    if (row >= rn.line_count) return 0;
    return (unsigned int)rn.len[row];
}

static void rn_clamp_cursor(void) {
    if (rn.line_count == 0) {
        rn.line_count = 1;
        rn.len[0] = 0;
        rn.lines[0][0] = 0;
    }
    if (rn.cy >= rn.line_count) rn.cy = rn.line_count - 1u;
    unsigned int len = rn_line_len(rn.cy);
    if (rn.cx > len) rn.cx = len;
}

static void rn_fix_view(void) {
    rn_clamp_cursor();
    unsigned int body = rn_body_rows();
    if (rn.cy < rn.rowoff) rn.rowoff = rn.cy;
    if (rn.cy >= rn.rowoff + body) rn.rowoff = rn.cy - body + 1u;
    if (rn.cx < rn.coloff) rn.coloff = rn.cx;
    if (rn.cx >= rn.coloff + rn.cols) rn.coloff = rn.cx - rn.cols + 1u;
}

static void rn_clear_line(unsigned int row) {
    (void)au_tty_setcursor(row, 0);
    (void)au_tty_clearline();
}

static void rn_draw_status(void) {
    char num[32];
    au_usize off = 0;
    char bar[RN_PROMPT_MAX];
    bar[0] = 0;
    const char *name = rn.path[0] ? rn.path : "[new]";
    (void)rn_copy(bar, sizeof(bar), name);
    off = au_strlen(bar);
    if (rn.dirty && off + 2u < sizeof(bar)) { bar[off++] = ' '; bar[off++] = '*'; bar[off] = 0; }
    if (off + 2u < sizeof(bar)) { bar[off++] = ' '; bar[off] = 0; }
    rn_u64(num, sizeof(num), (au_u64)rn.cy + 1u);
    for (au_usize i = 0; num[i] && off + 1u < sizeof(bar); ++i) bar[off++] = num[i];
    if (off + 1u < sizeof(bar)) bar[off++] = ':';
    rn_u64(num, sizeof(num), (au_u64)rn.cx + 1u);
    for (au_usize i = 0; num[i] && off + 1u < sizeof(bar); ++i) bar[off++] = num[i];
    bar[off] = 0;
    rn_clear_line(0);
    rn_write_fd((au_i64)RABBITBONE_STDOUT, bar, off > rn.cols ? rn.cols : off);
}

static void rn_draw_rows(void) {
    unsigned int body = rn_body_rows();
    for (unsigned int y = 0; y < body; ++y) {
        unsigned int filerow = rn.rowoff + y;
        rn_clear_line(y + 1u);
        if (filerow >= rn.line_count) continue;
        unsigned int len = rn_line_len(filerow);
        if (rn.coloff >= len) continue;
        unsigned int n = len - rn.coloff;
        if (n > rn.cols) n = rn.cols;
        for (unsigned int i = 0; i < n; ++i) {
            char c = rn.lines[filerow][rn.coloff + i];
            if (c == '\t') c = ' ';
            if ((unsigned char)c < 32u) c = ' ';
            rn_write_fd((au_i64)RABBITBONE_STDOUT, &c, 1u);
        }
    }
}

static void rn_draw_message(void) {
    rn_clear_line(rn_message_row());
    if (rn.msg[0]) {
        au_usize n = au_strlen(rn.msg);
        if (n > rn.cols) n = rn.cols;
        rn_write_fd((au_i64)RABBITBONE_STDOUT, rn.msg, n);
    }
}

static void rn_draw_keybar(void) {
    static const char keys[] = "^S Save  ^O Write  ^F Find  ^G GoTo  ^K Cut  ^U Paste  ^X Exit";
    rn_clear_line(rn_keybar_row());
    au_usize n = sizeof(keys) - 1u;
    if (n > rn.cols) n = rn.cols;
    rn_write_fd((au_i64)RABBITBONE_STDOUT, keys, n);
}

static void rn_refresh(void) {
    rn_measure();
    rn_fix_view();
    (void)au_tty_cursor_visible(0);
    rn_draw_status();
    rn_draw_rows();
    rn_draw_message();
    rn_draw_keybar();
    unsigned int crow = 1u + rn.cy - rn.rowoff;
    unsigned int ccol = rn.cx >= rn.coloff ? rn.cx - rn.coloff : 0u;
    unsigned int max_cursor_row = rn_message_row() > 0u ? rn_message_row() - 1u : 0u;
    if (crow > max_cursor_row) crow = max_cursor_row;
    if (ccol >= rn.cols) ccol = rn.cols - 1u;
    (void)au_tty_setcursor(crow, ccol);
    (void)au_tty_cursor_visible(1);
}

static void rn_mark_dirty(void) {
    rn.dirty = 1;
    rn.quit_confirm = 0;
}

static int rn_insert_line_at(unsigned int at, const char *text, unsigned int len) {
    if (rn.line_count >= RN_LINES_MAX || at > rn.line_count) return 0;
    if (len >= RN_LINE_MAX) return 0;
    for (unsigned int i = rn.line_count; i > at; --i) {
        rn.len[i] = rn.len[i - 1u];
        au_memcpy(rn.lines[i], rn.lines[i - 1u], (au_usize)rn.len[i - 1u] + 1u);
    }
    if (text && len) au_memcpy(rn.lines[at], text, len);
    rn.lines[at][len] = 0;
    rn.len[at] = (unsigned short)len;
    ++rn.line_count;
    return 1;
}

static void rn_delete_line_at(unsigned int at) {
    if (at >= rn.line_count) return;
    for (unsigned int i = at + 1u; i < rn.line_count; ++i) {
        rn.len[i - 1u] = rn.len[i];
        au_memcpy(rn.lines[i - 1u], rn.lines[i], (au_usize)rn.len[i] + 1u);
    }
    if (rn.line_count > 1u) --rn.line_count;
    else { rn.len[0] = 0; rn.lines[0][0] = 0; }
}

static void rn_insert_char(char c) {
    rn_clamp_cursor();
    if (c == '\t') {
        for (unsigned int i = 0; i < 4u; ++i) rn_insert_char(' ');
        return;
    }
    unsigned int len = rn_line_len(rn.cy);
    if (len + 1u >= RN_LINE_MAX) { rn_set_msg("line too long"); return; }
    char *line = rn.lines[rn.cy];
    au_memmove(line + rn.cx + 1u, line + rn.cx, (au_usize)(len - rn.cx) + 1u);
    line[rn.cx] = c;
    ++rn.cx;
    rn.len[rn.cy] = (unsigned short)(len + 1u);
    rn_mark_dirty();
}

static void rn_insert_newline(void) {
    rn_clamp_cursor();
    if (rn.line_count >= RN_LINES_MAX) { rn_set_msg("too many lines"); return; }
    unsigned int len = rn_line_len(rn.cy);
    unsigned int tail = len - rn.cx;
    char tailbuf[RN_LINE_MAX];
    if (tail) au_memcpy(tailbuf, rn.lines[rn.cy] + rn.cx, tail);
    tailbuf[tail] = 0;
    rn.lines[rn.cy][rn.cx] = 0;
    rn.len[rn.cy] = (unsigned short)rn.cx;
    if (!rn_insert_line_at(rn.cy + 1u, tailbuf, tail)) { rn_set_msg("too many lines"); return; }
    ++rn.cy;
    rn.cx = 0;
    rn.final_newline = 1;
    rn_mark_dirty();
}

static void rn_backspace(void) {
    rn_clamp_cursor();
    if (rn.cx > 0) {
        unsigned int len = rn_line_len(rn.cy);
        char *line = rn.lines[rn.cy];
        au_memmove(line + rn.cx - 1u, line + rn.cx, (au_usize)(len - rn.cx) + 1u);
        --rn.cx;
        rn.len[rn.cy] = (unsigned short)(len - 1u);
        rn_mark_dirty();
        return;
    }
    if (rn.cy == 0) return;
    unsigned int prev_len = rn_line_len(rn.cy - 1u);
    unsigned int cur_len = rn_line_len(rn.cy);
    if (prev_len + cur_len >= RN_LINE_MAX) { rn_set_msg("line too long"); return; }
    au_memcpy(rn.lines[rn.cy - 1u] + prev_len, rn.lines[rn.cy], (au_usize)cur_len + 1u);
    rn.len[rn.cy - 1u] = (unsigned short)(prev_len + cur_len);
    rn_delete_line_at(rn.cy);
    --rn.cy;
    rn.cx = prev_len;
    rn_mark_dirty();
}

static void rn_delete(void) {
    rn_clamp_cursor();
    unsigned int len = rn_line_len(rn.cy);
    if (rn.cx < len) {
        char *line = rn.lines[rn.cy];
        au_memmove(line + rn.cx, line + rn.cx + 1u, (au_usize)(len - rn.cx));
        rn.len[rn.cy] = (unsigned short)(len - 1u);
        rn_mark_dirty();
        return;
    }
    if (rn.cy + 1u >= rn.line_count) return;
    unsigned int next_len = rn_line_len(rn.cy + 1u);
    if (len + next_len >= RN_LINE_MAX) { rn_set_msg("line too long"); return; }
    au_memcpy(rn.lines[rn.cy] + len, rn.lines[rn.cy + 1u], (au_usize)next_len + 1u);
    rn.len[rn.cy] = (unsigned short)(len + next_len);
    rn_delete_line_at(rn.cy + 1u);
    rn_mark_dirty();
}

static void rn_cut_line(void) {
    rn_clamp_cursor();
    unsigned int len = rn_line_len(rn.cy);
    rn.clip_len = (unsigned short)len;
    au_memcpy(rn.clip, rn.lines[rn.cy], (au_usize)len + 1u);
    rn_delete_line_at(rn.cy);
    if (rn.cy >= rn.line_count) rn.cy = rn.line_count - 1u;
    rn.cx = 0;
    rn_mark_dirty();
}

static void rn_paste_line(void) {
    if (rn.clip_len >= RN_LINE_MAX) return;
    unsigned int at = rn.cy + 1u;
    if (!rn_insert_line_at(at, rn.clip, rn.clip_len)) { rn_set_msg("too many lines"); return; }
    rn.cy = at;
    rn.cx = rn.clip_len;
    rn_mark_dirty();
}

static int rn_has_prefix(const char *s, const char *p) {
    while (*p) if (*s++ != *p++) return 0;
    return 1;
}

static int rn_parse_u32(const char *s, unsigned int *out) {
    au_u64 v = 0;
    if (!s || !*s || !out) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        v = v * 10u + (au_u64)(*s - '0');
        if (v > 0xffffffffull) return 0;
        ++s;
    }
    *out = (unsigned int)v;
    return 1;
}

static int rn_find_from(const char *needle, unsigned int sy, unsigned int sx, unsigned int *oy, unsigned int *ox) {
    au_usize nlen = au_strlen(needle);
    if (!needle || !needle[0]) return 0;
    for (unsigned int pass = 0; pass < 2u; ++pass) {
        unsigned int y0 = pass == 0 ? sy : 0u;
        unsigned int y1 = pass == 0 ? rn.line_count : sy + 1u;
        for (unsigned int y = y0; y < y1; ++y) {
            unsigned int startx = (pass == 0 && y == sy) ? sx : 0u;
            unsigned int len = rn_line_len(y);
            if (nlen > (au_usize)len) continue;
            for (unsigned int x = startx; x + (unsigned int)nlen <= len; ++x) {
                if (rn_has_prefix(rn.lines[y] + x, needle)) {
                    if (oy) *oy = y;
                    if (ox) *ox = x;
                    return 1;
                }
            }
        }
    }
    return 0;
}

static int rn_prompt(const char *label, char *out, au_usize cap) {
    au_usize len = au_strlen(out);
    unsigned int old_confirm = rn.quit_confirm;
    rn.quit_confirm = 0;
    for (;;) {
        rn_refresh();
        rn_clear_line(rn_message_row());
        rn_draw_keybar();
        rn_puts(label);
        rn_write_fd((au_i64)RABBITBONE_STDOUT, out, len);
        (void)au_tty_setcursor(rn_message_row(), (unsigned int)(au_strlen(label) + len));
        au_key_event_t ev;
        if (au_tty_readkey(&ev, RABBITBONE_TTY_READ_NONBLOCK) != 0) { rn.quit_confirm = old_confirm; return 0; }
        if (ev.code == RABBITBONE_KEY_NONE) { (void)au_sleep(1); continue; }
        if (ev.ch == 3u || ev.code == RABBITBONE_KEY_ESC) { rn_set_msg(""); rn.quit_confirm = old_confirm; return 0; }
        if (ev.code == RABBITBONE_KEY_ENTER || ev.ch == '\n') { out[len] = 0; rn_set_msg(""); rn.quit_confirm = old_confirm; return 1; }
        if (ev.code == RABBITBONE_KEY_BACKSPACE || ev.ch == '\b') { if (len) out[--len] = 0; continue; }
        if (ev.ch >= 32u && ev.ch <= 126u && len + 1u < cap) { out[len++] = (char)ev.ch; out[len] = 0; }
    }
}

static int rn_load_file(const char *path) {
    rn.line_count = 1;
    rn.len[0] = 0;
    rn.lines[0][0] = 0;
    rn.final_newline = 0;
    if (!path || !*path) return 0;
    au_stat_t st;
    au_memset(&st, 0, sizeof(st));
    au_i64 sr = au_lstat(path, &st);
    if (sr == 0 && st.type == RN_NODE_DIR) { rn_err("nano: is directory: "); rn_err(path); rn_err("\n"); rn_log_event("open", path, RABBITBONE_ERR_ISDIR); return 1; }
    if (sr < 0 && sr != RABBITBONE_ERR_NOENT) { rn_err("nano: cannot stat: "); rn_err(path); rn_err("\n"); rn_log_event("stat", path, sr); return 1; }
    au_i64 fd = au_open(path);
    if (fd == RABBITBONE_ERR_NOENT) return 0;
    if (fd < 0) { rn_err("nano: cannot open: "); rn_err(path); rn_err("\n"); rn_log_event("open", path, fd); return 1; }
    char buf[512];
    unsigned int y = 0;
    unsigned int x = 0;
    for (;;) {
        au_i64 got = au_read(fd, buf, sizeof(buf));
        if (got < 0) { (void)au_close(fd); rn_err("nano: read failed\n"); return 1; }
        if (got == 0) break;
        for (au_i64 i = 0; i < got; ++i) {
            char c = buf[i];
            if (c == '\r') continue;
            if (c == '\n') {
                rn.lines[y][x] = 0;
                rn.len[y] = (unsigned short)x;
                rn.final_newline = 1;
                if (y + 1u >= RN_LINES_MAX) { (void)au_close(fd); rn_err("nano: too many lines\n"); return 1; }
                ++y;
                x = 0;
                rn.line_count = y + 1u;
                rn.lines[y][0] = 0;
                rn.len[y] = 0;
                continue;
            }
            rn.final_newline = 0;
            if (x + 1u >= RN_LINE_MAX) { (void)au_close(fd); rn_err("nano: line too long\n"); return 1; }
            rn.lines[y][x++] = c;
        }
    }
    (void)au_close(fd);
    rn.lines[y][x] = 0;
    rn.len[y] = (unsigned short)x;
    if (rn.line_count == 0) rn.line_count = 1;
    if (rn.line_count > 1u && rn.len[rn.line_count - 1u] == 0 && rn.final_newline) {
        --rn.line_count;
    }
    return 0;
}

static au_i64 rn_write_buffer(au_i64 fd) {
    for (unsigned int y = 0; y < rn.line_count; ++y) {
        au_i64 rc = 0;
        if (rn.len[y]) {
            rc = rn_write_all(fd, rn.lines[y], rn.len[y]);
            if (rc < 0) return rc;
        }
        if (y + 1u < rn.line_count || rn.final_newline) {
            rc = rn_write_all(fd, "\n", 1u);
            if (rc < 0) return rc;
        }
    }
    return 0;
}

static int rn_tmp_path(const char *path, char *out, au_usize cap) {
    if (!path || !path[0] || !out || cap == 0) return 0;
    au_usize n = au_strlen(path);
    au_usize s = au_strlen(RN_SAVE_SUFFIX);
    if (n + s + 1u > cap) return 0;
    au_memcpy(out, path, n);
    au_memcpy(out + n, RN_SAVE_SUFFIX, s + 1u);
    return 1;
}

static int rn_target_is_writable_file(const char *path) {
    au_stat_t st;
    au_memset(&st, 0, sizeof(st));
    au_i64 sr = au_lstat(path, &st);
    if (sr == RABBITBONE_ERR_NOENT) return 1;
    if (sr < 0) { rn_set_error("write failed", path, sr); rn_log_event("stat", path, sr); return 0; }
    if (st.type == RN_NODE_DIR) { rn_set_error("write failed", path, RABBITBONE_ERR_ISDIR); rn_log_event("stat", path, RABBITBONE_ERR_ISDIR); return 0; }
    if (st.type != RN_NODE_FILE) { rn_set_error("write failed", path, RABBITBONE_ERR_UNSUPPORTED); rn_log_event("stat", path, RABBITBONE_ERR_UNSUPPORTED); return 0; }
    return 1;
}

static int rn_save_to(const char *path) {
    char tmp[RN_PATH_MAX];
    if (!path || !*path) { rn_set_msg("no file"); return 0; }
    if (!rn_target_is_writable_file(path)) return 0;
    if (!rn_tmp_path(path, tmp, sizeof(tmp))) { rn_set_msg("path too long"); rn_log_event("temp", path, RABBITBONE_ERR_INVAL); return 0; }

    au_i64 fd = au_open2(tmp, RABBITBONE_O_WRONLY | RABBITBONE_O_CREAT | RABBITBONE_O_TRUNC);
    if (fd < 0) { rn_set_error("write failed", tmp, fd); rn_log_event("open", tmp, fd); return 0; }

    au_i64 rc = rn_write_buffer(fd);
    if (rc == 0) {
        au_i64 fr = au_fsync(fd);
        if (fr < 0 && fr != RABBITBONE_ERR_UNSUPPORTED) rc = fr;
    }
    au_i64 cr = au_close(fd);
    if (rc == 0 && cr < 0) rc = cr;
    if (rc < 0) {
        rn_set_error("write failed", path, rc);
        rn_log_event("write", path, rc);
        (void)au_unlink(tmp);
        return 0;
    }

    rc = au_rename(tmp, path);
    if (rc < 0) {
        rn_set_error("write failed", path, rc);
        rn_log_event("rename", path, rc);
        (void)au_unlink(tmp);
        return 0;
    }
    rn.dirty = 0;
    rn.quit_confirm = 0;
    rn_set_msg("saved");
    rn_log_event("save", path, 0);
    return 1;
}

static void rn_save(void) {
    if (!rn.path[0]) {
        char p[RN_PATH_MAX];
        p[0] = 0;
        if (!rn_prompt("write: ", p, sizeof(p))) return;
        if (!p[0]) { rn_set_msg("no file"); return; }
        if (rn_save_to(p)) (void)rn_copy(rn.path, sizeof(rn.path), p);
        return;
    }
    (void)rn_save_to(rn.path);
}

static void rn_save_as(void) {
    char p[RN_PATH_MAX];
    (void)rn_copy(p, sizeof(p), rn.path);
    if (!rn_prompt("write: ", p, sizeof(p))) return;
    if (!p[0]) { rn_set_msg("no file"); return; }
    if (rn_save_to(p)) (void)rn_copy(rn.path, sizeof(rn.path), p);
}

static void rn_search(void) {
    char q[RN_PROMPT_MAX];
    q[0] = 0;
    if (!rn_prompt("search: ", q, sizeof(q))) return;
    unsigned int y = 0, x = 0;
    unsigned int sx = rn.cx + 1u;
    unsigned int sy = rn.cy;
    if (rn_find_from(q, sy, sx, &y, &x)) {
        rn.cy = y;
        rn.cx = x;
        rn_set_msg("");
    } else {
        rn_set_msg("not found");
    }
}

static void rn_goto_line(void) {
    char q[32];
    q[0] = 0;
    if (!rn_prompt("line: ", q, sizeof(q))) return;
    unsigned int line = 0;
    if (!rn_parse_u32(q, &line) || line == 0) { rn_set_msg("invalid line"); return; }
    if (line > rn.line_count) line = rn.line_count;
    rn.cy = line - 1u;
    rn.cx = 0;
}

static void rn_move_left(void) {
    rn_clamp_cursor();
    if (rn.cx > 0) --rn.cx;
    else if (rn.cy > 0) { --rn.cy; rn.cx = rn_line_len(rn.cy); }
    rn.quit_confirm = 0;
}

static void rn_move_right(void) {
    rn_clamp_cursor();
    unsigned int len = rn_line_len(rn.cy);
    if (rn.cx < len) ++rn.cx;
    else if (rn.cy + 1u < rn.line_count) { ++rn.cy; rn.cx = 0; }
    rn.quit_confirm = 0;
}

static void rn_move_up(void) {
    if (rn.cy > 0) --rn.cy;
    rn_clamp_cursor();
    rn.quit_confirm = 0;
}

static void rn_move_down(void) {
    if (rn.cy + 1u < rn.line_count) ++rn.cy;
    rn_clamp_cursor();
    rn.quit_confirm = 0;
}

static void rn_page_up(void) {
    unsigned int n = rn_body_rows();
    rn.cy = rn.cy > n ? rn.cy - n : 0u;
    rn_clamp_cursor();
    rn.quit_confirm = 0;
}

static void rn_page_down(void) {
    unsigned int n = rn_body_rows();
    rn.cy += n;
    if (rn.cy >= rn.line_count) rn.cy = rn.line_count - 1u;
    rn_clamp_cursor();
    rn.quit_confirm = 0;
}

static void rn_home(void) { rn.cx = 0; rn.quit_confirm = 0; }
static void rn_end(void) { rn_clamp_cursor(); rn.cx = rn_line_len(rn.cy); rn.quit_confirm = 0; }

static int rn_handle_key(const au_key_event_t *ev) {
    if (!ev) return 1;
    if (ev->ch == 24u) {
        if (rn.dirty && !rn.quit_confirm) { rn.quit_confirm = 1; rn_set_msg("modified; ^X again"); return 1; }
        return 0;
    }
    if (ev->ch == 17u) return 0;
    if (ev->ch == 19u) { rn_save(); return 1; }
    if (ev->ch == 15u) { rn_save_as(); return 1; }
    if (ev->ch == 6u) { rn_search(); return 1; }
    if (ev->ch == 7u) { rn_goto_line(); return 1; }
    if (ev->ch == 11u) { rn_cut_line(); return 1; }
    if (ev->ch == 21u) { rn_paste_line(); return 1; }
    if (ev->ch == 12u) { rn_set_msg(""); (void)au_tty_clear(); return 1; }
    if (ev->code == RABBITBONE_KEY_UP) { rn_move_up(); return 1; }
    if (ev->code == RABBITBONE_KEY_DOWN) { rn_move_down(); return 1; }
    if (ev->code == RABBITBONE_KEY_LEFT) { rn_move_left(); return 1; }
    if (ev->code == RABBITBONE_KEY_RIGHT) { rn_move_right(); return 1; }
    if (ev->code == RABBITBONE_KEY_HOME) { rn_home(); return 1; }
    if (ev->code == RABBITBONE_KEY_END) { rn_end(); return 1; }
    if (ev->code == RABBITBONE_KEY_PAGEUP) { rn_page_up(); return 1; }
    if (ev->code == RABBITBONE_KEY_PAGEDOWN) { rn_page_down(); return 1; }
    if (ev->code == RABBITBONE_KEY_DELETE) { rn_delete(); return 1; }
    if (ev->code == RABBITBONE_KEY_BACKSPACE || ev->ch == '\b') { rn_backspace(); return 1; }
    if (ev->code == RABBITBONE_KEY_ENTER || ev->ch == '\n') { rn_insert_newline(); return 1; }
    if (ev->code == RABBITBONE_KEY_TAB || ev->ch == '\t') { rn_insert_char('\t'); return 1; }
    if (ev->ch >= 32u && ev->ch <= 126u) { rn_insert_char((char)ev->ch); return 1; }
    return 1;
}

static int rn_usage(int rc) {
    rn_puts_fd(rc ? (au_i64)RABBITBONE_STDERR : (au_i64)RABBITBONE_STDOUT, "usage: nano [FILE]\n");
    return rc;
}

static RN_NOINLINE int rn_run(void) {
    for (;;) {
        rn_refresh();
        au_key_event_t ev;
        if (au_tty_readkey(&ev, RABBITBONE_TTY_READ_NONBLOCK) != 0) return 1;
        if (ev.code == RABBITBONE_KEY_NONE) { (void)au_sleep(1); continue; }
        if (!rn_handle_key(&ev)) return 0;
    }
}

int main(int argc, char **argv) {
    au_memset(&rn, 0, sizeof(rn));
    if (argc > 2) return rn_usage(1);
    if (argc == 2) {
        if (rn_streq(argv[1], "--help") || rn_streq(argv[1], "-h")) return rn_usage(0);
        if (!rn_copy(rn.path, sizeof(rn.path), argv[1])) { rn_err("nano: path too long\n"); return 1; }
    }
    if (rn_load_file(rn.path) != 0) return 1;
    rn.dirty = 0;
    rn.cy = 0;
    rn.cx = 0;
    unsigned int old_mode = RABBITBONE_TTY_MODE_CANON | RABBITBONE_TTY_MODE_ECHO;
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) old_mode = ti.mode;
    (void)au_tty_setmode(RABBITBONE_TTY_MODE_RAW);
    (void)au_tty_clear();
    (void)au_tty_setcursor(0, 0);
    int rc = rn_run();
    (void)au_tty_cursor_visible(1);
    (void)au_tty_setmode(old_mode);
    (void)au_tty_clear();
    (void)au_tty_setcursor(0, 0);
    return rc;
}
