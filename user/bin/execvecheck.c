#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *bad_args[] = { "/bin/exectarget", "env" };
    const char *bad_env[] = { "RABBITBONE_EXEC_PHASE=10" };
    au_i64 bad = au_execve("/bin/does-not-exist", 2, bad_args, 1, bad_env);
    if (bad >= 0) return 30;
    const char *args[] = { "/bin/exectarget", "env", "alpha", "beta" };
    const char *env[] = { "RABBITBONE_EXEC_PHASE=10", "EXECVE=ok" };
    au_i64 r = au_execve("/bin/exectarget", 4, args, 2, env);
    const char msg[] = "execvecheck: execve returned unexpectedly\n";
    au_write_console(msg, sizeof(msg) - 1);
    return r < 0 ? 31 : 32;
}
