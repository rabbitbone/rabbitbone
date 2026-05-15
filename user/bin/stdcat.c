#include <rabbitbone_sys.h>

static int startswith(const char *s, const char *prefix) {
    if (!s || !prefix) return 0;
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static int waitable_input(au_i64 in) {
    au_fdinfo_t fi;
    au_memset(&fi, 0, sizeof(fi));
    if (au_fdinfo(in, &fi) != 0) return 0;
    return startswith(fi.path, "pipe:") || startswith(fi.path, "console:");
}

static int copy_fd_to_stdout(au_i64 in) {
    char buf[160];
    int waitable = waitable_input(in);
    for (;;) {
        au_i64 got = au_read(in, buf, sizeof(buf));
        if (got < 0) return 20;
        if (got == 0) {
            if (!waitable) return 0;
            au_i64 ev = au_poll(in, RABBITBONE_POLL_READ | RABBITBONE_POLL_HUP);
            if (ev < 0) return 20;
            if ((ev & RABBITBONE_POLL_HUP) != 0 && (ev & RABBITBONE_POLL_READ) == 0) return 0;
            if ((ev & RABBITBONE_POLL_READ) == 0) {
                (void)au_sleep(1);
            }
            continue;
        }
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write((au_i64)RABBITBONE_STDOUT, buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 21;
            off += wrote;
        }
    }
}

int main(int argc, char **argv) {
    if (argc <= 1) return copy_fd_to_stdout((au_i64)RABBITBONE_STDIN);
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) return 10;
        if (argv[i][0] == '-' && argv[i][1] == 0) {
            rc = copy_fd_to_stdout((au_i64)RABBITBONE_STDIN);
        } else {
            au_i64 fd = au_open(argv[i]);
            if (fd < 0) return 11;
            rc = copy_fd_to_stdout(fd);
            if (au_close(fd) != 0 && rc == 0) rc = 12;
        }
        if (rc != 0) return rc;
    }
    return 0;
}
