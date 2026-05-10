#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    const char *bad = (const char *)0x1000ull;
    char *badw = (char *)0x2000ull;
    au_i64 a = au_write_console(bad, 4);
    au_i64 b = au_open(bad);
    au_i64 c = au_read(77, badw, 8);
    au_i64 d = au_create("/tmp/badptr-should-not-exist", bad, 4);
    if (a < 0 && b < 0 && c < 0 && d < 0) return 0;
    return 9;
}
