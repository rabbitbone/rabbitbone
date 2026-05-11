#include <aurora_sys.h>

int main(void) {
    au_i64 h = au_open("/etc/motd");
    if (h <= 0) return 1;
    au_stat_t st;
    if (au_fstat(h, &st) != 0 || st.type != 1 || st.size == 0) return 2;
    au_fdinfo_t fi;
    if (au_fdinfo(h, &fi) != 0 || fi.handle != (unsigned int)h || fi.offset != 0) return 3;
    char b[8];
    if (au_read(h, b, 4) != 4) return 4;
    if (au_tell(h) != 4) return 5;
    au_i64 dup = au_dup(h);
    if (dup <= 0 || dup == h) return 6;
    if (au_tell(dup) != 4) return 7;
    if (au_seek(dup, 0) != 0) return 8;
    if (au_tell(h) != 0) return 9;
    char c = 0;
    if (au_read(h, &c, 1) != 1 || c == 0) return 16;
    if (au_tell(dup) != 1) return 17;
    if (au_close(dup) != 0) return 10;
    if (au_close(h) != 0) return 11;

    au_i64 dh = au_open("/");
    if (dh <= 0) return 12;
    au_dirent_t de;
    au_i64 got = au_readdir(dh, 0, &de);
    if (got != 1 || de.name[0] == 0) return 13;
    if (au_read(dh, b, sizeof(b)) >= 0) return 14;
    if (au_close(dh) != 0) return 15;
    return 0;
}
