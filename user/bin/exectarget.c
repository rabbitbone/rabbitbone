#include <aurora_sys.h>

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

static int contains_aurora(const char *buf, au_usize n) {
    const char needle[] = "AuroraOS";
    for (au_usize i = 0; i + sizeof(needle) - 1 <= n; ++i) {
        au_usize j = 0;
        while (j < sizeof(needle) - 1 && buf[i + j] == needle[j]) ++j;
        if (j == sizeof(needle) - 1) return 1;
    }
    return 0;
}

static int has_env(char **envp, const char *needle) {
    if (!envp || !needle) return 0;
    for (int i = 0; envp[i] && i < 16; ++i) {
        if (au_strcmp(envp[i], needle) == 0) return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    if (argc < 2 || !argv || !argv[1]) return 10;
    if (au_strcmp(argv[1], "env") == 0) {
        if (argc != 4) return 11;
        if (au_strcmp(argv[2], "alpha") != 0 || au_strcmp(argv[3], "beta") != 0) return 12;
        if (!has_env(envp, "AURORA_STAGE=10")) return 13;
        if (!has_env(envp, "EXECVE=ok")) return 14;
        return 0;
    }
    if (argc < 3 || !argv[2]) return 20;
    au_i64 fd = parse_i64(argv[2]);
    if (fd <= 0) return 21;
    if (au_strcmp(argv[1], "inherit") == 0) {
        au_i64 off = au_tell(fd);
        if (off < 6) return 22;
        char buf[128];
        au_memset(buf, 0, sizeof(buf));
        au_i64 got = au_read(fd, buf, sizeof(buf) - 1);
        if (got <= 0) return 23;
        if (!contains_aurora(buf, (au_usize)got)) return 24;
        au_close(fd);
        return 0;
    }
    if (au_strcmp(argv[1], "cloexec") == 0) {
        au_fdinfo_t info;
        au_i64 r = au_fdinfo(fd, &info);
        if (r >= 0) return 31;
        char c;
        r = au_read(fd, &c, 1);
        if (r >= 0) return 32;
        return 0;
    }
    return 40;
}
