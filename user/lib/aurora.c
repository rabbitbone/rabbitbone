#include <aurora_sys.h>

au_i64 au_write_console(const char *s, au_usize n) {
    au_result_t r = au_syscall3(AU_SYS_WRITE_CONSOLE, (au_u64)s, n, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_open(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_OPEN, (au_u64)path, 0, 0);
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
    au_result_t r = au_syscall3(AU_SYS_SEEK, (au_u64)h, off, 0);
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

au_i64 au_unlink(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_UNLINK, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_log(const char *msg) {
    au_result_t r = au_syscall3(AU_SYS_LOG, (au_u64)msg, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_ticks(void) {
    au_result_t r = au_syscall3(AU_SYS_TICKS, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_getpid(void) {
    au_result_t r = au_syscall3(AU_SYS_GETPID, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_procinfo(unsigned int pid, au_procinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_PROCINFO, (au_u64)pid, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_usize au_strlen(const char *s) {
    au_usize n = 0;
    if (!s) return 0;
    while (s[n]) ++n;
    return n;
}

int au_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

void *au_memset(void *p, int c, au_usize n) {
    unsigned char *d = (unsigned char *)p;
    for (au_usize i = 0; i < n; ++i) d[i] = (unsigned char)c;
    return p;
}

void *au_memcpy(void *d, const void *s, au_usize n) {
    unsigned char *dd = (unsigned char *)d;
    const unsigned char *ss = (const unsigned char *)s;
    for (au_usize i = 0; i < n; ++i) dd[i] = ss[i];
    return d;
}

void *memcpy(void *d, const void *s, au_usize n) { return au_memcpy(d, s, n); }
void *memset(void *p, int c, au_usize n) { return au_memset(p, c, n); }
au_usize strlen(const char *s) { return au_strlen(s); }
int strcmp(const char *a, const char *b) { return au_strcmp(a, b); }

au_i64 au_spawn(const char *path) {
    au_result_t r = au_syscall3(AU_SYS_SPAWN, (au_u64)path, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_spawnv(const char *path, unsigned int argc, const char *const *argv) {
    au_result_t r = au_syscall3(AU_SYS_SPAWNV, (au_u64)path, (au_u64)argc, (au_u64)argv);
    return r.error ? r.error : r.value;
}

au_i64 au_wait(unsigned int pid, au_procinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_WAIT, (au_u64)pid, (au_u64)out, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_yield(void) {
    au_result_t r = au_syscall3(AU_SYS_YIELD, 0, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_sleep(au_u64 ticks) {
    au_result_t r = au_syscall3(AU_SYS_SLEEP, ticks, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_schedinfo(au_schedinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_SCHEDINFO, (au_u64)out, 0, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_preemptinfo(au_preemptinfo_t *out) {
    au_result_t r = au_syscall3(AU_SYS_PREEMPTINFO, (au_u64)out, 0, 0);
    return r.error ? r.error : r.value;
}

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
