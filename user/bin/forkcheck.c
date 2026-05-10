#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_i64 child = au_fork();
    if (child < 0) return 80;
    if (child == 0) {
        for (unsigned i = 0; i < 8; ++i) {
            if (au_yield() != 0) return 81;
        }
        return 17;
    }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return 82;
    if (info.pid != (unsigned int)child) return 83;
    if (info.state != 2u || info.exit_code != 17) return 84;
    if (info.address_space_generation == 0) return 85;
    if (au_wait((unsigned int)au_getpid(), &info) >= 0) return 86;
    return 0;
}
