#include <aurora_sys.h>

static int copy_fd_to_stdout(au_i64 in) {
    char buf[160];
    for (;;) {
        au_i64 got = au_read(in, buf, sizeof(buf));
        if (got < 0) return 20;
        if (got == 0) return 0;
        au_i64 off = 0;
        while (off < got) {
            au_i64 wrote = au_write((au_i64)AURORA_STDOUT, buf + off, (au_usize)(got - off));
            if (wrote <= 0) return 21;
            off += wrote;
        }
    }
}

int main(int argc, char **argv) {
    if (argc <= 1) return copy_fd_to_stdout((au_i64)AURORA_STDIN);
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) return 10;
        if (argv[i][0] == '-' && argv[i][1] == 0) {
            rc = copy_fd_to_stdout((au_i64)AURORA_STDIN);
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
