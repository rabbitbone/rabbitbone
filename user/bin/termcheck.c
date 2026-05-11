#include <aurora_sys.h>
#include <aurora_term.h>

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
    au_term_move(1, 1);
    au_term_clear_line();
    return 0;
}
