#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_i64 pid = au_getpid();
    if (pid <= 0) return 30;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_procinfo((unsigned int)pid, &info) != 0) return 31;
    if (info.pid != (unsigned int)pid) return 32;
    if (info.state != 1u) return 33;
    if (info.address_space_generation == 0 || info.address_space != 0) return 34;
    if (info.mapped_pages == 0) return 35;
    if (info.name[0] != '/') return 36;
    if (au_procinfo(0xffffffffu, &info) >= 0) return 37;
    return 0;
}
