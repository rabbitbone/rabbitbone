#include <aurora/drivers.h>
#include <aurora/libc.h>

#define VGA_WIDTH 80u
#define VGA_HEIGHT 25u
#define VGA_MEMORY ((volatile u16 *)0xb8000ull)

static usize row;
static usize col;
static u8 color;

static u16 make_cell(char c) { return (u16)c | ((u16)color << 8); }

static void clamp_cursor(void) {
    if (row >= VGA_HEIGHT) row = VGA_HEIGHT - 1u;
    if (col >= VGA_WIDTH) col = 0u;
}

static void scroll_if_needed(void) {
    if (row < VGA_HEIGHT) return;
    for (usize y = 1; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }
    for (usize x = 0; x < VGA_WIDTH; ++x) VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = make_cell(' ');
    row = VGA_HEIGHT - 1;
}

void vga_set_color(u8 fg, u8 bg) { color = (u8)((bg << 4) | (fg & 0x0f)); }

void vga_clear(void) {
    for (usize y = 0; y < VGA_HEIGHT; ++y) {
        for (usize x = 0; x < VGA_WIDTH; ++x) VGA_MEMORY[y * VGA_WIDTH + x] = make_cell(' ');
    }
    row = 0;
    col = 0;
}

void vga_init(void) {
    vga_set_color(15, 1);
    vga_clear();
}

void vga_putc(char c) {
    clamp_cursor();
    if (c == '\r') return;
    if (c == '\n') {
        col = 0;
        ++row;
        scroll_if_needed();
        return;
    }
    if (c == '\b') {
        if (col > 0) --col;
        clamp_cursor();
        VGA_MEMORY[row * VGA_WIDTH + col] = make_cell(' ');
        return;
    }
    clamp_cursor();
    VGA_MEMORY[row * VGA_WIDTH + col] = make_cell(c);
    ++col;
    if (col >= VGA_WIDTH) {
        col = 0;
        ++row;
        scroll_if_needed();
    }
}

void vga_write_n(const char *s, usize n) { for (usize i = 0; i < n; ++i) vga_putc(s[i]); }
void vga_write(const char *s) { if (s) vga_write_n(s, strlen(s)); }
