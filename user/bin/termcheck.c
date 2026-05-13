#include <aurora_sys.h>

static int write_console_bytes(const char *s, au_usize n) {
    return au_write_console(s, n) == (au_i64)n ? 0 : -1;
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    au_ttyinfo_t info;
    if (au_tty_getinfo(&info) != 0) return 1;
    if (info.rows < 25 || info.cols < 80) return 2;
    unsigned int old = info.mode;
    if (au_tty_setmode(AURORA_TTY_MODE_RAW) != 0) return 3;
    au_ttyinfo_t raw;
    if (au_tty_getinfo(&raw) != 0 || raw.mode != AURORA_TTY_MODE_RAW) return 4;
    au_key_event_t ev;
    if (au_tty_readkey(&ev, AURORA_TTY_READ_NONBLOCK) != 0) return 5;
    if (ev.code != AURORA_KEY_NONE && ev.code != AURORA_KEY_CHAR && ev.code < AURORA_KEY_UP) return 6;
    if (au_tty_setmode(old) != 0) return 7;
    if (au_tty_scroll(1) != 0) return 8;
    if (au_tty_scroll(-1) != 0) return 9;
    if (au_tty_setcursor(info.cursor_row, info.cursor_col) != 0) return 10;
    if (au_tty_clearline() != 0) return 11;
    if (au_tty_cursor_visible(0) != 0) return 12;
    if (au_tty_cursor_visible(1) != 0) return 13;

    if (write_console_bytes("", 0) != 0) return 14;
    return 0;
}
