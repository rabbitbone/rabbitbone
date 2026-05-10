#ifndef AURORA_CONSOLE_H
#define AURORA_CONSOLE_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

void console_init(void);
void console_write(const char *s);
void console_write_n(const char *s, usize n);
void console_putc(char c);
void console_clear(void);
void console_set_color(u8 fg, u8 bg);
int kprintf(const char *fmt, ...);
int ksnprintf(char *buf, usize cap, const char *fmt, ...);
int kvsnprintf(char *buf, usize cap, const char *fmt, __builtin_va_list ap);

#if defined(__cplusplus)
}
#endif
#endif
