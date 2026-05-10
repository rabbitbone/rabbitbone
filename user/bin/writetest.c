#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    const char *path = "/tmp/user-writetest.txt";
    const char payload[] = "userland wrote this through Aurora syscalls";
    au_unlink(path);
    if (au_create(path, payload, sizeof(payload) - 1) < 0) return 20;
    au_i64 h = au_open(path);
    if (h < 0) return 21;
    char buf[80];
    au_memset(buf, 0, sizeof(buf));
    au_i64 got = au_read(h, buf, sizeof(buf) - 1);
    au_close(h);
    if (got != (au_i64)(sizeof(payload) - 1)) return 22;
    if (au_strcmp(buf, payload) != 0) return 23;
    const char ok[] = "writetest: ramfs create/read ok\n";
    au_write_console(ok, sizeof(ok) - 1);
    return 0;
}
