#include <rabbitbone/log.h>
#include <rabbitbone/console.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/spinlock.h>

#define LOG_EARLY_LINES 64u
#define LOG_EARLY_LINE_LEN 192u
#define LOG_HEAP_LINES 4096u
#define LOG_HEAP_LINE_LEN 384u

static char early_ring[LOG_EARLY_LINES][LOG_EARLY_LINE_LEN];
static char *ring_storage;
static u32 ring_lines;
static u32 ring_line_len;
static u32 write_index;
static u32 total_lines;
static spinlock_t log_lock;
static log_level_t console_min_level = LOG_ERROR;
static log_level_t serial_min_level = LOG_TRACE;
static bool heap_ring_active;

static char *ring_line(u32 idx) {
    return ring_storage + (usize)idx * (usize)ring_line_len;
}

const char *log_level_name(log_level_t level) {
    switch (level) {
        case LOG_TRACE: return "TRACE";
        case LOG_INFO: return "INFO";
        case LOG_WARN: return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

void log_init(void) {
    spinlock_init(&log_lock);
    ring_storage = &early_ring[0][0];
    ring_lines = LOG_EARLY_LINES;
    ring_line_len = LOG_EARLY_LINE_LEN;
    write_index = 0;
    total_lines = 0;
    heap_ring_active = false;
    memset(early_ring, 0, sizeof(early_ring));
}

void log_enable_heap_ring(void) {
    if (heap_ring_active) return;
    char *new_ring = (char *)kcalloc(LOG_HEAP_LINES, LOG_HEAP_LINE_LEN);
    if (!new_ring) return;

    u64 flags = spin_lock_irqsave(&log_lock);
    if (heap_ring_active) {
        spin_unlock_irqrestore(&log_lock, flags);
        kfree(new_ring);
        return;
    }

    u32 copy_count = total_lines;
    if (copy_count > LOG_HEAP_LINES) copy_count = LOG_HEAP_LINES;
    u32 old_start = total_lines == ring_lines ? write_index : 0u;
    if (total_lines > copy_count) old_start = (old_start + (total_lines - copy_count)) % ring_lines;

    for (u32 i = 0; i < copy_count; ++i) {
        const char *src = ring_line((old_start + i) % ring_lines);
        char *dst = new_ring + (usize)i * LOG_HEAP_LINE_LEN;
        strncpy(dst, src, LOG_HEAP_LINE_LEN - 1u);
        dst[LOG_HEAP_LINE_LEN - 1u] = 0;
    }

    ring_storage = new_ring;
    ring_lines = LOG_HEAP_LINES;
    ring_line_len = LOG_HEAP_LINE_LEN;
    total_lines = copy_count;
    write_index = copy_count == LOG_HEAP_LINES ? 0u : copy_count;
    heap_ring_active = true;
    spin_unlock_irqrestore(&log_lock, flags);
}

void log_write(log_level_t level, const char *component, const char *fmt, ...) {
    char msg[320];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);

    char line[LOG_HEAP_LINE_LEN];
    ksnprintf(line, sizeof(line), "[%s] %s: %s\n", log_level_name(level), component ? component : "kernel", msg);
    u64 flags = spin_lock_irqsave(&log_lock);
    char *dst = ring_line(write_index);
    strncpy(dst, line, ring_line_len - 1u);
    dst[ring_line_len - 1u] = 0;
    write_index = (write_index + 1u) % ring_lines;
    if (total_lines < ring_lines) ++total_lines;
    if (level >= console_min_level) console_write(line);
    else if (level >= serial_min_level) serial_write(line);
    spin_unlock_irqrestore(&log_lock, flags);
}

void log_dump_ring(void (*writer)(const char *line)) {
    if (!writer) return;
    u64 flags = spin_lock_irqsave(&log_lock);
    u32 start = total_lines == ring_lines ? write_index : 0u;
    for (u32 i = 0; i < total_lines; ++i) {
        u32 idx = (start + i) % ring_lines;
        writer(ring_line(idx));
    }
    spin_unlock_irqrestore(&log_lock, flags);
}
