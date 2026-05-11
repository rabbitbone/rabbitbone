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

    const char *p = "/tmp/fdcheck-openflags.txt";
    au_unlink(p);
    au_i64 fh = au_open2(p, AURORA_O_CREAT | AURORA_O_EXCL | AURORA_O_RDWR | AURORA_O_CLOEXEC);
    if (fh < 0) return 18;
    if (au_fdinfo(fh, &fi) != 0 || !(fi.flags & AURORA_FD_CLOEXEC) || (fi.open_flags & (AURORA_O_CREAT | AURORA_O_EXCL | AURORA_O_RDWR)) != (AURORA_O_CREAT | AURORA_O_EXCL | AURORA_O_RDWR)) return 19;
    if (au_write(fh, "123", 3) != 3) return 20;
    if (au_seek_ex(fh, -1, AURORA_SEEK_CUR) != 2) return 21;
    if (au_seek_ex(fh, 0, AURORA_SEEK_END) != 3) return 22;
    if (au_open2(p, AURORA_O_CREAT | AURORA_O_EXCL | AURORA_O_RDONLY) >= 0) return 23;
    au_i64 ro = au_open2(p, AURORA_O_RDONLY);
    if (ro < 0) return 24;
    if (au_write(ro, "x", 1) >= 0) return 25;
    if (au_close(ro) != 0) return 26;
    au_i64 ah = au_open2(p, AURORA_O_WRONLY | AURORA_O_APPEND);
    if (ah < 0) return 27;
    if (au_write(ah, "A", 1) != 1) return 28;
    if (au_close(ah) != 0) return 29;
    if (au_seek(fh, 0) != 0) return 30;
    char obuf[8];
    au_memset(obuf, 0, sizeof(obuf));
    if (au_read(fh, obuf, 4) != 4 || au_strcmp(obuf, "123A") != 0) return 31;
    if (au_ftruncate(fh, 2) != 0) return 32;
    if (au_fstat(fh, &st) != 0 || st.size != 2) return 33;
    if (au_close(fh) != 0) return 34;
    au_i64 th = au_open2(p, AURORA_O_TRUNC | AURORA_O_RDWR);
    if (th < 0) return 35;
    if (au_fstat(th, &st) != 0 || st.size != 0) return 36;
    if (au_close(th) != 0) return 37;
    if (au_open2(p, AURORA_O_DIRECTORY) >= 0) return 38;
    if (au_unlink(p) != 0) return 39;
    dh = au_open2("/", AURORA_O_DIRECTORY);
    if (dh <= 0) return 40;
    if (au_close(dh) != 0) return 41;

    const char *pre = "/disk0/fdcheck-prealloc.bin";
    au_unlink(pre);
    au_i64 ph = au_open2(pre, AURORA_O_CREAT | AURORA_O_RDWR);
    if (ph < 0) return 42;
    if (au_fpreallocate(ph, 8192) != 0) return 43;
    if (au_fstat(ph, &st) != 0 || st.size != 8192) return 44;
    if (au_close(ph) != 0) return 45;
    if (au_unlink(pre) != 0) return 46;
    return 0;
}
