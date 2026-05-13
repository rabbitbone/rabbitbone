#ifndef AURORA_TTY_H
#define AURORA_TTY_H
#include <aurora/types.h>
#include <aurora/abi.h>
#if defined(__cplusplus)
extern "C" {
#endif

void tty_init(void);
bool tty_getinfo(aurora_ttyinfo_t *out);
bool tty_set_mode(u32 mode);
u32 tty_get_mode(void);
bool tty_read_char(char *out);
/* Kernel callers may block when AURORA_TTY_READ_NONBLOCK is not set.
 * User-process callers are scheduler-facing and receive AURORA_KEY_NONE when no key is ready. */
bool tty_read_key(aurora_key_event_t *out, u32 flags);
bool tty_scroll(i32 lines);
bool tty_set_cursor(u32 row, u32 col);
bool tty_set_cursor_visible(bool visible);
bool tty_clear_line(void);
bool tty_clear(void);
bool tty_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
