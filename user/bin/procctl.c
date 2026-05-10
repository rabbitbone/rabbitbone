#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));

    au_i64 p1 = au_spawn("/bin/hello");
    if (p1 <= 0) return 90;
    if (au_wait((unsigned int)p1, &info) != 0) return 91;
    if (info.pid != (unsigned int)p1 || info.state != 2u || info.exit_code != 7) return 92;

    const char *args[] = { "/bin/fscheck", "/disk0/hello.txt" };
    au_i64 p2 = au_spawnv("/bin/fscheck", 2, args);
    if (p2 <= 0 || p2 == p1) return 93;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)p2, &info) != 0) return 94;
    if (info.pid != (unsigned int)p2 || info.state != 2u || info.exit_code != 0) return 95;

    if (au_spawnv("/bin/hello", 0, args) >= 0) return 96;
    if (au_spawnv("/bin/hello", 1, (const char *const *)0) >= 0) return 97;
    if (au_wait(0xffffffffu, &info) >= 0) return 98;
    return 0;
}
