#include <rabbitbone_sys.h>

static void fill_long_component(char *buf, au_usize n) {
    if (!n) return;
    buf[0] = '/';
    for (au_usize i = 1; i + 1 < n; ++i) buf[i] = 'a';
    buf[n - 1] = 0;
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int read_exact(au_i64 h, char *buf, au_usize n) {
    au_i64 r = au_read(h, buf, n);
    if (r < 0 || (au_usize)r != n) return 0;
    buf[n] = 0;
    return 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char long_component[80];
    fill_long_component(long_component, sizeof(long_component));
    au_i64 a = au_open("/bad\\path");
    au_i64 b = au_open("bad\\relative");
    au_i64 c = au_open(long_component);
    au_i64 d = au_create("/bad\\create", "x", 1);
    au_i64 e = au_unlink("/bad\\unlink");
    if (!(a < 0 && b < 0 && c < 0 && d < 0 && e < 0)) return 12;

    char cwd[256];
    if (au_getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/")) return 20;
    if (au_chdir("/disk0") < 0) return 21;
    if (au_getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/disk0")) return 22;

    au_statvfs_t sv;
    au_memset(&sv, 0, sizeof(sv));
    if (au_statvfs(".", &sv) < 0 || !streq(sv.fs_name, "ext4")) return 23;

    au_i64 h = au_open("hello.txt");
    if (h < 0) return 24;
    char buf[6];
    if (!read_exact(h, buf, 5) || !streq(buf, "Hello")) return 25;
    au_close(h);

    if (au_chdir("../disk0/.") < 0) return 26;
    if (au_getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/disk0")) return 27;

    const char payload[] = "cwd-relative-data";
    if (au_create("badpath-cwd.tmp", payload, sizeof(payload) - 1u) < 0) return 28;
    h = au_open("./badpath-cwd.tmp");
    if (h < 0) return 29;
    char data[sizeof(payload)];
    if (!read_exact(h, data, sizeof(payload) - 1u) || !streq(data, payload)) return 30;
    au_close(h);
    if (au_rename("badpath-cwd.tmp", "badpath-cwd.renamed") < 0) return 31;
    if (au_unlink("badpath-cwd.renamed") < 0) return 32;

    if (au_chdir("/") < 0) return 33;
    if (au_getcwd(cwd, sizeof(cwd)) < 0 || !streq(cwd, "/")) return 34;
    h = au_open("disk0/hello.txt");
    if (h < 0) return 35;
    if (!read_exact(h, buf, 5) || !streq(buf, "Hello")) return 36;
    au_close(h);
    return 0;
}
