#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    unsigned int fds[2] = {0, 0};
    if (au_pipe(fds) != 0) return 10;
    au_i64 r = au_poll((au_i64)fds[0], AURORA_POLL_READ | AURORA_POLL_WRITE);
    if (r != 0) return 11;
    r = au_poll((au_i64)fds[1], AURORA_POLL_READ | AURORA_POLL_WRITE);
    if ((r & AURORA_POLL_WRITE) == 0 || (r & AURORA_POLL_READ) != 0) return 12;
    const char msg[] = "ready";
    if (au_write((au_i64)fds[1], msg, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 13;
    r = au_poll((au_i64)fds[0], AURORA_POLL_READ);
    if ((r & AURORA_POLL_READ) == 0) return 14;
    char buf[8];
    au_memset(buf, 0, sizeof(buf));
    if (au_read((au_i64)fds[0], buf, sizeof(msg) - 1u) != (au_i64)(sizeof(msg) - 1u)) return 15;
    r = au_poll((au_i64)fds[0], AURORA_POLL_READ);
    if (r != 0) return 16;
    au_i64 fd = au_open("/disk0/hello.txt");
    if (fd <= 0) return 17;
    r = au_poll(fd, AURORA_POLL_READ);
    if ((r & AURORA_POLL_READ) == 0) return 18;
    if (au_poll(fd, 0) >= 0) return 19;
    unsigned int hupfds[2] = {0, 0};
    if (au_pipe(hupfds) != 0) return 20;
    if (au_close((au_i64)hupfds[1]) != 0) return 21;
    r = au_poll((au_i64)hupfds[0], AURORA_POLL_READ | AURORA_POLL_HUP);
    if ((r & AURORA_POLL_HUP) == 0) return 22;
    return 0;
}
