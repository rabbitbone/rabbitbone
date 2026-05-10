#ifndef AURORA_DRIVERS_H
#define AURORA_DRIVERS_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

void serial_init(void);
void serial_write(const char *s);
void serial_write_n(const char *s, usize n);
void serial_putc(char c);
bool serial_received(void);
int serial_read(void);

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);
void vga_write_n(const char *s, usize n);
void vga_set_color(u8 fg, u8 bg);

void pic_remap(u8 offset1, u8 offset2);
void pic_send_eoi(u8 irq);
void pic_set_mask(u8 irq);
void pic_clear_mask(u8 irq);

void pit_init(u32 hz);
u64 pit_ticks(void);
void pit_irq(void);

void keyboard_init(void);
void keyboard_irq(void);
bool keyboard_getc(char *out);

#if defined(__cplusplus)
}
#endif
#endif
