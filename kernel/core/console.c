#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/libc.h>
#include <aurora/spinlock.h>
#include <aurora/abi.h>

typedef enum ansi_state {
    ANSI_TEXT = 0,
    ANSI_ESC = 1,
    ANSI_CSI = 2,
} ansi_state_t;

typedef struct console_theme_def {
    u32 id;
    const char *name;
    u8 fg;
    u8 bg;
    u8 panic_fg;
    u8 panic_bg;
} console_theme_def_t;

static const console_theme_def_t themes[] = {
    { AURORA_THEME_LEGACY, "legacy", 15, 1, 15, 4 },
    { AURORA_THEME_BLACK,  "black",  15, 0, 15, 0 },
};

static spinlock_t console_lock;
static ansi_state_t ansi_state;
static u32 ansi_args[4];
static u32 ansi_argc;
static bool ansi_arg_active;
static u32 active_theme = AURORA_THEME_LEGACY;

static const console_theme_def_t *theme_by_id(u32 id) {
    for (usize i = 0; i < AURORA_ARRAY_LEN(themes); ++i) {
        if (themes[i].id == id) return &themes[i];
    }
    return &themes[0];
}

static const console_theme_def_t *current_theme_def(void) { return theme_by_id(active_theme); }

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

static u32 clamp_u32(u32 v, u32 max) { return v > max ? max : v; }

static void apply_normal_theme_unlocked(void) {
    const console_theme_def_t *t = current_theme_def();
    vga_set_color(t->fg, t->bg);
}

static void recolor_normal_theme_unlocked(void) {
    const console_theme_def_t *t = current_theme_def();
    vga_set_color(t->fg, t->bg);
    vga_recolor(t->fg, t->bg);
}

static void ansi_finish(char cmd) {
    u32 row = 0, col = 0, rows = 0, cols = 0;
    vga_get_cursor(&row, &col);
    vga_get_size(&rows, &cols);
    u32 max_row = rows ? rows - 1u : 0u;
    u32 max_col = cols ? cols - 1u : 0u;
    row = clamp_u32(row, max_row);
    col = clamp_u32(col, max_col);
    const console_theme_def_t *theme = current_theme_def();
    switch (cmd) {
        case 'H':
        case 'f': {
            u32 r = ansi_arg(0, 1u);
            u32 c = ansi_arg(1, 1u);
            vga_move_cursor(clamp_u32(r ? r - 1u : 0u, max_row), clamp_u32(c ? c - 1u : 0u, max_col));
            break;
        }
        case 'A': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row > n ? row - n : 0u, col);
            break;
        }
        case 'B': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(clamp_u32(row + (n > max_row - row ? max_row - row : n), max_row), col);
            break;
        }
        case 'C': {
            u32 n = ansi_arg(0, 1u);
            vga_move_cursor(row, clamp_u32(col + (n > max_col - col ? max_col - col : n), max_col));
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
            if (ansi_argc == 0 || ansi_args[0] == 0) apply_normal_theme_unlocked();
            for (u32 i = 0; i < ansi_argc; ++i) {
                u32 a = ansi_args[i];
                if (a == 0u) apply_normal_theme_unlocked();
                else if (a >= 30u && a <= 37u) vga_set_color((u8)(a - 30u), theme->bg);
                else if (a >= 40u && a <= 47u) vga_set_color(theme->fg, (u8)(a - 40u));
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

static void console_putc_unlocked(char c) {
    serial_putc(c);
    vga_ansi_putc(c);
}

void console_init(void) {
    spinlock_init(&console_lock);
    vga_init();
    serial_init();
    active_theme = AURORA_THEME_LEGACY;
    apply_normal_theme_unlocked();
    ansi_reset();
}

void console_putc(char c) {
    u64 flags = spin_lock_irqsave(&console_lock);
    console_putc_unlocked(c);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_write_n(const char *s, usize n) {
    if (!s && n) return;
    u64 flags = spin_lock_irqsave(&console_lock);
    for (usize i = 0; i < n; ++i) console_putc_unlocked(s[i]);
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_write(const char *s) {
    if (!s) return;
    console_write_n(s, strlen(s));
}

void console_clear(void) {
    u64 flags = spin_lock_irqsave(&console_lock);
    apply_normal_theme_unlocked();
    vga_clear();
    ansi_reset();
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_set_color(u8 fg, u8 bg) {
    u64 flags = spin_lock_irqsave(&console_lock);
    vga_set_color(fg, bg);
    spin_unlock_irqrestore(&console_lock, flags);
}

u32 console_theme(void) {
    u64 flags = spin_lock_irqsave(&console_lock);
    u32 id = active_theme;
    spin_unlock_irqrestore(&console_lock, flags);
    return id;
}

const char *console_theme_name(u32 id) { return theme_by_id(id)->name; }

bool console_set_theme(u32 id) {
    if (id >= AURORA_THEME_MAX) return false;
    u64 flags = spin_lock_irqsave(&console_lock);
    active_theme = id;
    recolor_normal_theme_unlocked();
    spin_unlock_irqrestore(&console_lock, flags);
    return true;
}

void console_apply_theme(void) {
    u64 flags = spin_lock_irqsave(&console_lock);
    apply_normal_theme_unlocked();
    spin_unlock_irqrestore(&console_lock, flags);
}

void console_panic_begin(void) {
    u64 flags = spin_lock_irqsave(&console_lock);
    const console_theme_def_t *theme = current_theme_def();
    vga_set_color(theme->panic_fg, theme->panic_bg);
    vga_clear();
    ansi_reset();
    spin_unlock_irqrestore(&console_lock, flags);
}
