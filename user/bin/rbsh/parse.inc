typedef struct sh_stage {
    char *argv[SH_ARG_MAX];
    unsigned int argc;
    char path[SH_PATH_MAX];
    char in_path[SH_PATH_MAX];
    char out_path[SH_PATH_MAX];
    char err_path[SH_PATH_MAX];
} sh_stage_t;

static int sh_is_builtin(const char *name);
static int sh_run_builtin(int argc, char **argv, int allow_exit);
static int sh_command_from_argv(int argc, char **argv);
static int sh_path_is_elf(const char *path);
static int sh_spawn_external_wait(sh_stage_t *stage);

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
            if (dst[0]) return 0;
            if (!sh_copy(dst, SH_PATH_MAX, tokens[++i])) return 0;
            continue;
        }
        if (t[0] == '>' && t[1]) {
            if (stage->out_path[0] || !sh_copy(stage->out_path, SH_PATH_MAX, t + 1)) return 0;
            continue;
        }
        if (t[0] == '<' && t[1]) {
            if (stage->in_path[0] || !sh_copy(stage->in_path, SH_PATH_MAX, t + 1)) return 0;
            continue;
        }
        if (t[0] == '2' && t[1] == '>' && t[2]) {
            if (stage->err_path[0] || !sh_copy(stage->err_path, SH_PATH_MAX, t + 2)) return 0;
            continue;
        }
        if (stage->argc >= SH_ARG_MAX) return 0;
        stage->argv[stage->argc++] = t;
    }
    if (stage->argc == 0) return 0;
    if (!sh_is_builtin(stage->argv[0]) && !sh_resolve_program(stage->path, sizeof(stage->path), stage->argv[0])) return 0;
    return 1;
}
