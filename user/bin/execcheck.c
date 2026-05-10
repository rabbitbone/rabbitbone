#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_i64 bad = au_exec("/bin/does-not-exist");
    if (bad >= 0) return 80;
    au_stat_t st;
    if (au_stat("/bin/hello", &st) != 0 || st.size == 0) return 81;
    const char *args[] = { "/bin/fscheck", "/disk0/hello.txt" };
    au_i64 r = au_execv("/bin/fscheck", 2, args);
    const char fail[] = "execcheck: execv returned unexpectedly\n";
    au_write_console(fail, sizeof(fail) - 1);
    return r < 0 ? 90 : 91;
}
