#include <aurora_sys.h>

#define PAGE 4096ul
#define MAP_FLAGS (AURORA_MAP_ANON | AURORA_MAP_PRIVATE)

static int current_mapped_pages(unsigned long long *out) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (!out) return 1;
    if (au_procinfo((unsigned int)au_getpid(), &info) != 0) return 2;
    *out = info.mapped_pages;
    return 0;
}

static int wait_exit(unsigned int pid, unsigned int state, int exit_code, int faulted, unsigned long long vector) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait(pid, &info) != 0) return 1;
    if (info.pid != pid) return 2;
    if (info.state != state) return 3;
    if ((info.faulted ? 1 : 0) != faulted) return 4;
    if (faulted && info.fault_vector != vector) return 5;
    if (!faulted && info.exit_code != exit_code) return 6;
    return 0;
}

static int check_basic_map_unmap(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE * 2u, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS, -1, 0);
    if (p == (void *)-1 || !p || ((unsigned long)p & (PAGE - 1u)) != 0) return 10;
    p[0] = 0x11u;
    p[PAGE - 1u] = 0x22u;
    p[PAGE] = 0x33u;
    p[PAGE * 2u - 1u] = 0x44u;
    if (p[0] != 0x11u || p[PAGE - 1u] != 0x22u || p[PAGE] != 0x33u || p[PAGE * 2u - 1u] != 0x44u) return 11;
    if (mmap(p, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS | AURORA_MAP_FIXED, -1, 0) != (void *)-1) return 12;
    if (munmap(p, PAGE) != 0) return 13;
    au_i64 child = au_fork();
    if (child < 0) return 14;
    if (child == 0) {
        p[0] = 0x55u;
        return 15;
    }
    int wr = wait_exit((unsigned int)child, 3u, 0, 1, 14u);
    if (wr != 0) return 20 + wr;
    if (p[PAGE] != 0x33u || p[PAGE * 2u - 1u] != 0x44u) return 27;
    if (munmap(p + PAGE, PAGE) != 0) return 28;
    if (munmap(p + PAGE, PAGE) >= 0) return 29;
    return 0;
}

static int check_mprotect_fault_and_restore(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 40;
    p[0] = 0x61u;
    if (mprotect(p, PAGE, AURORA_PROT_READ) != 0) return 41;
    au_i64 child = au_fork();
    if (child < 0) return 42;
    if (child == 0) {
        p[0] = 0x62u;
        return 43;
    }
    int wr = wait_exit((unsigned int)child, 3u, 0, 1, 14u);
    if (wr != 0) return 50 + wr;
    if (p[0] != 0x61u) return 56;
    if (mprotect(p, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE) != 0) return 57;
    p[0] = 0x63u;
    if (p[0] != 0x63u) return 58;
    if (mprotect(p, PAGE, AURORA_PROT_WRITE | AURORA_PROT_EXEC) >= 0) return 59;
    if (munmap(p, PAGE) != 0) return 60;
    return 0;
}

static int check_mmap_fork_cow(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 70;
    p[0] = 7u;
    p[PAGE - 1u] = 9u;
    au_i64 child = au_fork();
    if (child < 0) return 71;
    if (child == 0) {
        if (p[0] != 7u || p[PAGE - 1u] != 9u) return 72;
        p[0] = 31u;
        p[PAGE - 1u] = 33u;
        if (p[0] != 31u || p[PAGE - 1u] != 33u) return 73;
        return 74;
    }
    p[0] = 11u;
    p[PAGE - 1u] = 13u;
    int wr = wait_exit((unsigned int)child, 2u, 74, 0, 0);
    if (wr != 0) return 80 + wr;
    if (p[0] != 11u || p[PAGE - 1u] != 13u) return 87;
    if (munmap(p, PAGE) != 0) return 88;
    return 0;
}

static int check_exec_release(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 100;
    p[0] = 0xacu;
    au_i64 child = au_fork();
    if (child < 0) return 101;
    if (child == 0) {
        const char *args[] = { "/bin/hello" };
        au_i64 r = au_execv("/bin/hello", 1, args);
        return r < 0 ? 102 : 103;
    }
    int wr = wait_exit((unsigned int)child, 2u, 7, 0, 0);
    if (wr != 0) return 110 + wr;
    if (p[0] != 0xacu) return 117;
    p[0] = 0xadu;
    if (p[0] != 0xadu) return 118;
    if (munmap(p, PAGE) != 0) return 119;
    return 0;
}

static int check_fixed_and_arg_rejects(void) {
    unsigned char *fixed = (unsigned char *)0x0000010080020000ull;
    unsigned char *p = (unsigned char *)mmap(fixed, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS | AURORA_MAP_FIXED, -1, 0);
    if (p != fixed) return 130;
    p[0] = 0x5au;
    if (p[0] != 0x5au) return 131;
    if (mprotect(p + 1, PAGE, AURORA_PROT_READ) >= 0) return 132;
    if (munmap(p + 1, PAGE) >= 0) return 133;
    if (mmap(0, 0, AURORA_PROT_READ, MAP_FLAGS, -1, 0) != (void *)-1) return 134;
    if (mmap(0, PAGE, 0, MAP_FLAGS, -1, 0) != (void *)-1) return 135;
    if (mmap(0, PAGE, AURORA_PROT_READ, AURORA_MAP_ANON, -1, 0) != (void *)-1) return 136;
    if (munmap(p, PAGE) != 0) return 137;
    return 0;
}

static int check_demand_anon(void) {
    unsigned long long before = 0, after_map = 0, after_first = 0, after_second = 0, after_unmap = 0;
    if (current_mapped_pages(&before) != 0) return 150;
    unsigned char *p = (unsigned char *)mmap(0, PAGE * 2u, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 151;
    if (current_mapped_pages(&after_map) != 0) return 152;
    if (after_map != before) return 153;
    if (p[0] != 0) return 154;
    if (current_mapped_pages(&after_first) != 0) return 155;
    if (after_first != before + 1ull) return 156;
    p[PAGE] = 0x88u;
    if (current_mapped_pages(&after_second) != 0) return 157;
    if (after_second != before + 2ull) return 158;
    if (munmap(p, PAGE * 2u) != 0) return 159;
    if (current_mapped_pages(&after_unmap) != 0) return 160;
    if (after_unmap != before) return 161;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int r = check_demand_anon();
    if (r) return r;
    r = check_basic_map_unmap();
    if (r) return r;
    r = check_mprotect_fault_and_restore();
    if (r) return r;
    r = check_mmap_fork_cow();
    if (r) return r;
    r = check_exec_release();
    if (r) return r;
    r = check_fixed_and_arg_rejects();
    if (r) return r;
    return 0;
}
