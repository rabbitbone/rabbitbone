#include <aurora_sys.h>

static int wait_for(unsigned int pid, int expected_exit) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait(pid, &info) != 0) return 1;
    if (info.pid != pid) return 2;
    if (info.state != 2u || info.faulted || info.exit_code != expected_exit) return 3;
    return 0;
}

static int check_direct_brk(void) {
    au_i64 start = au_brk(0);
    if (start <= 0) return 10;
    au_i64 old = au_sbrk(1);
    if (old != start) return 11;
    volatile unsigned char *p = (volatile unsigned char *)(unsigned long)start;
    p[0] = 0x5au;
    if (p[0] != 0x5au) return 12;
    if (au_brk((void *)(unsigned long)start) < 0) return 13;
    if (au_brk((void *)(unsigned long)((au_u64)start - 1u)) >= 0) return 14;
    if (au_brk((void *)(unsigned long)((au_u64)start + 129ull * 4096ull)) >= 0) return 15;
    if (au_brk((void *)(unsigned long)start) != start) return 16;
    return 0;
}

static int check_malloc_basic(void) {
    unsigned char *a = (unsigned char *)malloc(32);
    unsigned char *b = (unsigned char *)malloc(9000);
    if (!a || !b || a == b) return 20;
    for (unsigned i = 0; i < 32; ++i) a[i] = (unsigned char)(i + 1u);
    for (unsigned i = 0; i < 9000; ++i) b[i] = (unsigned char)(i ^ 0x5au);
    for (unsigned i = 0; i < 32; ++i) if (a[i] != (unsigned char)(i + 1u)) return 21;
    for (unsigned i = 0; i < 9000; i += 257u) if (b[i] != (unsigned char)(i ^ 0x5au)) return 22;
    free(a);
    unsigned char *c = (unsigned char *)malloc(16);
    if (!c) return 23;
    for (unsigned i = 0; i < 16; ++i) c[i] = (unsigned char)(0xa0u + i);
    unsigned char *z = (unsigned char *)calloc(64, 4);
    if (!z) return 24;
    for (unsigned i = 0; i < 256; ++i) if (z[i] != 0) return 25;
    unsigned char *d = (unsigned char *)realloc(c, 6000);
    if (!d) return 26;
    for (unsigned i = 0; i < 16; ++i) if (d[i] != (unsigned char)(0xa0u + i)) return 27;
    for (unsigned i = 16; i < 6000; ++i) d[i] = (unsigned char)(i * 3u);
    for (unsigned i = 16; i < 6000; i += 331u) if (d[i] != (unsigned char)(i * 3u)) return 28;
    free(b);
    free(z);
    free(d);
    void *too_big = malloc(1024u * 1024u);
    if (too_big) return 29;
    if (malloc((au_usize)-16) != 0) return 30;
    if (calloc(((au_usize)1 << ((sizeof(au_usize) * 8u) - 1u)), 2u) != 0) return 31;
    return 0;
}

static int check_heap_fork_cow(void) {
    unsigned char *shared = (unsigned char *)malloc(128);
    if (!shared) return 40;
    shared[0] = 7;
    shared[127] = 19;
    au_i64 child = au_fork();
    if (child < 0) return 41;
    if (child == 0) {
        if (shared[0] != 7 || shared[127] != 19) return 42;
        shared[0] = 31;
        shared[127] = 37;
        if (shared[0] != 31 || shared[127] != 37) return 43;
        return 44;
    }
    shared[0] = 11;
    shared[127] = 23;
    int wr = wait_for((unsigned int)child, 44);
    if (wr != 0) return 50 + wr;
    if (shared[0] != 11 || shared[127] != 23) return 55;
    free(shared);
    return 0;
}

static int check_heap_fork_exec_release(void) {
    unsigned char *buf = (unsigned char *)malloc(4096);
    if (!buf) return 60;
    buf[0] = 0xacu;
    buf[4095] = 0xceu;
    au_i64 child = au_fork();
    if (child < 0) return 61;
    if (child == 0) {
        const char *args[] = { "/bin/hello" };
        au_i64 r = au_execv("/bin/hello", 1, args);
        return r < 0 ? 62 : 63;
    }
    int wr = wait_for((unsigned int)child, 7);
    if (wr != 0) return 70 + wr;
    if (buf[0] != 0xacu || buf[4095] != 0xceu) return 75;
    buf[0] = 0x42u;
    if (buf[0] != 0x42u) return 76;
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int r = check_direct_brk();
    if (r) return r;
    r = check_malloc_basic();
    if (r) return r;
    r = check_heap_fork_cow();
    if (r) return r;
    r = check_heap_fork_exec_release();
    if (r) return r;
    return 0;
}
