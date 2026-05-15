static bool copy_string_arg(u64 user_or_kernel_ptr, char *out, usize max_len) {
    if (!out || max_len == 0) return false;
    if (process_user_active()) return process_copy_string_from_user((uptr)user_or_kernel_ptr, out, max_len);
    const char *s = (const char *)(uptr)user_or_kernel_ptr;
    if (!s) return false;
    usize n = strnlen(s, max_len);
    if (n >= max_len) return false;
    memcpy(out, s, n + 1u);
    return true;
}

static bool syscall_path_chars_valid(const char *path) {
    if (!path || !path[0]) return false;
    usize comp_len = 0;
    for (usize i = 0; i < SYSCALL_PATH_MAX; ++i) {
        unsigned char c = (unsigned char)path[i];
        if (c == 0) return true;
        if (c < 0x20u || c == 0x7fu || c == '\\') return false;
        if (c == '/') {
            comp_len = 0;
        } else {
            ++comp_len;
            if (comp_len >= VFS_NAME_MAX) return false;
        }
    }
    return false;
}

static bool syscall_get_cwd(char *out, usize out_size) {
    if (!out || out_size == 0) return false;
    if (process_user_active() && process_current_cwd(out, out_size)) return true;
    usize n = strnlen(kernel_cwd, out_size);
    if (n >= out_size) return false;
    memcpy(out, kernel_cwd, n + 1u);
    return true;
}

static bool syscall_set_cwd(const char *path) {
    if (!path || !rabbitbone_rust_path_policy_check((const u8 *)path, SYSCALL_PATH_MAX)) return false;
    if (process_user_active()) return process_set_current_cwd(path);
    usize n = strnlen(path, sizeof(kernel_cwd));
    if (n >= sizeof(kernel_cwd)) return false;
    memset(kernel_cwd, 0, sizeof(kernel_cwd));
    memcpy(kernel_cwd, path, n + 1u);
    return true;
}


static bool copy_symlink_target_arg(u64 user_or_kernel_ptr, char *out) {
    if (!out) return false;
    if (!copy_string_arg(user_or_kernel_ptr, out, SYSCALL_PATH_MAX)) return false;
    if (!syscall_path_chars_valid(out)) return false;
    return strnlen(out, SYSCALL_PATH_MAX) < SYSCALL_PATH_MAX &&
           rabbitbone_rust_path_no_traversal_check((const u8 *)out, SYSCALL_PATH_MAX, true);
}

static bool copy_path_arg(u64 user_or_kernel_ptr, char *out) {
    char raw[SYSCALL_PATH_MAX];
    if (!copy_string_arg(user_or_kernel_ptr, raw, sizeof(raw))) return false;
    if (!syscall_path_chars_valid(raw)) return false;
    bool ok = false;
    if (raw[0] == '/') {
        ok = path_normalize(raw, out, SYSCALL_PATH_MAX);
    } else {
        char cwd[SYSCALL_PATH_MAX];
        if (!syscall_get_cwd(cwd, sizeof(cwd))) return false;
        ok = path_join(cwd, raw, out, SYSCALL_PATH_MAX);
    }
    if (!ok) return false;
    return rabbitbone_rust_path_policy_check((const u8 *)out, SYSCALL_PATH_MAX);
}

static bool copy_in_buf(void *dst, u64 src, usize len) {
    if (!len) return true;
    if (!dst || !src) return false;
    if (process_user_active()) return process_copy_from_user(dst, (uptr)src, len);
    memcpy(dst, (const void *)(uptr)src, len);
    return true;
}

static bool copy_out_buf(u64 dst, const void *src, usize len) {
    if (!len) return true;
    if (!dst || !src) return false;
    if (process_user_active()) return process_copy_to_user((uptr)dst, src, len);
    memcpy((void *)(uptr)dst, src, len);
    return true;
}

static bool validate_user_read_range(u64 src, usize len) {
    if (!len) return true;
    if (!src) return false;
    u64 end = 0;
    if (__builtin_add_overflow(src, (u64)len - 1u, &end)) return false;
    if (process_user_active()) return process_validate_user_range((uptr)src, len, false);
    return true;
}

static bool validate_user_write_range(u64 dst, usize len) {
    if (!len) return true;
    if (!dst) return false;
    u64 end = 0;
    if (__builtin_add_overflow(dst, (u64)len - 1u, &end)) return false;
    if (process_user_active()) return process_validate_user_range((uptr)dst, len, true);
    return true;
}

