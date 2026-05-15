#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 pid = au_spawn("/bin/hello");
    if (pid <= 0) return 50;
    if (au_wait((unsigned int)pid, &info) != 0) return 51;
    if (info.pid != (unsigned int)pid || info.state != 2u || info.exit_code != 7) return 52;
    const char *argvv[] = { "/bin/hello" };
    if (au_spawnv("/bin/hello", 0, argvv) >= 0) return 53;
    if (au_spawnv("/bin/hello", 1, (const char *const *)0) >= 0) return 54;
    if (au_wait(0xffffffffu, &info) >= 0) return 55;
    if (au_wait(0, &info) >= 0) return 56;
    return 0;
}
