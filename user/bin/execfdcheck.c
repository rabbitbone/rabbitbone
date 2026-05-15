#include <rabbitbone_sys.h>

static void u64_to_dec(au_u64 v, char *out, au_usize cap) {
    char tmp[24];
    au_usize n = 0;
    if (!out || cap == 0) return;
    if (v == 0) { if (cap > 1) { out[0] = '0'; out[1] = 0; } return; }
    while (v && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    au_usize i = 0;
    while (n && i + 1 < cap) out[i++] = tmp[--n];
    out[i] = 0;
}

int main(int argc, char **argv) {
    const int clo = (argc >= 2 && argv && argv[1] && au_strcmp(argv[1], "cloexec") == 0);
    au_i64 fd = au_open("/disk0/hello.txt");
    if (fd <= 0) return clo ? 40 : 30;
    char prefix[7];
    au_memset(prefix, 0, sizeof(prefix));
    if (au_read(fd, prefix, 6) != 6) return clo ? 41 : 31;
    if (prefix[0] != 'H' || prefix[1] != 'e') return clo ? 42 : 32;
    if (clo) {
        if (au_fdctl(fd, RABBITBONE_FDCTL_SET, RABBITBONE_FD_CLOEXEC) != RABBITBONE_FD_CLOEXEC) return 43;
        au_fdinfo_t info;
        if (au_fdinfo(fd, &info) != 0 || !(info.flags & RABBITBONE_FD_CLOEXEC)) return 44;
    }
    char fd_text[24];
    u64_to_dec((au_u64)fd, fd_text, sizeof(fd_text));
    const char *args[] = { "/bin/exectarget", clo ? "cloexec" : "inherit", fd_text };
    au_i64 r = au_execv("/bin/exectarget", 3, args);
    au_close(fd);
    return r < 0 ? (clo ? 45 : 33) : (clo ? 46 : 34);
}
