#include <rabbitbone_sys.h>

static void put(const char *s) {
    if (s) (void)au_write((au_i64)RABBITBONE_STDOUT, s, au_strlen(s));
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

static void put_hex64(au_u64 v) {
    static const char hex[] = "0123456789abcdef";
    char out[19];
    out[0] = '0';
    out[1] = 'x';
    for (unsigned int i = 0; i < 16u; ++i) {
        unsigned int shift = (15u - i) * 4u;
        out[2u + i] = hex[(v >> shift) & 0xfu];
    }
    out[18] = 0;
    put(out);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    for (;;) {
        const char *sh_argv[] = { "/disk0/bin/sh", "--login", "rabbitbone" };
        au_i64 pid = au_spawnv("/disk0/bin/sh", 3, sh_argv);
        if (pid < 0) {
            put("init: shell unavailable\n");
            (void)au_sleep(100);
            continue;
        }

        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        au_i64 waited = au_wait((unsigned int)pid, &info);
        if (waited < 0) {
            put("init: wait failed\n");
            (void)au_sleep(25);
            continue;
        }
        if (info.faulted || info.exit_code != 0) {
            put("init: shell stopped exit=");
            put_i64(info.exit_code);
            if (info.faulted) {
                put(" faulted vector=");
                put_u64(info.fault_vector);
                put(" rip=");
                put_hex64(info.fault_rip);
                put(" addr=");
                put_hex64(info.fault_addr);
            }
            put("\n");
        }
        (void)au_sleep(10);
    }
}
