#include <rabbitbone_sys.h>

static int same_bytes(const char *a, const char *b, au_usize n) {
    for (au_usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

static int contains(const char *buf, au_usize n, const char *needle) {
    au_usize m = au_strlen(needle);
    if (m == 0 || n < m) return 0;
    for (au_usize i = 0; i + m <= n; ++i) {
        au_usize j = 0;
        while (j < m && buf[i + j] == needle[j]) ++j;
        if (j == m) return 1;
    }
    return 0;
}

static int cat_to_stdout(const char *path) {
    au_i64 in = path && !(path[0] == '-' && path[1] == 0) ? au_open(path) : (au_i64)RABBITBONE_STDIN;
    if (in < 0) return 70;
    au_fdinfo_t out_info;
    au_memset(&out_info, 0, sizeof(out_info));
    if (au_fdinfo((au_i64)RABBITBONE_STDOUT, &out_info) != 0 || out_info.handle != RABBITBONE_STDOUT) return 71;
    char buf[128];
    for (;;) {
        au_i64 got = au_read(in, buf, sizeof(buf));
        if (got < 0) return 72;
        if (got == 0) break;
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write((au_i64)RABBITBONE_STDOUT, buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 73;
            off += wrote;
        }
    }
    if (in > (au_i64)RABBITBONE_STDERR && au_close(in) != 0) return 74;
    return 0;
}

static int check_stdio_pipeline(void) {
    au_fdinfo_t stdio;
    au_memset(&stdio, 0, sizeof(stdio));
    if (au_fdinfo((au_i64)RABBITBONE_STDIN, &stdio) != 0 || stdio.handle != RABBITBONE_STDIN) return 40;
    if (au_fdinfo((au_i64)RABBITBONE_STDOUT, &stdio) != 0 || stdio.handle != RABBITBONE_STDOUT) return 41;
    if (au_fdinfo((au_i64)RABBITBONE_STDERR, &stdio) != 0 || stdio.handle != RABBITBONE_STDERR) return 42;

    unsigned int stdfds[2] = {0, 0};
    if (au_pipe(stdfds) != 0) return 43;
    au_i64 child = au_fork();
    if (child < 0) return 44;
    if (child == 0) {
        if (au_close((au_i64)stdfds[0]) != 0) return 45;
        if (au_dup2((au_i64)stdfds[1], (au_i64)RABBITBONE_STDOUT, 0) != (au_i64)RABBITBONE_STDOUT) return 46;
        if (au_fdctl((au_i64)stdfds[1], RABBITBONE_FDCTL_SET, RABBITBONE_FD_CLOEXEC) != RABBITBONE_FD_CLOEXEC) return 47;
        const char *args[] = { "/bin/pipecheck", "--cat", "/disk0/hello.txt" };
        au_execv("/bin/pipecheck", 3, args);
        return 48;
    }
    if (au_close((au_i64)stdfds[1]) != 0) return 49;
    char out[256];
    au_memset(out, 0, sizeof(out));
    au_usize total = 0;
    for (;;) {
        au_i64 events = au_poll((au_i64)stdfds[0], RABBITBONE_POLL_READ | RABBITBONE_POLL_HUP);
        if (events < 0) return 52;
        if ((events & RABBITBONE_POLL_READ) != 0) {
            char tmp[64];
            au_i64 got = au_read((au_i64)stdfds[0], tmp, sizeof(tmp));
            if (got < 0) return 53;
            if (got == 0 && (events & RABBITBONE_POLL_HUP) != 0) break;
            for (au_i64 i = 0; i < got && total + 1u < sizeof(out); ++i) out[total++] = tmp[i];
            continue;
        }
        if ((events & RABBITBONE_POLL_HUP) != 0) break;
        (void)au_yield();
    }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return 50;
    if (info.exit_code != 0 || info.state != 2u) return 51;
    if (total == 0) return 54;
    if (!contains(out, total, "Hello")) return 55;
    if (au_poll((au_i64)stdfds[0], RABBITBONE_POLL_READ) != RABBITBONE_POLL_HUP) return 56;
    if (au_close((au_i64)stdfds[0]) != 0) return 57;
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && argv[1] && au_strcmp(argv[1], "--cat") == 0) {
        return cat_to_stdout(argc > 2 ? argv[2] : "-");
    }
    unsigned int fds[2] = {0, 0};
    if (au_pipe(fds) != 0) return 10;
    if (fds[0] <= RABBITBONE_STDERR || fds[1] <= RABBITBONE_STDERR || fds[0] == fds[1]) return 11;
    au_pipeinfo_t pi;
    au_memset(&pi, 0, sizeof(pi));
    if (au_pipeinfo(fds[0], &pi) != 0) return 12;
    if (pi.capacity != RABBITBONE_PIPE_BUF || pi.endpoint != 1u || pi.pipe_id == 0 || pi.read_refs != 1u || pi.write_refs != 1u) return 13;
    const char msg[] = "rabbitbone-pipe-ipc";
    if (au_write((au_i64)fds[1], msg, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 14;
    if (au_read((au_i64)fds[0], (void *)0, 1u) >= 0) return 30;
    au_memset(&pi, 0, sizeof(pi));
    if (au_pipeinfo(fds[1], &pi) != 0 || pi.bytes_available != sizeof(msg) - 1u || pi.endpoint != 2u || pi.read_refs != 1u || pi.write_refs != 1u) return 15;
    char buf[64];
    au_memset(buf, 0, sizeof(buf));
    if (au_read((au_i64)fds[0], buf, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 16;
    if (!same_bytes(buf, msg, sizeof(msg) - 1u)) return 17;
    if (au_read((au_i64)fds[0], buf, 1) != 0) return 18;
    if (au_read((au_i64)fds[1], buf, 1) >= 0) return 19;
    if (au_write((au_i64)fds[0], msg, 1) >= 0) return 20;
    unsigned int hupfds[2] = {0, 0};
    if (au_pipe(hupfds) != 0) return 31;
    if (au_close((au_i64)hupfds[1]) != 0) return 32;
    au_i64 hup = au_poll((au_i64)hupfds[0], RABBITBONE_POLL_READ | RABBITBONE_POLL_HUP);
    if ((hup & RABBITBONE_POLL_HUP) == 0) return 33;
    if (au_close((au_i64)hupfds[0]) != 0) return 34;
    unsigned int leakfds[2] = {0, 0};
    for (int i = 0; i < 32; ++i) {
        if (au_pipe(leakfds) != 0) return 35;
        if (au_close((au_i64)leakfds[0]) != 0 || au_close((au_i64)leakfds[1]) != 0) return 36;
    }
    if (au_pipe(leakfds) != 0) return 38;
    if (au_close((au_i64)leakfds[0]) != 0 || au_close((au_i64)leakfds[1]) != 0) return 39;
    unsigned int a[2] = {0, 0};
    unsigned int b[2] = {0, 0};
    if (au_pipe(a) != 0 || au_pipe(b) != 0) return 55;
    if (au_dup2((au_i64)a[0], (au_i64)b[0], 0) != (au_i64)b[0]) return 56;
    au_i64 whup = au_poll((au_i64)b[1], RABBITBONE_POLL_WRITE | RABBITBONE_POLL_HUP);
    if ((whup & RABBITBONE_POLL_HUP) == 0) return 57;
    (void)au_close((au_i64)a[0]);
    (void)au_close((au_i64)a[1]);
    (void)au_close((au_i64)b[0]);
    (void)au_close((au_i64)b[1]);
    unsigned int forkfds[2] = {0, 0};
    if (au_pipe(forkfds) != 0) return 21;
    au_i64 child = au_fork();
    if (child < 0) return 22;
    if (child == 0) {
        const char child_msg[] = "fork-child-pipe";
        if (au_write((au_i64)forkfds[1], child_msg, sizeof(child_msg) - 1u) != (au_i64)(sizeof(child_msg) - 1u)) return 23;
        return 37;
    }
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait((unsigned int)child, &info) != 0) return 24;
    if (info.exit_code != 37 || info.state != 2u) return 25;
    char child_buf[64];
    au_memset(child_buf, 0, sizeof(child_buf));
    const char child_msg[] = "fork-child-pipe";
    if (au_read((au_i64)forkfds[0], child_buf, sizeof(child_msg) - 1u) != (au_i64)(sizeof(child_msg) - 1u)) return 26;
    if (!same_bytes(child_buf, child_msg, sizeof(child_msg) - 1u)) return 27;
    if (au_fdctl((au_i64)forkfds[0], RABBITBONE_FDCTL_SET, RABBITBONE_FD_CLOEXEC) != RABBITBONE_FD_CLOEXEC) return 28;
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo((au_i64)forkfds[0], &fi) != 0 || !(fi.flags & RABBITBONE_FD_CLOEXEC)) return 29;
    int pipe_stdio = check_stdio_pipeline();
    if (pipe_stdio != 0) return pipe_stdio;
    return 0;
}
