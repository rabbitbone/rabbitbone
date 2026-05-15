#ifndef RABBITBONE_TERM_H
#define RABBITBONE_TERM_H
#include <rabbitbone_sys.h>

static inline au_i64 au_term_write(const char *s) {
    if (!s) return 0;
    au_usize n = au_strlen(s);
    au_usize off = 0;
    while (off < n) {
        au_i64 wrote = au_write(AU_STDOUT, s + off, n - off);
        if (wrote <= 0) return wrote < 0 ? wrote : -1;
        off += (au_usize)wrote;
    }
    return (au_i64)n;
}
static inline au_i64 au_term_clear(void) { return au_tty_clear(); }
static inline au_i64 au_term_move(unsigned int row, unsigned int col) { return au_tty_setcursor(row, col); }
static inline au_i64 au_term_clear_line(void) { return au_tty_clearline(); }
static inline au_i64 au_term_scroll(int lines) { return au_tty_scroll(lines); }
static inline au_i64 au_term_cursor_visible(unsigned int visible) { return au_tty_cursor_visible(visible); }
static inline void au_term_set_raw(void) { (void)au_tty_setmode(RABBITBONE_TTY_MODE_RAW); }
static inline void au_term_set_canon_echo(void) { (void)au_tty_setmode(RABBITBONE_TTY_MODE_CANON | RABBITBONE_TTY_MODE_ECHO); }
static inline int au_term_read_key(au_key_event_t *ev) { return (int)au_tty_readkey(ev, RABBITBONE_TTY_READ_NONBLOCK); }
static inline int au_term_read_key_blocking(au_key_event_t *ev) {
    if (!ev) return -1;
    for (;;) {
        au_i64 r = au_tty_readkey(ev, RABBITBONE_TTY_READ_NONBLOCK);
        if (r < 0) return (int)r;
        if (ev->code != RABBITBONE_KEY_NONE) return (int)r;
        (void)au_yield();
    }
}

#endif
