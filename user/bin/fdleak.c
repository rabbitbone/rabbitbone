#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_i64 fds[RABBITBONE_PROCESS_HANDLE_CAP - 3u];
    au_memset(fds, 0, sizeof(fds));
    for (int i = 0; i < (int)RABBITBONE_PROCESS_HANDLE_CAP - 3; ++i) {
        au_i64 h = au_open("/dev/null");
        if (h <= 0) return 10 + i;
        if (h != (au_i64)(i + 3)) return 50 + i;
        fds[i] = h;
    }
    const int handle_count = (int)RABBITBONE_PROCESS_HANDLE_CAP - 3;
    const int last = handle_count - 1;
    const int prev = handle_count - 2;
    const au_i64 last_handle = (au_i64)(RABBITBONE_PROCESS_HANDLE_CAP - 1u);
    const au_i64 prev_handle = (au_i64)(RABBITBONE_PROCESS_HANDLE_CAP - 2u);
    if (fds[prev] != prev_handle || fds[last] != last_handle) return 90;

    au_fdinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(prev_handle, &info) != 0 || info.handle != (unsigned int)prev_handle) return 91;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(last_handle, &info) != 0 || info.handle != (unsigned int)last_handle) return 92;
    if (au_close(prev_handle) != 0) return 93;
    if (au_close(last_handle) != 0) return 94;
    if (au_dup2(fds[0], last_handle, RABBITBONE_FD_CLOEXEC) != last_handle) return 95;
    au_memset(&info, 0, sizeof(info));
    if (au_fdinfo(last_handle, &info) != 0 || info.handle != (unsigned int)last_handle || !(info.flags & RABBITBONE_FD_CLOEXEC)) return 96;
    if (au_close(last_handle) != 0) return 97;
    for (int i = 0; i < prev; ++i) {
        if (au_close(fds[i]) != 0) return 120 + i;
    }
    return 0;
}
