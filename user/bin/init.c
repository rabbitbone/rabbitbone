#include <aurora_sys.h>

static void put(const char *s) {
    if (s) (void)au_write((au_i64)AURORA_STDOUT, s, au_strlen(s));
}

static void put_u64(au_u64 v) {
    char tmp[24];
    char out[24];
    au_usize n = 0;
    if (v == 0) { put("0"); return; }
    while (v && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    au_usize i = 0;
    while (n && i + 1u < sizeof(out)) out[i++] = tmp[--n];
    out[i] = 0;
    put(out);
}

static void put_i64(au_i64 v) {
    if (v < 0) {
        put("-");
        au_u64 mag = (au_u64)(-(v + 1)) + 1u;
        put_u64(mag);
    } else {
        put_u64((au_u64)v);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    put("init: pid=");
    put_i64(au_getpid());
    put(" starting /disk0/bin/sh\n");

    for (;;) {
        const char *sh_argv[] = { "/disk0/bin/sh", "--login", "aurora" };
        au_i64 pid = au_spawnv("/disk0/bin/sh", 3, sh_argv);
        if (pid < 0) {
            put("init: cannot spawn /disk0/bin/sh, retrying\n");
            (void)au_sleep(100);
            continue;
        }
        put("init: /disk0/bin/sh pid=");
        put_i64(pid);
        put("\n");

        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        au_i64 waited = au_wait((unsigned int)pid, &info);
        if (waited < 0) {
            put("init: wait failed for shell pid=");
            put_i64(pid);
            put("\n");
            (void)au_sleep(25);
            continue;
        }
        put("init: /disk0/bin/sh exited pid=");
        put_u64(info.pid);
        put(" exit=");
        put_i64(info.exit_code);
        put(" state=");
        put_u64(info.state);
        if (info.faulted) put(" faulted=1");
        put(", restarting\n");
        (void)au_sleep(10);
    }
}
