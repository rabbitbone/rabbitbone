#include <aurora/drivers.h>
#include <aurora/arch/io.h>
#include <aurora/libc.h>

#define COM1 0x3f8u

static int tx_empty(void) { return inb(COM1 + 5) & 0x20; }
bool serial_received(void) { return (inb(COM1 + 5) & 1) != 0; }

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xc7);
    outb(COM1 + 4, 0x0b);
}

void serial_putc(char c) {
    if (c == '\n') serial_putc('\r');
    for (u32 spin = 0; spin < 100000 && !tx_empty(); ++spin) {}
    outb(COM1, (u8)c);
}

void serial_write_n(const char *s, usize n) { for (usize i = 0; i < n; ++i) serial_putc(s[i]); }
void serial_write(const char *s) { if (s) serial_write_n(s, strlen(s)); }
int serial_read(void) { return serial_received() ? (int)inb(COM1) : -1; }
