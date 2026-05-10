#include <aurora/drivers.h>
#include <aurora/arch/io.h>
#include <aurora/log.h>
#include <aurora/spinlock.h>

#define KBD_DATA 0x60
#define KBD_STATUS 0x64
#define QUEUE_SIZE 128u

static char queue[QUEUE_SIZE];
static u32 head, tail;
static bool shift_down;
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

static void enqueue(char c) {
    u32 next = (tail + 1) % QUEUE_SIZE;
    if (next == head) return;
    queue[tail] = c;
    tail = next;
}

void keyboard_init(void) {
    spinlock_init(&kbd_lock);
    head = tail = 0;
    shift_down = false;
    KLOG(LOG_INFO, "kbd", "ps/2 keyboard ready");
}

void keyboard_irq(void) {
    if ((inb(KBD_STATUS) & 1) == 0) return;
    u8 sc = inb(KBD_DATA);
    bool release = (sc & 0x80) != 0;
    u8 code = sc & 0x7f;
    u64 flags = spin_lock_irqsave(&kbd_lock);
    if (code == 42 || code == 54) {
        shift_down = !release;
        spin_unlock_irqrestore(&kbd_lock, flags);
        return;
    }
    if (!release) {
        char c = code < 128u ? (shift_down ? shift_map[code] : normal_map[code]) : 0;
        if (c) enqueue(c);
    }
    spin_unlock_irqrestore(&kbd_lock, flags);
}

bool keyboard_getc(char *out) {
    u64 flags = spin_lock_irqsave(&kbd_lock);
    if (head == tail) {
        spin_unlock_irqrestore(&kbd_lock, flags);
        return false;
    }
    if (out) *out = queue[head];
    head = (head + 1) % QUEUE_SIZE;
    spin_unlock_irqrestore(&kbd_lock, flags);
    return true;
}
