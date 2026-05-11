#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_i64 fds[AURORA_PROCESS_HANDLE_CAP - 3u];
    au_memset(fds, 0, sizeof(fds));
    for (int i = 0; i < (int)AURORA_PROCESS_HANDLE_CAP - 3; ++i) {
        au_i64 h = au_open("/dev/null");
        if (h <= 0) return 10 + i;
        if (h != (au_i64)(i + 3)) return 50 + i;
        fds[i] = h;
    }
    if (fds[29] != 32 || fds[30] != 33) return 90;

    au_fdinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(32, &info) != 0 || info.handle != 32) return 91;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(33, &info) != 0 || info.handle != 33) return 92;
    if (au_close(32) != 0) return 93;
    if (au_close(33) != 0) return 94;
    if (au_dup2(fds[0], 33, AURORA_FD_CLOEXEC) != 33) return 95;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(33, &info) != 0 || info.handle != 33 || !(info.flags & AURORA_FD_CLOEXEC)) return 96;
    if (au_close(33) != 0) return 97;
    for (int i = 0; i < 29; ++i) {
        if (au_close(fds[i]) != 0) return 120 + i;
    }
    return 0;
}
