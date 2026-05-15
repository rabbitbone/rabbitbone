#include <rabbitbone_sys.h>

static int write_console_bytes(const char *s, au_usize n) {
    return au_write_console(s, n) == (au_i64)n ? 0 : -1;
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;
    au_ttyinfo_t info;
    if (au_tty_getinfo(&info) != 0) return 1;
    if (info.rows < 12 || info.cols < 40 || info.cursor_row >= info.rows || info.cursor_col >= info.cols) return 2;
    unsigned int old = info.mode;
    if (au_tty_setmode(RABBITBONE_TTY_MODE_RAW) != 0) return 3;
    au_ttyinfo_t raw;
    if (au_tty_getinfo(&raw) != 0 || raw.mode != RABBITBONE_TTY_MODE_RAW) return 4;
    au_key_event_t ev;
    if (au_tty_readkey(&ev, RABBITBONE_TTY_READ_NONBLOCK) != 0) return 5;
    if (ev.code != RABBITBONE_KEY_NONE && ev.code != RABBITBONE_KEY_CHAR && ev.code < RABBITBONE_KEY_UP) return 6;
    if (au_tty_setmode(old) != 0) return 7;
    if (au_tty_scroll(1) != 0) return 8;
    if (au_tty_scroll(-1) != 0) return 9;
    au_ttyinfo_t after_scroll;
    if (au_tty_getinfo(&after_scroll) != 0) return 10;
    if (after_scroll.rows < 1u || after_scroll.cols < 1u) return 11;
    unsigned int safe_row = 0u;
    unsigned int safe_col = 0u;
    if (au_tty_setcursor(safe_row, safe_col) != 0) return 12;
    if (au_tty_clearline() != 0) return 13;
    au_ttyinfo_t after_clear;
    if (au_tty_getinfo(&after_clear) != 0 || after_clear.rows < 1u || after_clear.cols < 1u) return 14;
    if (au_tty_setcursor(after_clear.rows - 1u, after_clear.cols - 1u) != 0) return 15;
    if (au_tty_setcursor(0u, 0u) != 0) return 16;
    if (au_tty_cursor_visible(0) != 0) return 17;
    if (au_tty_cursor_visible(1) != 0) return 18;

    if (write_console_bytes("", 0) != 0) return 19;
    return 0;
}
