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
bool tty_read_key(aurora_key_event_t *out, u32 flags);
bool tty_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
