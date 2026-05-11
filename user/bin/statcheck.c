#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    au_stat_t st;
    au_memset(&st, 0, sizeof(st));
    if (au_stat("/etc/motd", &st) < 0) return 30;
    if (st.type != 1 || st.size == 0) return 31;
    au_i64 t0 = au_ticks();
    if (t0 < 0) return 32;
    const char *dir = "/tmp/user-statcheck-dir";
    au_unlink(dir);
    if (au_mkdir(dir) < 0) return 33;
    au_memset(&st, 0, sizeof(st));
    if (au_stat(dir, &st) < 0) return 34;
    if (st.type != 2) return 35;
    if (au_unlink(dir) < 0) return 36;
    au_statvfs_t sv;
    au_memset(&sv, 0, sizeof(sv));
    if (au_statvfs("/disk0", &sv) < 0) return 37;
    if (sv.block_size < 1024 || sv.total_blocks == 0 || sv.free_blocks > sv.total_blocks) return 38;
    au_i64 h = au_open("/disk0/hello.txt");
    if (h < 0) return 39;
    if (au_fsync(h) < 0) return 40;
    if (au_close(h) < 0) return 41;
    if (au_sync() < 0) return 42;
    return 0;
}
