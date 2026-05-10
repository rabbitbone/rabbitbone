#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_i64 r = au_exec("/bin/does-not-exist");
    if (r >= 0) return 10;
    au_stat_t st;
    if (au_stat("/bin/hello", &st) != 0 || st.size == 0) return 11;
    return 0;
}
