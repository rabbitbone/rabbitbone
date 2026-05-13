#include <aurora_sys.h>

#define PAGE 4096ul
#define PATH "/tmp/mmapfilecheck.dat"
#define DIRPATH "/"
#define FILE_SIZE 6000ul
#define MAP_FILE_FLAGS (AURORA_MAP_PRIVATE)

static unsigned char pattern_at(unsigned long i) {
    return (unsigned char)(((i * 37ul) + 11ul) & 0xffu);
}

static void fill_pattern(unsigned char *buf, unsigned long n) {
    for (unsigned long i = 0; i < n; ++i) buf[i] = pattern_at(i);
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

static int prepare_file(void) {
    unsigned char data[FILE_SIZE];
    fill_pattern(data, FILE_SIZE);
    (void)au_unlink(PATH);
    if (au_create(PATH, data, FILE_SIZE) < 0) return 1;
    return 0;
}

static int check_private_copy_and_close(void) {
    if (prepare_file() != 0) return 10;
    au_i64 fd = au_open2(PATH, AURORA_O_RDWR);
    if (fd < 0) return 11;
    unsigned char *p = (unsigned char *)mmap(0, PAGE * 2u, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FILE_FLAGS, fd, 0);
    if (p == (void *)-1 || !p || ((unsigned long)p & (PAGE - 1u)) != 0) return 12;
    if (p[0] != pattern_at(0) || p[PAGE - 1u] != pattern_at(PAGE - 1u)) return 13;
    if (p[PAGE] != pattern_at(PAGE) || p[FILE_SIZE - 1u] != pattern_at(FILE_SIZE - 1u)) return 14;
    if (p[FILE_SIZE] != 0 || p[(PAGE * 2u) - 1u] != 0) return 15;
    p[0] = 0xaau;
    p[PAGE + 10u] = 0xbbu;
    unsigned char byte = 0;
    if (au_seek(fd, 0) < 0 || au_read(fd, &byte, 1) != 1) return 16;
    if (byte != pattern_at(0)) return 17;
    byte = 0x5au;
    if (au_seek(fd, 100) < 0 || au_write(fd, &byte, 1) != 1) return 18;
    if (p[100] != pattern_at(100)) return 19;
    if (au_close(fd) != 0) return 20;
    if (p[0] != 0xaau || p[PAGE + 10u] != 0xbbu || p[100] != pattern_at(100)) return 21;
    if (munmap(p, PAGE * 2u) != 0) return 22;
    fd = au_open2(PATH, AURORA_O_RDONLY);
    if (fd < 0) return 23;
    if (au_seek(fd, 0) < 0 || au_read(fd, &byte, 1) != 1) return 24;
    if (byte != pattern_at(0)) return 25;
    if (au_seek(fd, 100) < 0 || au_read(fd, &byte, 1) != 1) return 26;
    if (byte != 0x5au) return 27;
    if (au_close(fd) != 0) return 28;
    return 0;
}

static int check_offset_and_partial_unmap(void) {
    if (prepare_file() != 0) return 40;
    au_i64 fd = au_open2(PATH, AURORA_O_RDONLY);
    if (fd < 0) return 41;
    unsigned char *p = (unsigned char *)mmap(0, PAGE * 2u, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FILE_FLAGS, fd, PAGE);
    if (p == (void *)-1 || !p) return 42;
    if (p[0] != pattern_at(PAGE)) return 43;
    if (p[FILE_SIZE - PAGE - 1u] != pattern_at(FILE_SIZE - 1u)) return 44;
    if (p[FILE_SIZE - PAGE] != 0 || p[(PAGE * 2u) - 1u] != 0) return 45;
    if (munmap(p, PAGE) != 0) return 46;
    au_i64 child = au_fork();
    if (child < 0) return 47;
    if (child == 0) {
        volatile unsigned char x = p[0];
        (void)x;
        return 48;
    }
    int wr = wait_exit((unsigned int)child, 3u, 0, 1, 14u);
    if (wr != 0) return 50 + wr;
    if (p[PAGE] != 0) return 57;
    if (munmap(p + PAGE, PAGE) != 0) return 58;
    if (au_close(fd) != 0) return 59;
    return 0;
}

static int check_file_mprotect_and_cow(void) {
    if (prepare_file() != 0) return 70;
    au_i64 fd = au_open2(PATH, AURORA_O_RDONLY);
    if (fd < 0) return 71;
    unsigned char *p = (unsigned char *)mmap(0, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FILE_FLAGS, fd, 0);
    if (p == (void *)-1 || !p) return 72;
    if (au_close(fd) != 0) return 73;
    au_i64 child = au_fork();
    if (child < 0) return 74;
    if (child == 0) {
        if (p[0] != pattern_at(0)) return 75;
        p[0] = 0x31u;
        p[PAGE - 1u] = 0x32u;
        if (p[0] != 0x31u || p[PAGE - 1u] != 0x32u) return 76;
        return 77;
    }
    p[0] = 0x41u;
    p[PAGE - 1u] = 0x42u;
    int wr = wait_exit((unsigned int)child, 2u, 77, 0, 0);
    if (wr != 0) return 80 + wr;
    if (p[0] != 0x41u || p[PAGE - 1u] != 0x42u) return 87;
    if (mprotect(p, PAGE, AURORA_PROT_READ) != 0) return 88;
    child = au_fork();
    if (child < 0) return 89;
    if (child == 0) {
        p[0] = 0x55u;
        return 90;
    }
    wr = wait_exit((unsigned int)child, 3u, 0, 1, 14u);
    if (wr != 0) return 91 + wr;
    if (munmap(p, PAGE) != 0) return 98;
    return 0;
}

static int check_file_exec_release(void) {
    if (prepare_file() != 0) return 110;
    au_i64 fd = au_open2(PATH, AURORA_O_RDONLY);
    if (fd < 0) return 111;
    unsigned char *p = (unsigned char *)mmap(0, PAGE, AURORA_PROT_READ | AURORA_PROT_WRITE, MAP_FILE_FLAGS, fd, 0);
    if (p == (void *)-1 || !p) return 112;
    if (au_close(fd) != 0) return 113;
    au_i64 child = au_fork();
    if (child < 0) return 114;
    if (child == 0) {
        const char *args[] = { "/bin/hello" };
        au_i64 r = au_execv("/bin/hello", 1, args);
        return r < 0 ? 115 : 116;
    }
    int wr = wait_exit((unsigned int)child, 2u, 7, 0, 0);
    if (wr != 0) return 120 + wr;
    if (p[0] != pattern_at(0)) return 127;
    if (munmap(p, PAGE) != 0) return 128;
    return 0;
}

static int check_rejects(void) {
    if (prepare_file() != 0) return 140;
    au_i64 fd = au_open2(PATH, AURORA_O_RDONLY);
    if (fd < 0) return 141;
    if (mmap(0, PAGE, AURORA_PROT_READ, AURORA_MAP_ANON | AURORA_MAP_PRIVATE, fd, 0) != (void *)-1) return 142;
    if (mmap(0, PAGE, AURORA_PROT_READ, MAP_FILE_FLAGS, -1, 0) != (void *)-1) return 143;
    if (mmap(0, PAGE, AURORA_PROT_READ, MAP_FILE_FLAGS, fd, 1) != (void *)-1) return 144;
    if (mmap(0, PAGE, AURORA_PROT_READ, MAP_FILE_FLAGS | AURORA_MAP_ANON, -1, PAGE) != (void *)-1) return 145;
    if (mmap(0, PAGE, AURORA_PROT_WRITE | AURORA_PROT_EXEC, MAP_FILE_FLAGS, fd, 0) != (void *)-1) return 146;
    au_i64 dir = au_open2(DIRPATH, AURORA_O_DIRECTORY);
    if (dir < 0) return 147;
    if (mmap(0, PAGE, AURORA_PROT_READ, MAP_FILE_FLAGS, dir, 0) != (void *)-1) return 148;
    if (au_close(dir) != 0) return 149;
    au_i64 wo = au_open2(PATH, AURORA_O_WRONLY);
    if (wo < 0) return 150;
    if (mmap(0, PAGE, AURORA_PROT_READ, MAP_FILE_FLAGS, wo, 0) != (void *)-1) return 151;
    if (au_close(wo) != 0) return 152;
    if (au_close(fd) != 0) return 153;
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    int r = check_private_copy_and_close();
    if (r) return r;
    r = check_offset_and_partial_unmap();
    if (r) return r;
    r = check_file_mprotect_and_cow();
    if (r) return r;
    r = check_file_exec_release();
    if (r) return r;
    r = check_rejects();
    if (r) return r;
    return 0;
}
