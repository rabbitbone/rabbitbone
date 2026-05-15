#ifndef RABBITBONE_CONSOLE_H
#define RABBITBONE_CONSOLE_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

void console_init(void);
void console_write(const char *s);
void console_write_n(const char *s, usize n);
void console_putc(char c);
void console_flush(void);
void console_clear(void);
bool console_scroll(i32 lines);
bool console_move_cursor(u32 row, u32 col);
bool console_set_cursor_visible(bool visible);
bool console_clear_line(void);
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
