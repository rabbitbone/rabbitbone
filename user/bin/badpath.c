#include <aurora_sys.h>

static void fill_long_component(char *buf, au_usize n) {
    if (!n) return;
    buf[0] = '/';
    for (au_usize i = 1; i + 1 < n; ++i) buf[i] = 'a';
    buf[n - 1] = 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    char long_component[80];
    fill_long_component(long_component, sizeof(long_component));
    au_i64 a = au_open("relative/path");
    au_i64 b = au_open("/bad\\path");
    au_i64 c = au_open(long_component);
    au_i64 d = au_create("relative-create", "x", 1);
    au_i64 e = au_unlink("relative-unlink");
    if (a < 0 && b < 0 && c < 0 && d < 0 && e < 0) return 0;
    return 12;
}
