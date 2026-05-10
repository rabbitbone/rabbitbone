#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/libc.h>

void console_init(void) {
    vga_init();
    serial_init();
}

void console_putc(char c) {
    vga_putc(c);
    serial_putc(c);
}

void console_write_n(const char *s, usize n) {
    vga_write_n(s, n);
    serial_write_n(s, n);
}

void console_write(const char *s) {
    if (!s) return;
    console_write_n(s, strlen(s));
}

void console_clear(void) { vga_clear(); }
void console_set_color(u8 fg, u8 bg) { vga_set_color(fg, bg); }
