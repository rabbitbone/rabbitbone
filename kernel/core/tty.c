#include <aurora/tty.h>
#include <aurora/drivers.h>
#include <aurora/process.h>
#include <aurora/libc.h>
#include <aurora/spinlock.h>
#include <aurora/arch/io.h>

#define TTY_MODE_SLOTS 32u
#define TTY_DEFAULT_MODE (AURORA_TTY_MODE_CANON | AURORA_TTY_MODE_ECHO)

typedef struct tty_mode_slot {
    bool used;
    u32 pid;
    u32 mode;
} tty_mode_slot_t;

static spinlock_t tty_lock;
static u32 kernel_tty_mode = TTY_DEFAULT_MODE;
static tty_mode_slot_t tty_modes[TTY_MODE_SLOTS];

static bool tty_mode_valid(u32 mode) {
    const u32 allowed = AURORA_TTY_MODE_RAW | AURORA_TTY_MODE_ECHO | AURORA_TTY_MODE_CANON;
    if (mode & ~allowed) return false;
    if ((mode & AURORA_TTY_MODE_RAW) && (mode & AURORA_TTY_MODE_CANON)) return false;
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
    if (!create) return 0;
    tty_mode_slot_t *slot = free_slot ? free_slot : &tty_modes[pid % TTY_MODE_SLOTS];
    slot->used = true;
    slot->pid = pid;
    slot->mode = kernel_tty_mode;
    return slot;
}

void tty_init(void) {
    spinlock_init(&tty_lock);
    kernel_tty_mode = TTY_DEFAULT_MODE;
    memset(tty_modes, 0, sizeof(tty_modes));
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
    aurora_key_event_t ev;
    if (!keyboard_peek_event(&ev)) return false;
    if (!ev.ch) return false;
    if (!keyboard_get_event(&ev)) return false;
    if (out) *out = (char)ev.ch;
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

static void tty_key_none(aurora_key_event_t *out) {
    if (!out) return;
    out->code = AURORA_KEY_NONE;
    out->mods = 0;
    out->ch = 0;
    out->scancode = 0;
}

bool tty_read_key(aurora_key_event_t *out, u32 flags) {
    if (!out) return false;
    if (flags & ~AURORA_TTY_READ_NONBLOCK) return false;
    if (keyboard_get_event(out)) return true;
    if ((flags & AURORA_TTY_READ_NONBLOCK) || process_user_active()) {
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
