#include <aurora_sys.h>

static void u64_to_dec(au_u64 v, char *out, au_usize cap) {
    char tmp[24];
    au_usize n = 0;
    if (!out || cap == 0) return;
    if (v == 0) {
        if (cap > 1) { out[0] = '0'; out[1] = 0; }
        return;
    }
    while (v && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    au_usize i = 0;
    while (n && i + 1 < cap) out[i++] = tmp[--n];
    out[i] = 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_i64 fd = au_open("/disk0/hello.txt");
    if (fd <= 0) return 30;
    char prefix[7];
    au_memset(prefix, 0, sizeof(prefix));
    if (au_read(fd, prefix, 6) != 6) return 31;
    if (prefix[0] != 'H' || prefix[1] != 'e') return 32;
    char fd_text[24];
    u64_to_dec((au_u64)fd, fd_text, sizeof(fd_text));
    const char *args[] = { "/bin/execfdchild", fd_text };
    au_i64 r = au_execv("/bin/execfdchild", 2, args);
    au_close(fd);
    return r < 0 ? 33 : 34;
}
