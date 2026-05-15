#include <rabbitbone_sys.h>

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
    const char *path = "/disk0/hello.txt";
    if (argc > 1 && argv && argv[1]) path = argv[1];
    char buf[128];
    au_memset(buf, 0, sizeof(buf));
    au_i64 h = au_open(path);
    if (h < 0) {
        const char fail[] = "fscheck: open failed\n";
        au_write_console(fail, sizeof(fail) - 1);
        return 10;
    }
    au_i64 got = au_read(h, buf, sizeof(buf) - 1);
    au_close(h);
    if (got <= 0 || !contains_rabbitbone(buf, (au_usize)got)) {
        const char fail[] = "fscheck: read/contents failed\n";
        au_write_console(fail, sizeof(fail) - 1);
        return 11;
    }
    const char ok[] = "fscheck: ext4 read through syscall ok\n";
    au_write_console(ok, sizeof(ok) - 1);
    return 0;
}
