#ifndef RABBITBONE_DRIVERS_H
#define RABBITBONE_DRIVERS_H
#include <rabbitbone/types.h>
#include <rabbitbone/abi.h>
#include <rabbitbone/bootinfo.h>
#if defined(__cplusplus)
extern "C" {
#endif

void serial_init(void);
void serial_write(const char *s);
void serial_write_n(const char *s, usize n);
void serial_putc(char c);
bool serial_available(void);
bool serial_received(void);
int serial_read(void);

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_write(const char *s);
void vga_write_n(const char *s, usize n);
void vga_begin_update(void);
void vga_end_update(void);
void vga_flush(void);
void vga_set_color(u8 fg, u8 bg);
u8 vga_get_color(void);
void vga_fill_color(u8 fg, u8 bg);
void vga_recolor(u8 fg, u8 bg);
void vga_move_cursor(u32 row, u32 col);
void vga_get_cursor(u32 *row, u32 *col);
void vga_set_cursor_visible(bool visible);
void vga_clear_line(void);
void vga_get_size(u32 *rows, u32 *cols);
bool vga_scroll_view(i32 delta);
bool vga_enable_scrollback(void);
bool vga_use_boot_framebuffer(const rabbitbone_bootinfo_t *bootinfo);
bool vga_use_boot_framebuffer_early(const rabbitbone_bootinfo_t *bootinfo);

void pic_remap(u8 offset1, u8 offset2);
void pic_send_eoi(u8 irq);
bool pic_is_spurious_irq(u8 irq);
void pic_set_mask(u8 irq);
void pic_clear_mask(u8 irq);

void pit_init(u32 hz);
u64 pit_ticks(void);
void pit_irq(void);
bool pit_selftest(void);

void acpi_init(void);
void apic_init(void);
void hpet_init(void);
void smp_init_groundwork(void);
void pci_init(void);
void ahci_init(void);
void ata_pio_init(void);
void bootramdisk_init(const rabbitbone_bootinfo_t *bootinfo);

void keyboard_init(void);
void keyboard_irq(void);
bool keyboard_getc(char *out);
bool keyboard_get_event(rabbitbone_key_event_t *out);
bool keyboard_try_get_event(rabbitbone_key_event_t *out);
bool keyboard_peek_event(rabbitbone_key_event_t *out);
u32 keyboard_pending(void);

#if defined(__cplusplus)
}
#endif
#endif
