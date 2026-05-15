#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    if (argc >= 2 && argv && argv[1] && au_strcmp(argv[1], "shebang") == 0) {
        const char *script = (argc >= 3 && argv[2] && argv[2][0]) ? argv[2] : "/tmp/ktest-sb3";
        const char *args[] = { script, "from-exec" };
        au_i64 r = au_execv(script, 2, args);
        const char msg[] = "execcheck: shebang execv returned unexpectedly\n";
        au_write_console(msg, sizeof(msg) - 1);
        return r < 0 ? 92 : 93;
    }
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
