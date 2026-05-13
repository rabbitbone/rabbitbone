#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *args[] = { "/disk0/aurora-shebang-exec.sh", "from-exec" };
    au_i64 r = au_execv("/disk0/aurora-shebang-exec.sh", 2, args);
    const char msg[] = "shebangexec: execv returned unexpectedly\n";
    au_write_console(msg, sizeof(msg) - 1);
    return r < 0 ? 70 : 71;
}
