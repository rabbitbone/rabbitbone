#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/libc.h>

typedef enum ansi_state {
    ANSI_TEXT = 0,
    ANSI_ESC = 1,
    ANSI_CSI = 2,
} ansi_state_t;

static ansi_state_t ansi_state;
static u32 ansi_args[4];
static u32 ansi_argc;
static bool ansi_arg_active;

static void ansi_reset(void) {
    ansi_state = ANSI_TEXT;
    memset(ansi_args, 0, sizeof(ansi_args));
    ansi_argc = 0;
    ansi_arg_active = false;
}

static u32 ansi_arg(usize idx, u32 def) {
    if (idx >= ansi_argc || ansi_args[idx] == 0) return def;
    return ansi_args[idx];
}

static void ansi_finish(char cmd) {
    u32 row = 0, col = 0;
    vga_get_cursor(&row, &col);
    switch (cmd) {
        case 'H':
        case 'f': {
            u32 r = ansi_arg(0, 1u);
            u32 c = ansi_arg(1, 1u);
            vga_move_cursor(r ? r - 1u : 0u, c ? c - 1u : 0u);
            break;
        }
        case 'A': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row > n ? row - n : 0u, col);
            break;
        }
        case 'B': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row + n, col);
            break;
        }
        case 'C': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row, col + n);
            break;
        }
        case 'D': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row, col > n ? col - n : 0u);
            break;
        }
        case 'J':
            if (ansi_arg(0, 0u) == 2u || ansi_argc == 0) vga_clear();
            break;
        case 'K':
            vga_clear_line();
            break;
        case 'm':
            if (ansi_argc == 0 || ansi_args[0] == 0) vga_set_color(15, 1);
            for (u32 i = 0; i < ansi_argc; ++i) {
                u32 a = ansi_args[i];
                if (a >= 30u && a <= 37u) vga_set_color((u8)(a - 30u), 1);
                else if (a >= 40u && a <= 47u) vga_set_color(15, (u8)(a - 40u));
            }
            break;
        default:
            break;
    }
    ansi_reset();
}

static void vga_ansi_putc(char c) {
    switch (ansi_state) {
        case ANSI_TEXT:
            if ((unsigned char)c == 0x1b) { ansi_state = ANSI_ESC; return; }
            vga_putc(c);
            return;
        case ANSI_ESC:
            if (c == '[') {
                ansi_state = ANSI_CSI;
                ansi_argc = 1;
                ansi_args[0] = 0;
                ansi_arg_active = false;
                return;
            }
            ansi_reset();
            vga_putc(c);
            return;
        case ANSI_CSI:
            if (c >= '0' && c <= '9') {
                ansi_arg_active = true;
                if (ansi_argc == 0) ansi_argc = 1;
                u32 idx = ansi_argc - 1u;
                if (ansi_args[idx] <= 9999u) ansi_args[idx] = ansi_args[idx] * 10u + (u32)(c - '0');
                return;
            }
            if (c == ';') {
                if (ansi_argc < AURORA_ARRAY_LEN(ansi_args)) {
                    ++ansi_argc;
                    ansi_args[ansi_argc - 1u] = 0;
                    ansi_arg_active = false;
                }
                return;
            }
            if (!ansi_arg_active && ansi_argc == 1 && ansi_args[0] == 0) ansi_argc = 0;
            ansi_finish(c);
            return;
    }
}

void console_init(void) {
    vga_init();
    serial_init();
    ansi_reset();
}

void console_putc(char c) {
    serial_putc(c);
    vga_ansi_putc(c);
}

void console_write_n(const char *s, usize n) {
    for (usize i = 0; i < n; ++i) console_putc(s[i]);
}

void console_write(const char *s) {
    if (!s) return;
    console_write_n(s, strlen(s));
}

void console_clear(void) { vga_clear(); ansi_reset(); }
void console_set_color(u8 fg, u8 bg) { vga_set_color(fg, bg); }
