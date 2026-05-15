#include <rabbitbone/tty.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/process.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/arch/io.h>

#define TTY_MODE_SLOTS 32u
#define TTY_DEFAULT_MODE (RABBITBONE_TTY_MODE_CANON | RABBITBONE_TTY_MODE_ECHO)

typedef struct tty_mode_slot {
    bool used;
    u32 pid;
    u32 mode;
} tty_mode_slot_t;

static spinlock_t tty_lock;
static u32 kernel_tty_mode = TTY_DEFAULT_MODE;
static tty_mode_slot_t tty_modes[TTY_MODE_SLOTS];

static bool tty_mode_valid(u32 mode) {
    const u32 allowed = RABBITBONE_TTY_MODE_RAW | RABBITBONE_TTY_MODE_ECHO | RABBITBONE_TTY_MODE_CANON;
    if (mode & ~allowed) return false;
    if ((mode & RABBITBONE_TTY_MODE_RAW) && (mode & RABBITBONE_TTY_MODE_CANON)) return false;
    return true;
}

static u32 current_tty_pid(void) {
    return process_user_active() ? process_current_pid() : 0u;
}

static tty_mode_slot_t *tty_find_slot_locked(u32 pid, bool create) {
    tty_mode_slot_t *free_slot = 0;
    for (usize i = 0; i < TTY_MODE_SLOTS; ++i) {
        if (tty_modes[i].used && tty_modes[i].pid == pid) return &tty_modes[i];
        if (!tty_modes[i].used && !free_slot) free_slot = &tty_modes[i];
    }
    if (!create || !free_slot) return 0;
    free_slot->used = true;
    free_slot->pid = pid;
    free_slot->mode = TTY_DEFAULT_MODE;
    return free_slot;
}

void tty_init(void) {
    spinlock_init(&tty_lock);
    kernel_tty_mode = TTY_DEFAULT_MODE;
    memset(tty_modes, 0, sizeof(tty_modes));
    if (!vga_enable_scrollback()) KLOG(LOG_WARN, "tty", "scrollback initialization failed; using live screen only");
}

u32 tty_get_mode(void) {
    u32 pid = current_tty_pid();
    u64 flags = spin_lock_irqsave(&tty_lock);
    u32 mode = kernel_tty_mode;
    if (pid) {
        tty_mode_slot_t *slot = tty_find_slot_locked(pid, false);
        if (slot) mode = slot->mode;
    }
    spin_unlock_irqrestore(&tty_lock, flags);
    return mode;
}


void tty_forget_pid(u32 pid) {
    if (!pid) return;
    u64 flags = spin_lock_irqsave(&tty_lock);
    for (usize i = 0; i < TTY_MODE_SLOTS; ++i) {
        if (tty_modes[i].used && tty_modes[i].pid == pid) {
            memset(&tty_modes[i], 0, sizeof(tty_modes[i]));
            break;
        }
    }
    spin_unlock_irqrestore(&tty_lock, flags);
}

bool tty_set_mode(u32 mode) {
    if (!tty_mode_valid(mode)) return false;
    u32 pid = current_tty_pid();
    u64 flags = spin_lock_irqsave(&tty_lock);
    if (pid) {
        tty_mode_slot_t *slot = tty_find_slot_locked(pid, true);
        if (!slot) { spin_unlock_irqrestore(&tty_lock, flags); return false; }
        slot->mode = mode;
    } else {
        kernel_tty_mode = mode;
    }
    spin_unlock_irqrestore(&tty_lock, flags);
    return true;
}

bool tty_read_char(char *out) {
    console_flush();
    rabbitbone_key_event_t ev;
    while (keyboard_get_event(&ev)) {
        if (!ev.ch) continue;
        if (out) *out = (char)ev.ch;
        return true;
    }
    return false;
}

bool tty_getinfo(rabbitbone_ttyinfo_t *out) {
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

static void tty_key_none(rabbitbone_key_event_t *out) {
    if (!out) return;
    out->code = RABBITBONE_KEY_NONE;
    out->mods = 0;
    out->ch = 0;
    out->scancode = 0;
}

bool tty_scroll(i32 lines) {
    return console_scroll(lines);
}

bool tty_set_cursor(u32 row, u32 col) {
    u32 rows = 0, cols = 0;
    vga_get_size(&rows, &cols);
    if (rows == 0 || cols == 0) return false;
    if (row >= rows || col >= cols) return false;
    return console_move_cursor(row, col);
}

bool tty_set_cursor_visible(bool visible) {
    return console_set_cursor_visible(visible);
}

bool tty_clear_line(void) {
    return console_clear_line();
}

bool tty_clear(void) {
    console_clear();
    return true;
}

bool tty_read_key(rabbitbone_key_event_t *out, u32 flags) {
    console_flush();
    if (!out) return false;
    if (flags & ~RABBITBONE_TTY_READ_NONBLOCK) {
        tty_key_none(out);
        return false;
    }
    if (keyboard_get_event(out)) return true;
    if ((flags & RABBITBONE_TTY_READ_NONBLOCK) || process_user_active()) {
        tty_key_none(out);
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
    rabbitbone_ttyinfo_t info;
    if (!tty_getinfo(&info)) return false;
    if (info.rows < 12u || info.cols < 40u || info.cursor_row >= info.rows || info.cursor_col >= info.cols) return false;
    u32 old = tty_get_mode();
    if (!tty_set_mode(RABBITBONE_TTY_MODE_RAW)) return false;
    if (tty_get_mode() != RABBITBONE_TTY_MODE_RAW) return false;
    if (tty_set_mode(RABBITBONE_TTY_MODE_RAW | RABBITBONE_TTY_MODE_CANON)) return false;
    if (!tty_set_mode(old)) return false;
    u32 old_row = info.cursor_row;
    u32 old_col = info.cursor_col;
    if (!tty_set_cursor(info.rows - 1u, info.cols - 1u)) return false;
    if (!tty_set_cursor(old_row < info.rows ? old_row : 0u, old_col < info.cols ? old_col : 0u)) return false;
    rabbitbone_key_event_t ev;
    if (!tty_read_key(&ev, RABBITBONE_TTY_READ_NONBLOCK)) return false;
    return ev.code == RABBITBONE_KEY_NONE || ev.code == RABBITBONE_KEY_CHAR || ev.code >= RABBITBONE_KEY_UP;
}
