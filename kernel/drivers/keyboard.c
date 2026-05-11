#include <aurora/drivers.h>
#include <aurora/arch/io.h>
#include <aurora/log.h>
#include <aurora/spinlock.h>

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define QUEUE_SIZE 64u

static aurora_key_event_t queue[QUEUE_SIZE];
static u32 head, tail;
static bool shift_down;
static bool ctrl_down;
static bool alt_down;
static bool extended;
static spinlock_t kbd_lock;

static const char normal_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'', '`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

static const char shift_map[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"', '~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

static u32 mods(void) {
    return (shift_down ? AURORA_KEY_MOD_SHIFT : 0u) |
           (ctrl_down ? AURORA_KEY_MOD_CTRL : 0u) |
           (alt_down ? AURORA_KEY_MOD_ALT : 0u);
}

static void enqueue_event(u32 code, u32 ch, u32 scancode) {
    u32 next = (tail + 1u) % QUEUE_SIZE;
    if (next == head) return;
    aurora_key_event_t ev;
    ev.code = code;
    ev.mods = mods();
    ev.ch = ch;
    ev.scancode = scancode;
    queue[tail] = ev;
    tail = next;
}

static void enqueue_char(char c, u32 scancode) {
    u32 code = AURORA_KEY_CHAR;
    if (c == '\n') code = AURORA_KEY_ENTER;
    else if (c == '\b') code = AURORA_KEY_BACKSPACE;
    else if (c == '\t') code = AURORA_KEY_TAB;
    else if ((unsigned char)c == 27u) code = AURORA_KEY_ESC;
    enqueue_event(code, (u32)(unsigned char)c, scancode);
}

void keyboard_init(void) {
    spinlock_init(&kbd_lock);
    head = tail = 0;
    shift_down = false;
    ctrl_down = false;
    alt_down = false;
    extended = false;
    KLOG(LOG_INFO, "kbd", "ps/2 keyboard ready");
}

void keyboard_irq(void) {
    if ((inb(KBD_STATUS) & 1) == 0) return;
    u8 sc = inb(KBD_DATA);
    u64 flags = spin_lock_irqsave(&kbd_lock);
    if (sc == 0xe0) {
        extended = true;
        spin_unlock_irqrestore(&kbd_lock, flags);
        return;
    }
    if (sc == 0xe1) {
        extended = false;
        spin_unlock_irqrestore(&kbd_lock, flags);
        return;
    }
    bool release = (sc & 0x80u) != 0;
    u8 code = sc & 0x7fu;
    bool was_extended = extended;
    extended = false;

    if (!was_extended && (code == 42 || code == 54)) { shift_down = !release; spin_unlock_irqrestore(&kbd_lock, flags); return; }
    if (!was_extended && code == 29) { ctrl_down = !release; spin_unlock_irqrestore(&kbd_lock, flags); return; }
    if (!was_extended && code == 56) { alt_down = !release; spin_unlock_irqrestore(&kbd_lock, flags); return; }
    if (was_extended && code == 29) { ctrl_down = !release; spin_unlock_irqrestore(&kbd_lock, flags); return; }
    if (was_extended && code == 56) { alt_down = !release; spin_unlock_irqrestore(&kbd_lock, flags); return; }
    if (release) { spin_unlock_irqrestore(&kbd_lock, flags); return; }

    if (was_extended) {
        switch (code) {
            case 0x48: enqueue_event(AURORA_KEY_UP, 0, sc); break;
            case 0x50: enqueue_event(AURORA_KEY_DOWN, 0, sc); break;
            case 0x4b: enqueue_event(AURORA_KEY_LEFT, 0, sc); break;
            case 0x4d: enqueue_event(AURORA_KEY_RIGHT, 0, sc); break;
            case 0x53: enqueue_event(AURORA_KEY_DELETE, 0, sc); break;
            case 0x47: enqueue_event(AURORA_KEY_HOME, 0, sc); break;
            case 0x4f: enqueue_event(AURORA_KEY_END, 0, sc); break;
            case 0x49: enqueue_event(AURORA_KEY_PAGEUP, 0, sc); break;
            case 0x51: enqueue_event(AURORA_KEY_PAGEDOWN, 0, sc); break;
            default: break;
        }
        spin_unlock_irqrestore(&kbd_lock, flags);
        return;
    }

    char c = code < 128u ? (shift_down ? shift_map[code] : normal_map[code]) : 0;
    if (c && ctrl_down && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);
    if (c && ctrl_down && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 1);
    if (c) enqueue_char(c, sc);
    spin_unlock_irqrestore(&kbd_lock, flags);
}

bool keyboard_get_event(aurora_key_event_t *out) {
    u64 flags = spin_lock_irqsave(&kbd_lock);
    if (head == tail) { spin_unlock_irqrestore(&kbd_lock, flags); return false; }
    if (out) *out = queue[head];
    head = (head + 1u) % QUEUE_SIZE;
    spin_unlock_irqrestore(&kbd_lock, flags);
    return true;
}

bool keyboard_try_get_event(aurora_key_event_t *out) {
    u64 flags = irq_save();
    for (u32 spin = 0; spin < 1024u; ++spin) {
        if (spin_try_lock(&kbd_lock)) {
            bool ok = false;
            if (head != tail) {
                if (out) *out = queue[head];
                head = (head + 1u) % QUEUE_SIZE;
                ok = true;
            }
            spin_unlock(&kbd_lock);
            irq_restore(flags);
            return ok;
        }
        __asm__ volatile("pause");
    }
    irq_restore(flags);
    return false;
}

bool keyboard_peek_event(aurora_key_event_t *out) {
    u64 flags = spin_lock_irqsave(&kbd_lock);
    if (head == tail) { spin_unlock_irqrestore(&kbd_lock, flags); return false; }
    if (out) *out = queue[head];
    spin_unlock_irqrestore(&kbd_lock, flags);
    return true;
}

u32 keyboard_pending(void) {
    u64 flags = spin_lock_irqsave(&kbd_lock);
    u32 n = tail >= head ? tail - head : QUEUE_SIZE - head + tail;
    spin_unlock_irqrestore(&kbd_lock, flags);
    return n;
}

bool keyboard_getc(char *out) {
    aurora_key_event_t ev;
    while (keyboard_get_event(&ev)) {
        if (ev.ch) {
            if (out) *out = (char)ev.ch;
            return true;
        }
    }
    return false;
}
