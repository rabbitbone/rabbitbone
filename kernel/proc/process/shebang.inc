typedef struct parsed_shebang {
    char interpreter[VFS_PATH_MAX];
    char optional_arg[USER_ARG_STRING_MAX];
    bool has_optional_arg;
} parsed_shebang_t;

static process_status_t exec_copy_path(char *dst, usize dst_size, const char *src) {
    if (!dst || dst_size == 0 || !src || !*src) return PROC_ERR_INVAL;
    usize len = strnlen(src, dst_size);
    if (len >= dst_size) return PROC_ERR_RANGE;
    memcpy(dst, src, len + 1u);
    return PROC_OK;
}

static process_status_t exec_copy_arg(char dst[USER_ARG_STRING_MAX], const char *src) {
    if (!dst || !src) return PROC_ERR_INVAL;
    usize len = strnlen(src, USER_ARG_STRING_MAX);
    if (len >= USER_ARG_STRING_MAX) return PROC_ERR_RANGE;
    memcpy(dst, src, len + 1u);
    return PROC_OK;
}

static void exec_plan_rebind_argv(process_exec_plan_t *plan) {
    if (!plan) return;
    for (int i = 0; i < plan->argc && i < (int)PROCESS_ARG_MAX; ++i) {
        plan->argv[i] = plan->argv_storage[i];
    }
}

static process_status_t exec_plan_init(process_exec_plan_t *plan, const char *path, int argc, const char *const *argv) {
    if (!plan || !path || !*path || argc < 0 || argc > (int)PROCESS_ARG_MAX) return PROC_ERR_INVAL;
    if (argc > 0 && !argv) return PROC_ERR_INVAL;
    memset(plan, 0, sizeof(*plan));
    process_status_t st = exec_copy_path(plan->image_path, sizeof(plan->image_path), path);
    if (st != PROC_OK) return st;
    plan->argc = argc;
    for (int i = 0; i < argc; ++i) {
        st = exec_copy_arg(plan->argv_storage[i], argv[i] ? argv[i] : "");
        if (st != PROC_OK) return st;
    }
    exec_plan_rebind_argv(plan);
    return PROC_OK;
}

static bool shebang_is_space(char c) {
    return c == ' ' || c == '\t';
}

static process_status_t shebang_copy_token(char *dst, usize dst_size, const char *src, usize len) {
    if (!dst || !dst_size || !src || len == 0) return PROC_ERR_FORMAT;
    if (len >= dst_size) return PROC_ERR_RANGE;
    memcpy(dst, src, len);
    dst[len] = 0;
    return PROC_OK;
}

static process_status_t read_shebang_for_path(const active_process_t *proc, const char *path, parsed_shebang_t *out, bool *has_shebang) {
    if (!path || !out || !has_shebang) return PROC_ERR_INVAL;
    *has_shebang = false;
    memset(out, 0, sizeof(*out));

    if (!process_cred_can_traverse_parent(proc ? &proc->cred : 0, path)) return PROC_ERR_IO;

    vfs_stat_t st;
    if (vfs_stat(path, &st) != VFS_OK || st.type != VFS_NODE_FILE) return PROC_ERR_IO;

    char buf[PROCESS_SHEBANG_LINE_MAX + 2u];
    memset(buf, 0, sizeof(buf));
    usize got = 0;
    vfs_status_t vs = vfs_read(path, 0, buf, sizeof(buf), &got);
    if (vs != VFS_OK) return PROC_ERR_IO;
    if (got < 2u || buf[0] != '#' || buf[1] != '!') return PROC_OK;

    *has_shebang = true;
    if (!process_cred_can_access(proc ? &proc->cred : 0, &st, 04u)) return PROC_ERR_IO;

    usize end = 0;
    bool terminated = false;
    while (end < got) {
        if (buf[end] == '\n') { terminated = true; break; }
        ++end;
    }
    if (!terminated && got == sizeof(buf)) return PROC_ERR_RANGE;
    if (end > 0 && buf[end - 1u] == '\r') --end;
    if (end > PROCESS_SHEBANG_LINE_MAX) return PROC_ERR_RANGE;

    usize pos = 2u;
    while (pos < end && shebang_is_space(buf[pos])) ++pos;
    if (pos >= end) return PROC_ERR_FORMAT;

    usize interp_start = pos;
    while (pos < end && !shebang_is_space(buf[pos])) ++pos;
    usize interp_len = pos - interp_start;
    if (interp_len == 0) return PROC_ERR_FORMAT;
    if (buf[interp_start] != '/') return PROC_ERR_FORMAT;

    char raw_interpreter[VFS_PATH_MAX];
    process_status_t ps = shebang_copy_token(raw_interpreter, sizeof(raw_interpreter), buf + interp_start, interp_len);
    if (ps != PROC_OK) return ps;
    if (!path_normalize(raw_interpreter, out->interpreter, sizeof(out->interpreter))) return PROC_ERR_FORMAT;
    if (!rabbitbone_rust_path_policy_check((const u8 *)out->interpreter, VFS_PATH_MAX)) return PROC_ERR_FORMAT;
    if (strnlen(out->interpreter, USER_ARG_STRING_MAX) >= USER_ARG_STRING_MAX) return PROC_ERR_RANGE;

    while (pos < end && shebang_is_space(buf[pos])) ++pos;
    if (pos < end) {
        usize arg_start = pos;
        while (pos < end && !shebang_is_space(buf[pos])) ++pos;
        usize arg_len = pos - arg_start;
        ps = shebang_copy_token(out->optional_arg, sizeof(out->optional_arg), buf + arg_start, arg_len);
        if (ps != PROC_OK) return ps;
        out->has_optional_arg = true;
        while (pos < end && shebang_is_space(buf[pos])) ++pos;
        if (pos < end) return PROC_ERR_FORMAT;
    }
    return PROC_OK;
}

static process_status_t append_exec_plan_arg(process_exec_plan_t *plan, const char *arg) {
    if (!plan || plan->argc < 0 || plan->argc >= (int)PROCESS_ARG_MAX) return PROC_ERR_RANGE;
    process_status_t st = exec_copy_arg(plan->argv_storage[plan->argc], arg);
    if (st != PROC_OK) return st;
    plan->argv[plan->argc] = plan->argv_storage[plan->argc];
    ++plan->argc;
    return PROC_OK;
}

static process_status_t rewrite_exec_plan_for_shebang(process_exec_plan_t *plan, const parsed_shebang_t *sb) {
    if (!plan || !sb || !sb->interpreter[0]) return PROC_ERR_INVAL;
    char script_path[VFS_PATH_MAX];
    process_status_t st = exec_copy_path(script_path, sizeof(script_path), plan->image_path);
    if (st != PROC_OK) return st;

    process_exec_plan_t next;
    memset(&next, 0, sizeof(next));
    st = exec_copy_path(next.image_path, sizeof(next.image_path), sb->interpreter);
    if (st != PROC_OK) return st;
    next.shebang = true;

    st = append_exec_plan_arg(&next, sb->interpreter);
    if (st != PROC_OK) return st;
    if (sb->has_optional_arg) {
        st = append_exec_plan_arg(&next, sb->optional_arg);
        if (st != PROC_OK) return st;
    }
    st = append_exec_plan_arg(&next, script_path);
    if (st != PROC_OK) return st;

    for (int i = 1; i < plan->argc; ++i) {
        st = append_exec_plan_arg(&next, plan->argv[i]);
        if (st != PROC_OK) return st;
    }

    *plan = next;
    exec_plan_rebind_argv(plan);
    return PROC_OK;
}

static process_status_t resolve_exec_plan(active_process_t *proc, const char *path, int argc, const char *const *argv, process_exec_plan_t *plan) {
    process_status_t st = exec_plan_init(plan, path, argc, argv);
    if (st != PROC_OK) return st;

    for (unsigned int depth = 0; depth <= PROCESS_SHEBANG_MAX_DEPTH; ++depth) {
        parsed_shebang_t sb;
        bool has_shebang = false;
        st = read_shebang_for_path(proc, plan->image_path, &sb, &has_shebang);
        if (st != PROC_OK) return st;
        if (!has_shebang) return PROC_OK;
        if (depth == PROCESS_SHEBANG_MAX_DEPTH) return PROC_ERR_RANGE;
        st = rewrite_exec_plan_for_shebang(plan, &sb);
        if (st != PROC_OK) return st;
    }
    return PROC_ERR_RANGE;
}
