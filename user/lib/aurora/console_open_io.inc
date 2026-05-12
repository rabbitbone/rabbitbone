au_i64 au_write_console(const char *s, au_usize n) {
    au_result_t r = au_syscall3(AU_SYS_WRITE_CONSOLE, (au_u64)s, n, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_open(const char *path) {
    return au_open2(path, AURORA_O_RDONLY);
}

au_i64 au_open2(const char *path, unsigned int flags) {
    au_result_t r = au_syscall3(AU_SYS_OPEN, (au_u64)path, (au_u64)flags, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_close(au_i64 h) {
    au_result_t r = au_syscall3(AU_SYS_CLOSE, (au_u64)h, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_read(au_i64 h, void *buf, au_usize n) {
    au_result_t r = au_syscall3(AU_SYS_READ, (au_u64)h, (au_u64)buf, n);
    return r.error ? r.error : r.value;
}

au_i64 au_write(au_i64 h, const void *buf, au_usize n) {
    au_result_t r = au_syscall3(AU_SYS_WRITE, (au_u64)h, (au_u64)buf, n);
    return r.error ? r.error : r.value;
}

au_i64 au_seek(au_i64 h, au_u64 off) {
    return au_seek_ex(h, (au_i64)off, AURORA_SEEK_SET);
}

au_i64 au_seek_ex(au_i64 h, au_i64 off, unsigned int whence) {
    au_result_t r = au_syscall3(AU_SYS_SEEK, (au_u64)h, (au_u64)off, (au_u64)whence);
    return r.error ? r.error : r.value;
}

au_i64 au_create(const char *path, const void *data, au_usize n) {
    au_result_t r = au_syscall3(AU_SYS_CREATE, (au_u64)path, (au_u64)data, n);
    return r.error ? r.error : r.value;
}

au_i64 au_mkdir(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_MKDIR, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_stat(const char *path, au_stat_t *out) {
    au_result_t r = au_syscall3(AU_SYS_STAT, (au_u64)path, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_lstat(const char *path, au_stat_t *out) {
    au_result_t r = au_syscall3(AU_SYS_LSTAT, (au_u64)path, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_symlink(const char *target, const char *link_path) {
    au_result_t r = au_syscall3(AU_SYS_SYMLINK, (au_u64)target, (au_u64)link_path, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_readlink(const char *path, char *out, au_usize size) {
    au_result_t r = au_syscall3(AU_SYS_READLINK, (au_u64)path, (au_u64)out, size);
    return r.error ? r.error : r.value;
}

au_i64 au_link(const char *old_path, const char *new_path) {
    au_result_t r = au_syscall3(AU_SYS_LINK, (au_u64)old_path, (au_u64)new_path, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_unlink(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_UNLINK, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

