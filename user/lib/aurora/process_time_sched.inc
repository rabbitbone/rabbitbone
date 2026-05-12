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
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
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

au_i64 au_spawn_wait(const char *path, au_procinfo_t *out) {
    au_i64 pid = au_spawn(path);
    if (pid < 0) return pid;
    return au_wait((unsigned int)pid, out);
}

au_i64 au_spawnv_wait(const char *path, unsigned int argc, const char *const *argv, au_procinfo_t *out) {
    au_i64 pid = au_spawnv(path, argc, argv);
    if (pid < 0) return pid;
    return au_wait((unsigned int)pid, out);
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


au_i64 au_theme(unsigned int op, unsigned int value) {
    au_result_t r = au_syscall3(AU_SYS_THEME, (au_u64)op, (au_u64)value, 0);
    return r.error ? r.error : r.value;
}

au_i64 au_cred(unsigned int op, const void *arg0, const void *arg1, au_u64 value) {
    au_result_t r = au_syscall6(AU_SYS_CRED, (au_u64)op, (au_u64)(unsigned long)arg0, (au_u64)(unsigned long)arg1, value, 0, 0);
    return au_result_code(r);
}

au_i64 au_sudo(unsigned int op, const char *password, au_u64 value) {
    au_result_t r = au_syscall3(AU_SYS_SUDO, (au_u64)op, (au_u64)(unsigned long)password, value);
    return au_result_code(r);
}
