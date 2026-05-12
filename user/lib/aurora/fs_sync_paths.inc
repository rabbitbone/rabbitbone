au_i64 au_truncate(const char *path, au_u64 size) {
    au_result_t r = au_syscall3(AU_SYS_TRUNCATE, (au_u64)path, size, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_rename(const char *old_path, const char *new_path) {
    au_result_t r = au_syscall3(AU_SYS_RENAME, (au_u64)old_path, (au_u64)new_path, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_sync(void) {
    au_result_t r = au_syscall3(AU_SYS_SYNC, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fsync(au_i64 h) {
    au_result_t r = au_syscall3(AU_SYS_FSYNC, (au_u64)h, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fdatasync(au_i64 h) {
    au_result_t r = au_syscall3(AU_SYS_FDATASYNC, (au_u64)h, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_statvfs(const char *path, au_statvfs_t *out) {
    au_result_t r = au_syscall3(AU_SYS_STATVFS, (au_u64)path, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_install_commit(const char *staged_path, const char *final_path) {
    au_result_t r = au_syscall3(AU_SYS_INSTALL_COMMIT, (au_u64)staged_path, (au_u64)final_path, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_preallocate(const char *path, au_u64 size) {
    au_result_t r = au_syscall3(AU_SYS_PREALLOCATE, (au_u64)path, size, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_ftruncate(au_i64 h, au_u64 size) {
    au_result_t r = au_syscall3(AU_SYS_FTRUNCATE, (au_u64)h, size, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fpreallocate(au_i64 h, au_u64 size) {
    au_result_t r = au_syscall3(AU_SYS_FPREALLOCATE, (au_u64)h, size, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_chdir(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_CHDIR, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_getcwd(char *out, au_usize size) {
    au_result_t r = au_syscall3(AU_SYS_GETCWD, (au_u64)out, size, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_chmod(const char *path, unsigned int mode) {
    au_result_t r = au_syscall3(AU_SYS_CHMOD, (au_u64)(unsigned long)path, (au_u64)mode, 0);
    return au_result_code(r);
}

au_i64 au_chown(const char *path, unsigned int uid, unsigned int gid) {
    au_result_t r = au_syscall3(AU_SYS_CHOWN, (au_u64)(unsigned long)path, (au_u64)uid, (au_u64)gid);
    return au_result_code(r);
}

au_i64 au_kctl(unsigned int op, char *out, au_usize out_size, const char *arg) {
    au_result_t r = au_syscall6(AU_SYS_KCTL, (au_u64)op, (au_u64)(unsigned long)out, (au_u64)out_size, (au_u64)(unsigned long)arg, 0, 0);
    return r.error ? r.error : r.value;
}
