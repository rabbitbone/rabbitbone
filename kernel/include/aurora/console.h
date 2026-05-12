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
u32 console_theme(void);
const char *console_theme_name(u32 id);
bool console_set_theme(u32 id);
void console_apply_theme(void);
void console_panic_begin(void);
int kprintf(const char *fmt, ...);
int ksnprintf(char *buf, usize cap, const char *fmt, ...);
int kvsnprintf(char *buf, usize cap, const char *fmt, __builtin_va_list ap);

#if defined(__cplusplus)
}
#endif
#endif
