#include <rabbitbone_sys.h>

static int same_bytes(const char *a, const char *b, au_usize n) {
    for (au_usize i = 0; i < n; ++i) if (a[i] != b[i]) return 0;
    return 1;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned int fds[2] = {0, 0};
    if (au_pipe(fds) != 0) return 10;
    const char msg[] = "fd-remap-pipe";
    if (au_write((au_i64)fds[1], msg, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 11;

    const au_i64 target = 30;
    if (au_dup2((au_i64)fds[0], target, RABBITBONE_FD_CLOEXEC) != target) return 12;
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(target, &fi) != 0) return 13;
    if (fi.handle != (unsigned int)target || !(fi.flags & RABBITBONE_FD_CLOEXEC)) return 14;

    char buf[32];
    au_memset(buf, 0, sizeof(buf));
    if (au_read(target, buf, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 15;
    if (!same_bytes(buf, msg, sizeof(msg) - 1u)) return 16;

    au_i64 file = au_open("/disk0/hello.txt");
    if (file <= 0) return 17;
    if (au_dup2(file, target, 0) != target) return 18;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(target, &fi) != 0 || fi.flags != 0 || fi.handle != (unsigned int)target) return 19;
    char head[6];
    au_memset(head, 0, sizeof(head));
    if (au_read(target, head, 5) != 5) return 20;
    if (head[0] != 'H' || head[1] != 'e') return 21;
    if (au_close(target) != 0) return 22;
    if (au_read(target, head, 1) >= 0) return 23;
    return 0;
}
