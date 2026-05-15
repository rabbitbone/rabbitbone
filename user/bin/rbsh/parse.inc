static int sh_is_builtin(const char *name);
static int sh_run_builtin(int argc, char **argv, int allow_exit);
static int sh_command_from_argv(int argc, char **argv);
static int sh_path_is_elf(const char *path);
static int sh_spawn_external_wait(sh_stage_t *stage);
static int sh_run_pipeline_text(char *segment, int background);

static int sh_redir_operator_len(const char *p) {
    if (!p || !*p) return 0;
    if (p[0] == '<') return 1;
    if (p[0] == '&' && p[1] == '>') return p[2] == '>' ? 3 : 2;
    if ((p[0] == '1' || p[0] == '2') && p[1] == '>') {
        if (p[2] == '&' && sh_is_digit(p[3])) return 4;
        return p[2] == '>' ? 3 : 2;
    }
    if (p[0] == '>') {
        if (p[1] == '&' && sh_is_digit(p[2])) return 3;
        return p[1] == '>' ? 2 : 1;
    }
    return 0;
}

static int sh_expand_dollar(const char **pp, char *out, au_usize cap, au_usize *off) {
    const char *p = *pp;
    char name[SH_VAR_NAME_MAX];
    if (!p || *p != '$') return 0;
    ++p;
    if (*p == '?' || *p == '$') {
        name[0] = *p++;
        name[1] = 0;
        if (!sh_append_str(out, cap, off, sh_var_get(name))) return 0;
        *pp = p;
        return 1;
    }
    if (*p == '{') {
        ++p;
        const char *start = p;
        while (*p && *p != '}') ++p;
        if (*p != '}') return 0;
        au_usize n = (au_usize)(p - start);
        if (n == 0 || n >= sizeof(name)) return 0;
        if (!sh_copy_n(name, sizeof(name), start, n)) return 0;
        ++p;
        if (!sh_append_str(out, cap, off, sh_var_get(name))) return 0;
        *pp = p;
        return 1;
    }
    if (sh_is_name_start(*p)) {
        const char *start = p;
        while (sh_is_name_char(*p)) ++p;
        au_usize n = (au_usize)(p - start);
        if (n >= sizeof(name)) return 0;
        if (!sh_copy_n(name, sizeof(name), start, n)) return 0;
        if (!sh_append_str(out, cap, off, sh_var_get(name))) return 0;
        *pp = p;
        return 1;
    }
    if (!sh_append_char(out, cap, off, '$')) return 0;
    *pp = p;
    return 1;
}

static int sh_read_word(const char **rp, char *out, au_usize cap) {
    const char *p = *rp;
    au_usize off = 0;
    int started = 0;
    char quote = 0;
    if (!p || !out || cap == 0) return 0;
    out[0] = 0;
    if (*p == '~' && (p[1] == 0 || p[1] == '/')) {
        const char *home = sh_var_get("HOME");
        if (!sh_append_str(out, cap, &off, home && *home ? home : "/")) return 0;
        ++p;
        started = 1;
    }
    while (*p) {
        char c = *p;
        if (!quote && (sh_is_space(c) || sh_redir_operator_len(p))) break;
        started = 1;
        if (quote == '\'') {
            if (c == '\'') { quote = 0; ++p; continue; }
            if (!sh_append_char(out, cap, &off, c)) return 0;
            ++p;
            continue;
        }
        if (quote == '"') {
            if (c == '"') { quote = 0; ++p; continue; }
            if (c == '\\') {
                if (p[1] == '"' || p[1] == '\\' || p[1] == '$') {
                    ++p;
                    if (!sh_append_char(out, cap, &off, *p ? *p++ : '\\')) return 0;
                    continue;
                }
                if (!sh_append_char(out, cap, &off, c)) return 0;
                ++p;
                continue;
            }
            if (c == '$') {
                if (!sh_expand_dollar(&p, out, cap, &off)) return 0;
                continue;
            }
            if (!sh_append_char(out, cap, &off, c)) return 0;
            ++p;
            continue;
        }
        if (c == '\'') { quote = '\''; ++p; continue; }
        if (c == '"') { quote = '"'; ++p; continue; }
        if (c == '\\') {
            ++p;
            if (!*p) { if (!sh_append_char(out, cap, &off, '\\')) return 0; break; }
            if (!sh_append_char(out, cap, &off, *p++)) return 0;
            continue;
        }
        if (c == '$') {
            if (!sh_expand_dollar(&p, out, cap, &off)) return 0;
            continue;
        }
        if (!sh_append_char(out, cap, &off, c)) return 0;
        ++p;
    }
    if (quote) return 0;
    *rp = p;
    return started;
}

static int sh_tokenize(char *text, char tokens[SH_TOKENS_MAX][SH_TOKEN_MAX], unsigned int *count) {
    const char *p = text;
    unsigned int n = 0;
    if (!text || !tokens || !count) return 0;
    while (*p) {
        while (sh_is_space(*p)) ++p;
        if (!*p) break;
        if (n >= SH_TOKENS_MAX) return 0;
        int op_len = sh_redir_operator_len(p);
        if (op_len) {
            if (!sh_copy_n(tokens[n], SH_TOKEN_MAX, p, (au_usize)op_len)) return 0;
            ++n;
            p += op_len;
            continue;
        }
        if (!sh_read_word(&p, tokens[n], SH_TOKEN_MAX)) return 0;
        ++n;
    }
    *count = n;
    return 1;
}

static int sh_need_path_token(const char *op) {
    if (!op) return 0;
    if (sh_streq(op, "2>&1") || sh_streq(op, "1>&2") || sh_streq(op, ">&2")) return 0;
    return 1;
}

static int sh_set_stage_redir(sh_stage_t *stage, const char *op, const char *path) {
    if (!stage || !op) return 0;
    if (sh_streq(op, "<")) {
        if (!path || !*path || stage->in_path[0]) return 0;
        return sh_copy(stage->in_path, sizeof(stage->in_path), path);
    }
    if (sh_streq(op, ">") || sh_streq(op, "1>")) {
        if (!path || !*path || stage->out_path[0]) return 0;
        stage->out_mode = SH_REDIR_TRUNC;
        return sh_copy(stage->out_path, sizeof(stage->out_path), path);
    }
    if (sh_streq(op, ">>") || sh_streq(op, "1>>")) {
        if (!path || !*path || stage->out_path[0]) return 0;
        stage->out_mode = SH_REDIR_APPEND;
        return sh_copy(stage->out_path, sizeof(stage->out_path), path);
    }
    if (sh_streq(op, "2>")) {
        if (!path || !*path || stage->err_path[0] || stage->err_to_out) return 0;
        stage->err_mode = SH_REDIR_TRUNC;
        return sh_copy(stage->err_path, sizeof(stage->err_path), path);
    }
    if (sh_streq(op, "2>>")) {
        if (!path || !*path || stage->err_path[0] || stage->err_to_out) return 0;
        stage->err_mode = SH_REDIR_APPEND;
        return sh_copy(stage->err_path, sizeof(stage->err_path), path);
    }
    if (sh_streq(op, "2>&1")) {
        if (stage->err_path[0]) return 0;
        stage->err_to_out = 1;
        return 1;
    }
    if (sh_streq(op, "1>&2") || sh_streq(op, ">&2")) {
        if (stage->out_path[0]) return 0;
        stage->out_to_err = 1;
        return 1;
    }
    if (sh_streq(op, "&>") || sh_streq(op, "&>>")) {
        if (!path || !*path || stage->out_path[0] || stage->err_path[0] || stage->err_to_out) return 0;
        stage->out_mode = sh_streq(op, "&>>") ? SH_REDIR_APPEND : SH_REDIR_TRUNC;
        stage->err_to_out = 1;
        return sh_copy(stage->out_path, sizeof(stage->out_path), path);
    }
    return 0;
}

static SH_NOINLINE int sh_parse_stage(char *text, sh_stage_t *stage) {
    unsigned int ntok = 0;
    char (*tokens)[SH_TOKEN_MAX] = sh_parse_tokens;
    if (!text || !stage) return 0;
    au_memset(stage, 0, sizeof(*stage));
    au_memset(sh_parse_tokens, 0, sizeof(sh_parse_tokens));
    if (!sh_tokenize(text, tokens, &ntok)) return 0;
    if (ntok == 0) return 0;
    for (unsigned int i = 0; i < ntok; ++i) {
        char *t = tokens[i];
        if (sh_redir_operator_len(t) && sh_redir_operator_len(t) == (int)au_strlen(t)) {
            const char *path = 0;
            if (sh_need_path_token(t)) {
                if (i + 1u >= ntok) return 0;
                path = tokens[++i];
            }
            if (!sh_set_stage_redir(stage, t, path)) return 0;
            continue;
        }
        char name[SH_VAR_NAME_MAX];
        const char *value = 0;
        if (stage->argc == 0 && sh_assignment_token(t, name, sizeof(name), &value)) {
            if (stage->assign_count >= SH_ASSIGN_MAX) return 0;
            if (!sh_copy(stage->assign_name[stage->assign_count], sizeof(stage->assign_name[0]), name)) return 0;
            if (!sh_copy(stage->assign_value[stage->assign_count], sizeof(stage->assign_value[0]), value)) return 0;
            ++stage->assign_count;
            continue;
        }
        if (stage->argc >= SH_ARG_MAX) return 0;
        if (!sh_copy(stage->token_storage[stage->argc], sizeof(stage->token_storage[0]), t)) return 0;
        stage->argv[stage->argc] = stage->token_storage[stage->argc];
        ++stage->argc;
    }
    if (stage->argc > 0 && !sh_is_builtin(stage->argv[0])) {
        (void)sh_resolve_program(stage->path, sizeof(stage->path), stage->argv[0]);
    }
    return stage->argc > 0 || stage->assign_count > 0 || stage->in_path[0] || stage->out_path[0] || stage->err_path[0] || stage->err_to_out;
}

static int sh_split_pipeline(char *line, char **parts, unsigned int *count) {
    unsigned int n = 0;
    char quote = 0;
    int escaped = 0;
    char *start = line;
    if (!line || !parts || !count) return 0;
    for (char *p = line; ; ++p) {
        char c = *p;
        if (escaped) { escaped = 0; if (c == 0) break; continue; }
        if (c == '\\' && quote != '\'') { escaped = 1; continue; }
        if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) { quote = quote ? 0 : c; continue; }
        if ((c == '|' && p[1] != '|' && quote == 0) || c == 0) {
            if (n >= SH_STAGES_MAX) return 0;
            parts[n++] = start;
            if (c == 0) break;
            *p = 0;
            start = p + 1;
        }
        if (c == 0) break;
    }
    *count = n;
    return quote == 0;
}

static void sh_trim_segment(char **start, char **end) {
    if (!start || !end || !*start || !*end) return;
    while (*start < *end && sh_is_space(**start)) ++*start;
    while (*end > *start && sh_is_space((*end)[-1])) --*end;
    **end = 0;
}

static int sh_next_segment(char *line, au_usize *pos, char **segment, unsigned int *op_after, int *background) {
    if (!line || !pos || !segment || !op_after || !background) return -1;
    *segment = 0;
    *op_after = SH_OP_SEQ;
    *background = 0;

    au_usize i = *pos;
    for (;;) {
        while (line[i] && sh_is_space(line[i])) ++i;
        if (line[i] == ';') { ++i; continue; }
        if (line[i] == '&' && line[i + 1u] != '&' && line[i + 1u] != '>') { ++i; continue; }
        break;
    }
    if (!line[i] || line[i] == '#') { *pos = i; return 0; }

    char *start = line + i;
    char *end = start;
    char quote = 0;
    int escaped = 0;
    int word_started = 0;

    for (; line[i]; ++i) {
        char c = line[i];
        if (escaped) {
            escaped = 0;
            word_started = 1;
            end = line + i + 1u;
            continue;
        }
        if (c == '\\' && quote != '\'') {
            escaped = 1;
            word_started = 1;
            end = line + i + 1u;
            continue;
        }
        if ((c == '\'' || c == '"') && (quote == 0 || quote == c)) {
            quote = quote ? 0 : c;
            word_started = 1;
            end = line + i + 1u;
            continue;
        }
        if (quote != 0) {
            end = line + i + 1u;
            continue;
        }

        if (c == '#' && !word_started) {
            char *seg_end = line + i;
            sh_trim_segment(&start, &seg_end);
            *pos = i;
            if (!*start) return 0;
            *segment = start;
            return 1;
        }
        if (sh_is_space(c)) {
            word_started = 0;
            continue;
        }
        if (c == ';') {
            char *seg_end = line + i;
            sh_trim_segment(&start, &seg_end);
            *pos = i + 1u;
            *op_after = SH_OP_SEQ;
            *background = 0;
            return *start ? 1 : sh_next_segment(line, pos, segment, op_after, background);
        }
        if (c == '&') {
            if (i > 0u && line[i - 1u] == '>') {
                word_started = 1;
                end = line + i + 1u;
                continue;
            }
            if (line[i + 1u] == '>') {
                word_started = 1;
                end = line + i + 1u;
                continue;
            }
            if (line[i + 1u] == '&') {
                char *seg_end = line + i;
                sh_trim_segment(&start, &seg_end);
                *pos = i + 2u;
                *op_after = SH_OP_AND;
                *background = 0;
                return *start ? 1 : -1;
            }
            char *seg_end = line + i;
            sh_trim_segment(&start, &seg_end);
            *pos = i + 1u;
            *op_after = SH_OP_SEQ;
            *background = 1;
            return *start ? 1 : sh_next_segment(line, pos, segment, op_after, background);
        }
        if (c == '|' && line[i + 1u] == '|') {
            char *seg_end = line + i;
            sh_trim_segment(&start, &seg_end);
            *pos = i + 2u;
            *op_after = SH_OP_OR;
            *background = 0;
            return *start ? 1 : -1;
        }
        word_started = 1;
        end = line + i + 1u;
    }

    if (quote || escaped) return -1;
    sh_trim_segment(&start, &end);
    *pos = i;
    if (!*start) return 0;
    *segment = start;
    return 1;
}
