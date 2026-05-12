#ifndef AURORA_TERM_H
#define AURORA_TERM_H
#include <aurora_sys.h>

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
static inline au_i64 au_term_clear(void) { return au_term_write("\x1b[2J\x1b[1;1H"); }
static inline au_i64 au_term_move(unsigned int row, unsigned int col) {
    char buf[32];
    unsigned int n = 0;
    buf[n++] = '\x1b'; buf[n++] = '[';
    if (row == 0) row = 1;
    if (col == 0) col = 1;
    char tmp[10]; unsigned int m = 0; unsigned int v = row;
    do { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && m < sizeof(tmp));
    while (m) buf[n++] = tmp[--m];
    buf[n++] = ';'; m = 0; v = col;
    do { tmp[m++] = (char)('0' + (v % 10u)); v /= 10u; } while (v && m < sizeof(tmp));
    while (m) buf[n++] = tmp[--m];
    buf[n++] = 'H';
    return au_write(AU_STDOUT, buf, n);
}
static inline au_i64 au_term_clear_line(void) { return au_term_write("\x1b[K"); }
static inline void au_term_set_raw(void) { (void)au_tty_setmode(AURORA_TTY_MODE_RAW); }
static inline void au_term_set_canon_echo(void) { (void)au_tty_setmode(AURORA_TTY_MODE_CANON | AURORA_TTY_MODE_ECHO); }
static inline int au_term_read_key(au_key_event_t *ev) { return (int)au_tty_readkey(ev, AURORA_TTY_READ_NONBLOCK); }
static inline int au_term_read_key_blocking(au_key_event_t *ev) {
    if (!ev) return -1;
    for (;;) {
        au_i64 r = au_tty_readkey(ev, AURORA_TTY_READ_NONBLOCK);
        if (r < 0) return (int)r;
        if (ev->code != AURORA_KEY_NONE) return (int)r;
        (void)au_yield();
    }
}

#endif
