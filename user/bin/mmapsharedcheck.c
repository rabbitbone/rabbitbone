#include <rabbitbone_sys.h>

#define PAGE 4096ul
#define SHARED_FLAGS (RABBITBONE_MAP_ANON | RABBITBONE_MAP_SHARED)
#define PRIVATE_FLAGS (RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE)
#define PATH "/tmp/mmapsharedcheck.dat"

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

static int current_mapped_pages(unsigned long long *out) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (!out) return 1;
    if (au_procinfo((unsigned int)au_getpid(), &info) != 0) return 2;
    *out = info.mapped_pages;
    return 0;
}

static int check_materialized_shared_fork(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE * 2u, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, SHARED_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 10;
    p[0] = 0x11u;
    p[PAGE] = 0x22u;
    au_i64 child = au_fork();
    if (child < 0) return 11;
    if (child == 0) {
        if (p[0] != 0x11u || p[PAGE] != 0x22u) return 12;
        p[0] = 0x33u;
        p[PAGE] = 0x44u;
        return 13;
    }
    int wr = wait_exit((unsigned int)child, 2u, 13, 0, 0);
    if (wr != 0) return 20 + wr;
    if (p[0] != 0x33u || p[PAGE] != 0x44u) return 27;
    if (munmap(p, PAGE * 2u) != 0) return 28;
    return 0;
}

static int check_unmaterialized_shared_fork(void) {
    unsigned long long before = 0, after_map = 0;
    if (current_mapped_pages(&before) != 0) return 40;
    unsigned char *p = (unsigned char *)mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, SHARED_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 41;
    if (current_mapped_pages(&after_map) != 0) return 42;
    if (after_map != before) return 43;
    au_i64 child = au_fork();
    if (child < 0) return 44;
    if (child == 0) {
        if (p[123] != 0) return 45;
        p[123] = 0x5au;
        return 46;
    }
    int wr = wait_exit((unsigned int)child, 2u, 46, 0, 0);
    if (wr != 0) return 50 + wr;
    if (p[123] != 0x5au) return 57;
    if (munmap(p, PAGE) != 0) return 58;
    return 0;
}

static int check_parent_write_seen_by_child(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, SHARED_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 70;
    p[7] = 1u;
    au_i64 child = au_fork();
    if (child < 0) return 71;
    if (child == 0) {
        if (p[7] != 2u) return 72;
        p[8] = 3u;
        return 73;
    }
    p[7] = 2u;
    int wr = wait_exit((unsigned int)child, 2u, 73, 0, 0);
    if (wr != 0) return 80 + wr;
    if (p[7] != 2u || p[8] != 3u) return 87;
    if (munmap(p, PAGE) != 0) return 88;
    return 0;
}

static int check_child_unmap_and_exec_release(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, SHARED_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 100;
    p[0] = 0xa1u;
    au_i64 child = au_fork();
    if (child < 0) return 101;
    if (child == 0) {
        if (munmap(p, PAGE) != 0) return 102;
        return 103;
    }
    int wr = wait_exit((unsigned int)child, 2u, 103, 0, 0);
    if (wr != 0) return 110 + wr;
    if (p[0] != 0xa1u) return 117;
    p[0] = 0xa2u;
    child = au_fork();
    if (child < 0) return 118;
    if (child == 0) {
        const char *args[] = { "/bin/hello" };
        au_i64 r = au_execv("/bin/hello", 1, args);
        return r < 0 ? 119 : 120;
    }
    wr = wait_exit((unsigned int)child, 2u, 7, 0, 0);
    if (wr != 0) return 130 + wr;
    if (p[0] != 0xa2u) return 137;
    p[1] = 0xa3u;
    if (p[1] != 0xa3u) return 138;
    if (munmap(p, PAGE) != 0) return 139;
    return 0;
}

static int check_rejects(void) {
    if (mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_SHARED, -1, 0) != (void *)-1) return 150;
    if (mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, RABBITBONE_MAP_SHARED, -1, 0) != (void *)-1) return 151;
    if (mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, SHARED_FLAGS, 0, 0) != (void *)-1) return 152;
    (void)au_unlink(PATH);
    if (au_create(PATH, "abc", 3) < 0) return 153;
    au_i64 fd = au_open2(PATH, RABBITBONE_O_RDONLY);
    if (fd < 0) return 154;
    if (mmap(0, PAGE, RABBITBONE_PROT_READ, RABBITBONE_MAP_SHARED, fd, 0) != (void *)-1) return 155;
    if (mmap(0, PAGE, RABBITBONE_PROT_READ, RABBITBONE_MAP_ANON | RABBITBONE_MAP_SHARED, -1, PAGE) != (void *)-1) return 156;
    if (au_close(fd) != 0) return 157;
    (void)au_unlink(PATH);
    return 0;
}

static int check_private_still_cow(void) {
    unsigned char *p = (unsigned char *)mmap(0, PAGE, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, PRIVATE_FLAGS, -1, 0);
    if (p == (void *)-1 || !p) return 170;
    p[0] = 1u;
    au_i64 child = au_fork();
    if (child < 0) return 171;
    if (child == 0) {
        p[0] = 2u;
        return p[0] == 2u ? 172 : 173;
    }
    int wr = wait_exit((unsigned int)child, 2u, 172, 0, 0);
    if (wr != 0) return 180 + wr;
    if (p[0] != 1u) return 187;
    if (munmap(p, PAGE) != 0) return 188;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int r = check_materialized_shared_fork();
    if (r) return r;
    r = check_unmaterialized_shared_fork();
    if (r) return r;
    r = check_parent_write_seen_by_child();
    if (r) return r;
    r = check_child_unmap_and_exec_release();
    if (r) return r;
    r = check_rejects();
    if (r) return r;
    r = check_private_still_cow();
    if (r) return r;
    return 0;
}
