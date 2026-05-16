#include <rabbitbone_sys.h>

static void cpuid(unsigned int leaf, unsigned int subleaf, unsigned int *a, unsigned int *b, unsigned int *c, unsigned int *d) {
#if defined(__x86_64__)
    unsigned int eax = leaf;
    unsigned int ecx = subleaf;
    unsigned int ebx = 0;
    unsigned int edx = 0;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx) :: "memory");
    if (a) *a = eax;
    if (b) *b = ebx;
    if (c) *c = ecx;
    if (d) *d = edx;
#else
    (void)leaf; (void)subleaf;
    if (a) *a = 0;
    if (b) *b = 0;
    if (c) *c = 0;
    if (d) *d = 0;
#endif
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    unsigned int a = 0, b = 0, c = 0, d = 0;
    cpuid(0u, 0u, &a, &b, &c, &d);
    if (a == 0u) return 11;
    if ((b == 0u && c == 0u && d == 0u)) return 12;
    cpuid(1u, 0u, &a, &b, &c, &d);
    if ((d & (1u << 25u)) == 0u) return 13;
    au_i64 pid = au_getpid();
    if (pid <= 0) return 14;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_procinfo((unsigned int)pid, &info) != 0) return 15;
    if (info.pid != (unsigned int)pid) return 16;
    return 0;
}
