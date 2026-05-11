#include <aurora/tty.h>
#include <aurora/drivers.h>
#include <aurora/spinlock.h>
#include <aurora/arch/io.h>

static spinlock_t tty_lock;
static u32 tty_mode = AURORA_TTY_MODE_CANON | AURORA_TTY_MODE_ECHO;

void tty_init(void) {
    spinlock_init(&tty_lock);
    tty_mode = AURORA_TTY_MODE_CANON | AURORA_TTY_MODE_ECHO;
}

u32 tty_get_mode(void) {
    u64 flags = spin_lock_irqsave(&tty_lock);
    u32 mode = tty_mode;
    spin_unlock_irqrestore(&tty_lock, flags);
    return mode;
}

bool tty_set_mode(u32 mode) {
    const u32 allowed = AURORA_TTY_MODE_RAW | AURORA_TTY_MODE_ECHO | AURORA_TTY_MODE_CANON;
    if (mode & ~allowed) return false;
    if ((mode & AURORA_TTY_MODE_RAW) && (mode & AURORA_TTY_MODE_CANON)) return false;
    u64 flags = spin_lock_irqsave(&tty_lock);
    tty_mode = mode;
    spin_unlock_irqrestore(&tty_lock, flags);
    return true;
}

bool tty_getinfo(aurora_ttyinfo_t *out) {
    if (!out) return false;
    u32 rows = 0, cols = 0, row = 0, col = 0;
    vga_get_size(&rows, &cols);
    vga_get_cursor(&row, &col);
    out->rows = rows;
    out->cols = cols;
    out->cursor_row = row;
    out->cursor_col = col;
    out->mode = tty_get_mode();
    out->pending_keys = keyboard_pending();
    out->reserved0 = 0;
    out->reserved1 = 0;
    return true;
}

bool tty_read_key(aurora_key_event_t *out, u32 flags) {
    if (!out) return false;
    if (flags & ~AURORA_TTY_READ_NONBLOCK) return false;
    if (keyboard_get_event(out)) return true;
    if (flags & AURORA_TTY_READ_NONBLOCK) {
        out->code = AURORA_KEY_NONE;
        out->mods = 0;
        out->ch = 0;
        out->scancode = 0;
        return true;
    }
    for (;;) {
        cpu_sti();
        cpu_hlt();
        cpu_cli();
        if (keyboard_get_event(out)) return true;
    }
}

bool tty_selftest(void) {
    aurora_ttyinfo_t info;
    if (!tty_getinfo(&info)) return false;
    if (info.rows != 25u || info.cols != 80u) return false;
    u32 old = tty_get_mode();
    if (!tty_set_mode(AURORA_TTY_MODE_RAW)) return false;
    if (tty_get_mode() != AURORA_TTY_MODE_RAW) return false;
    if (tty_set_mode(AURORA_TTY_MODE_RAW | AURORA_TTY_MODE_CANON)) return false;
    if (!tty_set_mode(old)) return false;
    aurora_key_event_t ev;
    if (!tty_read_key(&ev, AURORA_TTY_READ_NONBLOCK)) return false;
    return ev.code == AURORA_KEY_NONE || ev.code == AURORA_KEY_CHAR || ev.code >= AURORA_KEY_UP;
}
