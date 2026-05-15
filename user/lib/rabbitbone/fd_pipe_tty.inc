au_i64 au_dup(au_i64 h) {
    au_result_t r = au_syscall3(AU_SYS_DUP, (au_u64)h, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tell(au_i64 h) {
    au_result_t r = au_syscall3(AU_SYS_TELL, (au_u64)h, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fstat(au_i64 h, au_stat_t *out) {
    au_result_t r = au_syscall3(AU_SYS_FSTAT, (au_u64)h, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fdinfo(au_i64 h, au_fdinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_FDINFO, (au_u64)h, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_readdir(au_i64 h, au_u64 index, au_dirent_t *out) {
    au_result_t r = au_syscall3(AU_SYS_READDIR, (au_u64)h, index, (au_u64)out);
    return r.error ? r.error : r.value;
}

au_i64 au_fork(void) {
    au_result_t r = au_syscall3(AU_SYS_FORK, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_exec(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_EXEC, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_execv(const char *path, unsigned int argc, const char *const *argv) {
    au_result_t r = au_syscall3(AU_SYS_EXECV, (au_u64)path, (au_u64)argc, (au_u64)argv);
    return r.error ? r.error : r.value;
}

au_i64 au_execve(const char *path, unsigned int argc, const char *const *argv, unsigned int envc, const char *const *envp) {
    au_result_t r = au_syscall6(AU_SYS_EXECVE, (au_u64)path, (au_u64)argc, (au_u64)argv, (au_u64)envc, (au_u64)envp, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_fdctl(au_i64 h, unsigned int op, unsigned int flags) {
    au_result_t r = au_syscall3(AU_SYS_FDCTL, (au_u64)h, (au_u64)op, (au_u64)flags);
    return r.error ? r.error : r.value;
}


au_i64 au_pipe(unsigned int out_handles[2]) {
    au_result_t r = au_syscall3(AU_SYS_PIPE, (au_u64)out_handles, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_pipeinfo(au_i64 h, au_pipeinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_PIPEINFO, (au_u64)h, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_dup2(au_i64 src, au_i64 target, unsigned int flags) {
    au_result_t r = au_syscall3(AU_SYS_DUP2, (au_u64)src, (au_u64)target, (au_u64)flags);
    return r.error ? r.error : r.value;
}

au_i64 au_poll(au_i64 h, unsigned int events) {
    au_result_t r = au_syscall3(AU_SYS_POLL, (au_u64)h, (au_u64)events, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_getinfo(au_ttyinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_TTY_GETINFO, (au_u64)out, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_setmode(unsigned int mode) {
    au_result_t r = au_syscall3(AU_SYS_TTY_SETMODE, (au_u64)mode, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_readkey(au_key_event_t *out, unsigned int flags) {
    au_result_t r = au_syscall3(AU_SYS_TTY_READKEY, (au_u64)out, (au_u64)flags, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_scroll(int lines) {
    au_result_t r = au_syscall3(AU_SYS_TTY_SCROLL, (au_u64)(au_i64)lines, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_setcursor(unsigned int row, unsigned int col) {
    au_result_t r = au_syscall3(AU_SYS_TTY_SETCURSOR, (au_u64)row, (au_u64)col, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_clearline(void) {
    au_result_t r = au_syscall3(AU_SYS_TTY_CLEAR_LINE, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_clear(void) {
    au_result_t r = au_syscall3(AU_SYS_TTY_CLEAR, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_tty_cursor_visible(unsigned int visible) {
    au_result_t r = au_syscall3(AU_SYS_TTY_CURSOR_VISIBLE, visible ? 1u : 0u, 0, 0);
    return r.error ? r.error : r.value;
}

