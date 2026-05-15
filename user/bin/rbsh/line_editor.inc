#define SH_HISTORY_MAX 64u
#define SH_PROMPT_MAX 160u
#define SH_COMPLETION_MAX 64u
#define SH_COMPLETION_NAME_MAX 96u

static char sh_history[SH_HISTORY_MAX][SH_LINE_MAX];
static au_usize sh_history_count = 0;

static void sh_term_move_to(unsigned int row0, unsigned int col0) { (void)au_tty_setcursor(row0, col0); }
static void sh_term_clear_to_eol(void) { (void)au_tty_clearline(); }
static void sh_term_cursor_visible(unsigned int visible) { (void)au_tty_cursor_visible(visible); }

static unsigned int sh_tty_rows(void) {
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0 && ti.rows > 0) return ti.rows;
    return 25u;
}

static unsigned int sh_scroll_page(void) {
    unsigned int rows = sh_tty_rows();
    return rows > 4u ? rows - 2u : rows;
}

static void sh_make_prompt(char *out, au_usize cap) {
    char cwd[SH_PATH_MAX];
    au_usize off = 0;
    if (!out || cap == 0) return;
    out[0] = 0;
    if (au_getcwd(cwd, sizeof(cwd)) < 0) sh_copy(cwd, sizeof(cwd), "?");
    const char *user = sh_user_name();
    (void)sh_append_str(out, cap, &off, user);
    (void)sh_append_char(out, cap, &off, ':');
    if (!sh_append_str(out, cap, &off, cwd)) {
        out[0] = 0;
        off = 0;
        (void)sh_append_str(out, cap, &off, cwd);
    }
    (void)sh_append_char(out, cap, &off, sh_streq(user, "root") ? '#' : '$');
    (void)sh_append_char(out, cap, &off, ' ');
}

typedef struct sh_completion {
    char text[SH_COMPLETION_NAME_MAX];
    unsigned int type;
} sh_completion_t;

typedef struct sh_line_editor {
    char *line;
    au_usize cap;
    au_usize len;
    au_usize cursor;
    au_usize history_pos;
    char draft[SH_LINE_MAX];
    char prompt[SH_PROMPT_MAX];
    unsigned int base_row;
    unsigned int rows;
    unsigned int cols;
    unsigned int prompt_cols;
    unsigned int old_rows_used;
} sh_line_editor_t;

static au_usize sh_min_usize(au_usize a, au_usize b) { return a < b ? a : b; }
static unsigned int sh_max_u32(unsigned int a, unsigned int b) { return a > b ? a : b; }

static void sh_editor_remeasure(sh_line_editor_t *ed) {
    if (!ed) return;
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) {
        ed->rows = ti.rows ? ti.rows : 25u;
        ed->cols = ti.cols ? ti.cols : 80u;
    } else {
        ed->rows = 25u;
        ed->cols = 80u;
    }
    ed->prompt_cols = (unsigned int)au_strlen(ed->prompt);
    if (ed->cols == 0) ed->cols = 80u;
}

static unsigned int sh_editor_rows_for_len(const sh_line_editor_t *ed, au_usize len) {
    if (!ed || ed->cols == 0) return 1u;
    au_usize cells_before_cursor_after_end = (au_usize)ed->prompt_cols + len;
    unsigned int rows = (unsigned int)(cells_before_cursor_after_end / ed->cols) + 1u;
    if (rows == 0) rows = 1u;
    return rows;
}

static unsigned int sh_editor_rows_used(const sh_line_editor_t *ed) {
    return sh_editor_rows_for_len(ed, ed ? ed->len : 0u);
}

static void sh_editor_cursor_pos(const sh_line_editor_t *ed, unsigned int *row_out, unsigned int *col_out) {
    unsigned int row = 0, col = 0;
    if (ed && ed->cols) {
        au_usize pos = (au_usize)ed->prompt_cols + ed->cursor;
        row = ed->base_row + (unsigned int)(pos / ed->cols);
        col = (unsigned int)(pos % ed->cols);
        if (ed->rows && row >= ed->rows) row = ed->rows - 1u;
        if (col >= ed->cols) col = ed->cols - 1u;
    }
    if (row_out) *row_out = row;
    if (col_out) *col_out = col;
}

static void sh_editor_place_cursor(sh_line_editor_t *ed) {
    if (!ed) return;
    sh_editor_remeasure(ed);
    unsigned int row = 0, col = 0;
    sh_editor_cursor_pos(ed, &row, &col);
    sh_term_move_to(row, col);
}

static void sh_editor_ensure_space(sh_line_editor_t *ed, unsigned int rows_needed) {
    if (!ed) return;
    sh_editor_remeasure(ed);
    if (rows_needed == 0) rows_needed = 1u;
    if (rows_needed > ed->rows) rows_needed = ed->rows;
    if (ed->base_row + rows_needed <= ed->rows) return;
    unsigned int new_base = ed->rows > rows_needed ? ed->rows - rows_needed : 0u;
    unsigned int need_scroll = ed->base_row > new_base ? ed->base_row - new_base : 1u;
    sh_term_move_to(ed->rows - 1u, 0);
    for (unsigned int i = 0; i < need_scroll; ++i) sh_write_n_fd((au_i64)RABBITBONE_STDOUT, "\n", 1u);
    ed->base_row = new_base;
}

static void sh_editor_clear_rows(sh_line_editor_t *ed, unsigned int rows) {
    if (!ed || rows == 0) return;
    for (unsigned int r = 0; r < rows && ed->base_row + r < ed->rows; ++r) {
        sh_term_move_to(ed->base_row + r, 0);
        sh_term_clear_to_eol();
    }
}

static void sh_editor_write_content(sh_line_editor_t *ed) {
    if (!ed || !ed->line) return;
    sh_term_move_to(ed->base_row, 0);
    sh_puts(ed->prompt);
    sh_write_n_fd((au_i64)RABBITBONE_STDOUT, ed->line, ed->len);
}

static void sh_editor_refresh(sh_line_editor_t *ed) {
    if (!ed || !ed->line) return;
    sh_editor_remeasure(ed);
    unsigned int rows_needed = sh_editor_rows_used(ed);
    sh_editor_ensure_space(ed, rows_needed);
    unsigned int clear_rows = sh_max_u32(ed->old_rows_used, rows_needed);
    if (clear_rows > ed->rows) clear_rows = ed->rows;
    sh_term_cursor_visible(0);
    sh_editor_clear_rows(ed, clear_rows);
    sh_editor_write_content(ed);
    ed->old_rows_used = rows_needed;
    sh_editor_place_cursor(ed);
    sh_term_cursor_visible(1);
}

static void sh_history_add(const char *line) {
    if (!line || !line[0]) return;
    if (sh_history_count > 0 && sh_streq(sh_history[sh_history_count - 1u], line)) return;
    if (sh_history_count < SH_HISTORY_MAX) {
        sh_copy(sh_history[sh_history_count], sizeof(sh_history[0]), line);
        ++sh_history_count;
        return;
    }
    for (au_usize i = 1u; i < SH_HISTORY_MAX; ++i) sh_copy(sh_history[i - 1u], sizeof(sh_history[0]), sh_history[i]);
    sh_copy(sh_history[SH_HISTORY_MAX - 1u], sizeof(sh_history[0]), line);
}

static void sh_editor_load_text(sh_line_editor_t *ed, const char *text) {
    if (!ed || !ed->line || !text) return;
    au_usize n = au_strlen(text);
    if (n >= ed->cap) n = ed->cap - 1u;
    au_memcpy(ed->line, text, n);
    ed->line[n] = 0;
    ed->len = n;
    ed->cursor = n;
}

static void sh_editor_history_up(sh_line_editor_t *ed) {
    if (!ed || sh_history_count == 0) return;
    if (ed->history_pos == sh_history_count) sh_copy(ed->draft, sizeof(ed->draft), ed->line);
    if (ed->history_pos > 0) --ed->history_pos;
    sh_editor_load_text(ed, sh_history[ed->history_pos]);
    sh_editor_refresh(ed);
}

static void sh_editor_history_down(sh_line_editor_t *ed) {
    if (!ed || sh_history_count == 0 || ed->history_pos >= sh_history_count) return;
    ++ed->history_pos;
    if (ed->history_pos == sh_history_count) sh_editor_load_text(ed, ed->draft);
    else sh_editor_load_text(ed, sh_history[ed->history_pos]);
    sh_editor_refresh(ed);
}

static void sh_editor_insert_text(sh_line_editor_t *ed, const char *text, au_usize n) {
    if (!ed || !ed->line || !text || n == 0) return;
    if (ed->len + n >= ed->cap) n = ed->cap - 1u - ed->len;
    if (n == 0) return;
    for (au_usize i = ed->len + n; i > ed->cursor + n - 1u; --i) ed->line[i] = ed->line[i - n];
    au_memcpy(ed->line + ed->cursor, text, n);
    ed->len += n;
    ed->cursor += n;
    ed->line[ed->len] = 0;
    sh_editor_refresh(ed);
}

static void sh_editor_insert(sh_line_editor_t *ed, char c) {
    if (!ed || !ed->line || ed->len + 1u >= ed->cap) return;
    for (au_usize i = ed->len; i > ed->cursor; --i) ed->line[i] = ed->line[i - 1u];
    ed->line[ed->cursor] = c;
    ++ed->len;
    ++ed->cursor;
    ed->line[ed->len] = 0;
    sh_editor_refresh(ed);
}

static void sh_editor_backspace(sh_line_editor_t *ed) {
    if (!ed || !ed->line || ed->cursor == 0) return;
    for (au_usize i = ed->cursor - 1u; i < ed->len; ++i) ed->line[i] = ed->line[i + 1u];
    --ed->cursor;
    --ed->len;
    sh_editor_refresh(ed);
}

static void sh_editor_delete(sh_line_editor_t *ed) {
    if (!ed || !ed->line || ed->cursor >= ed->len) return;
    for (au_usize i = ed->cursor; i < ed->len; ++i) ed->line[i] = ed->line[i + 1u];
    --ed->len;
    sh_editor_refresh(ed);
}

static void sh_editor_move_left(sh_line_editor_t *ed) {
    if (!ed || ed->cursor == 0) return;
    --ed->cursor;
    sh_editor_place_cursor(ed);
}

static void sh_editor_move_right(sh_line_editor_t *ed) {
    if (!ed || ed->cursor >= ed->len) return;
    ++ed->cursor;
    sh_editor_place_cursor(ed);
}

static void sh_editor_move_home(sh_line_editor_t *ed) {
    if (!ed) return;
    ed->cursor = 0;
    sh_editor_place_cursor(ed);
}

static void sh_editor_move_end(sh_line_editor_t *ed) {
    if (!ed) return;
    ed->cursor = ed->len;
    sh_editor_place_cursor(ed);
}

static au_usize sh_common_prefix_len(const char *a, const char *b) {
    au_usize n = 0;
    if (!a || !b) return 0;
    while (a[n] && b[n] && a[n] == b[n]) ++n;
    return n;
}

static int sh_completion_seen(const sh_completion_t *items, unsigned int count, const char *text) {
    for (unsigned int i = 0; i < count; ++i) if (sh_streq(items[i].text, text)) return 1;
    return 0;
}

static int sh_completion_add(sh_completion_t *items, unsigned int *count, const char *text, unsigned int type) {
    if (!items || !count || !text || !*text || *count >= SH_COMPLETION_MAX) return 0;
    if (sh_completion_seen(items, *count, text)) return 1;
    if (!sh_copy(items[*count].text, sizeof(items[*count].text), text)) return 0;
    items[*count].type = type;
    ++*count;
    return 1;
}

static int sh_match_prefix(const char *name, const char *prefix) {
    if (!name || !prefix) return 0;
    while (*prefix) if (*name++ != *prefix++) return 0;
    return 1;
}

static unsigned int sh_token_start_for_completion(const sh_line_editor_t *ed) {
    if (!ed || !ed->line) return 0;
    unsigned int start = (unsigned int)ed->cursor;
    while (start > 0) {
        char c = ed->line[start - 1u];
        if (c == ' ' || c == '\t' || c == '|' || c == '<' || c == '>' || c == ';' || c == '&') break;
        --start;
    }
    return start;
}

static int sh_completion_is_command_position(const sh_line_editor_t *ed, unsigned int token_start) {
    if (!ed || !ed->line) return 1;
    unsigned int i = token_start;
    while (i > 0 && (ed->line[i - 1u] == ' ' || ed->line[i - 1u] == '\t')) --i;
    if (i == 0) return 1;
    return ed->line[i - 1u] == '|' || ed->line[i - 1u] == ';' || ed->line[i - 1u] == '&';
}

static int sh_dirname_for_token(const char *token, char *dir, au_usize dir_cap, char *prefix, au_usize prefix_cap, char *token_dir_prefix, au_usize token_dir_cap) {
    const char *slash = 0;
    if (dir && dir_cap) dir[0] = 0;
    if (prefix && prefix_cap) prefix[0] = 0;
    if (token_dir_prefix && token_dir_cap) token_dir_prefix[0] = 0;
    if (!dir || !prefix || !token_dir_prefix) return 0;
    if (!token) token = "";
    for (const char *p = token; *p; ++p) if (*p == '/') slash = p;
    if (!slash) {
        return sh_copy(dir, dir_cap, ".") && sh_copy(prefix, prefix_cap, token);
    }
    au_usize dir_len = (au_usize)(slash - token);
    int ok = dir_len == 0 ? sh_copy(dir, dir_cap, "/") : sh_copy_n(dir, dir_cap, token, dir_len);
    ok = ok && sh_copy(prefix, prefix_cap, slash + 1);
    ok = ok && sh_copy_n(token_dir_prefix, token_dir_cap, token, (au_usize)(slash + 1 - token));
    return ok;
}

static void sh_complete_from_dir(sh_completion_t *items, unsigned int *count, const char *dir, const char *prefix) {
    au_i64 fd = au_open2(dir, RABBITBONE_O_DIRECTORY);
    if (fd < 0) return;
    for (au_u64 i = 0; *count < SH_COMPLETION_MAX; ++i) {
        au_dirent_t de;
        au_memset(&de, 0, sizeof(de));
        au_i64 got = au_readdir(fd, i, &de);
        if (got <= 0) break;
        if (de.name[0] == '.' && (!prefix || prefix[0] != '.')) continue;
        if (!sh_match_prefix(de.name, prefix ? prefix : "")) continue;
        sh_completion_add(items, count, de.name, de.type);
    }
    (void)au_close(fd);
}

static void sh_complete_commands(sh_completion_t *items, unsigned int *count, const char *prefix) {
    for (unsigned int i = 0; i < sizeof(sh_builtins) / sizeof(sh_builtins[0]) && *count < SH_COMPLETION_MAX; ++i) {
        if (sh_match_prefix(sh_builtins[i].name, prefix ? prefix : "")) sh_completion_add(items, count, sh_builtins[i].name, SH_NODE_FILE);
    }

    const char *path = sh_path_value();
    while (*path && *count < SH_COMPLETION_MAX) {
        const char *start = path;
        while (*path && *path != ':') ++path;
        au_usize len = (au_usize)(path - start);
        if (len > 0u) {
            char dir[SH_PATH_MAX];
            if (sh_copy_n(dir, sizeof(dir), start, len)) {
                au_i64 fd = au_open2(dir, RABBITBONE_O_DIRECTORY);
                if (fd >= 0) {
                    for (au_u64 i = 0; *count < SH_COMPLETION_MAX; ++i) {
                        au_dirent_t de;
                        au_memset(&de, 0, sizeof(de));
                        au_i64 got = au_readdir(fd, i, &de);
                        if (got <= 0) break;
                        if (!sh_match_prefix(de.name, prefix ? prefix : "")) continue;
                        char full[SH_PATH_MAX];
                        if (!sh_join_path(full, sizeof(full), dir, au_strlen(dir), de.name)) continue;
                        if (!sh_path_is_elf(full)) continue;
                        sh_completion_add(items, count, de.name, SH_NODE_FILE);
                    }
                    (void)au_close(fd);
                }
            }
        }
        if (*path == ':') ++path;
    }
}

static void sh_editor_print_completions(sh_line_editor_t *ed, const sh_completion_t *items, unsigned int count, const char *token_dir_prefix) {
    if (!ed || !items || count == 0) return;
    sh_editor_move_end(ed);
    sh_puts("\n");
    for (unsigned int i = 0; i < count; ++i) {
        if (token_dir_prefix && token_dir_prefix[0]) sh_puts(token_dir_prefix);
        sh_puts(items[i].text);
        if (items[i].type == SH_NODE_DIR) sh_puts("/");
        sh_puts(i + 1u == count ? "\n" : "  ");
    }
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) ed->base_row = ti.cursor_row;
    else ed->base_row = 0;
    ed->old_rows_used = 0;
    sh_editor_refresh(ed);
}

static SH_NOINLINE void sh_editor_complete(sh_line_editor_t *ed) {
    if (!ed || !ed->line) return;
    unsigned int token_start = sh_token_start_for_completion(ed);
    au_usize token_len = ed->cursor - token_start;
    char token[SH_LINE_MAX];
    sh_copy_n(token, sizeof(token), ed->line + token_start, token_len);

    sh_completion_t items[SH_COMPLETION_MAX];
    au_memset(items, 0, sizeof(items));
    unsigned int count = 0;
    char dir[SH_PATH_MAX];
    char name_prefix[SH_COMPLETION_NAME_MAX];
    char token_dir_prefix[SH_PATH_MAX];
    au_memset(dir, 0, sizeof(dir));
    au_memset(name_prefix, 0, sizeof(name_prefix));
    au_memset(token_dir_prefix, 0, sizeof(token_dir_prefix));

    int command_position = sh_completion_is_command_position(ed, token_start);
    if (command_position && !sh_strchr(token, '/')) {
        sh_copy(name_prefix, sizeof(name_prefix), token);
        sh_complete_commands(items, &count, name_prefix);
    } else {
        if (!sh_dirname_for_token(token, dir, sizeof(dir), name_prefix, sizeof(name_prefix), token_dir_prefix, sizeof(token_dir_prefix))) return;
        sh_complete_from_dir(items, &count, dir, name_prefix);
    }

    if (count == 0) return;

    au_usize common = au_strlen(items[0].text);
    for (unsigned int i = 1; i < count; ++i) common = sh_min_usize(common, sh_common_prefix_len(items[0].text, items[i].text));
    au_usize prefix_len = au_strlen(name_prefix);
    if (common > prefix_len) {
        sh_editor_insert_text(ed, items[0].text + prefix_len, common - prefix_len);
        return;
    }

    if (count == 1) {
        const char *suffix = items[0].text + prefix_len;
        sh_editor_insert_text(ed, suffix, au_strlen(suffix));
        if (items[0].type == SH_NODE_DIR) sh_editor_insert_text(ed, "/", 1u);
        else sh_editor_insert_text(ed, " ", 1u);
        return;
    }

    sh_editor_print_completions(ed, items, count, token_dir_prefix);
}

static void sh_editor_ctrl_c(sh_line_editor_t *ed) {
    if (!ed) return;
    ed->cursor = ed->len;
    sh_editor_place_cursor(ed);
    sh_puts("^C\n");
    ed->line[0] = 0;
    ed->len = 0;
    ed->cursor = 0;
}

static void sh_editor_ctrl_l(sh_line_editor_t *ed) {
    if (!ed) return;
    (void)au_tty_clear();
    ed->base_row = 0;
    ed->old_rows_used = 0;
    sh_editor_refresh(ed);
}

static SH_NOINLINE int sh_read_line(char *line, au_usize cap) {
    if (!line || cap < 2u) return -1;
    line[0] = 0;
    sh_line_editor_t ed;
    au_memset(&ed, 0, sizeof(ed));
    ed.line = line;
    ed.cap = cap;
    ed.history_pos = sh_history_count;
    sh_make_prompt(ed.prompt, sizeof(ed.prompt));
    au_ttyinfo_t ti;
    if (au_tty_getinfo(&ti) == 0) ed.base_row = ti.cursor_row;
    else ed.base_row = 0;
    sh_editor_refresh(&ed);
    for (;;) {
        au_key_event_t ev;
        if (au_tty_readkey(&ev, RABBITBONE_TTY_READ_NONBLOCK) != 0) return -1;
        if (ev.code == RABBITBONE_KEY_NONE) {
            (void)au_sleep(1);
            continue;
        }
        if (ev.code == RABBITBONE_KEY_PAGEUP) { (void)au_tty_scroll((int)sh_scroll_page()); continue; }
        if (ev.code == RABBITBONE_KEY_PAGEDOWN) { (void)au_tty_scroll(-(int)sh_scroll_page()); continue; }
        if (ev.code == RABBITBONE_KEY_UP) { sh_editor_history_up(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_DOWN) { sh_editor_history_down(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_LEFT) { sh_editor_move_left(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_RIGHT) { sh_editor_move_right(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_HOME) { sh_editor_move_home(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_END) { sh_editor_move_end(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_DELETE) { sh_editor_delete(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_TAB || ev.ch == '\t') { sh_editor_complete(&ed); continue; }
        if (ev.ch == 3u) { sh_editor_ctrl_c(&ed); return 0; }
        if (ev.ch == 4u) {
            if (ed.len == 0) { sh_puts("exit\n"); return -2; }
            sh_editor_delete(&ed);
            continue;
        }
        if (ev.ch == 12u) { sh_editor_ctrl_l(&ed); continue; }
        if (ev.code == RABBITBONE_KEY_ENTER || ev.ch == '\n') {
            ed.line[ed.len] = 0;
            ed.cursor = ed.len;
            sh_editor_place_cursor(&ed);
            sh_puts("\n");
            sh_history_add(ed.line);
            return (int)ed.len;
        }
        if (ev.code == RABBITBONE_KEY_BACKSPACE || ev.ch == '\b') { sh_editor_backspace(&ed); continue; }
        if (ev.ch >= 32u && ev.ch <= 126u) sh_editor_insert(&ed, (char)ev.ch);
    }
}
