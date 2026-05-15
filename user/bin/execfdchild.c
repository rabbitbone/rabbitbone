#include <rabbitbone_sys.h>

static au_i64 parse_i64(const char *s) {
    au_i64 v = 0;
    if (!s || !*s) return -1;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;
        v = v * 10 + (*s - '0');
        ++s;
    }
    return v;
}

static int contains_rabbitbone(const char *buf, au_usize n) {
    const char needle[] = "Rabbitbone";
    for (au_usize i = 0; i + sizeof(needle) - 1 <= n; ++i) {
        au_usize j = 0;
        while (j < sizeof(needle) - 1 && buf[i + j] == needle[j]) ++j;
        if (j == sizeof(needle) - 1) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || !argv || !argv[1]) return 20;
    au_i64 fd = parse_i64(argv[1]);
    if (fd <= 0) return 21;
    au_i64 off = au_tell(fd);
    if (off < 6) return 22;
    char buf[128];
    au_memset(buf, 0, sizeof(buf));
    au_i64 got = au_read(fd, buf, sizeof(buf) - 1);
    if (got <= 0) return 23;
    if (!contains_rabbitbone(buf, (au_usize)got)) return 24;
    au_close(fd);
    return 0;
}
