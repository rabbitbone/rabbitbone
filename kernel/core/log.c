#include <aurora/log.h>
#include <aurora/console.h>
#include <aurora/libc.h>
#include <aurora/spinlock.h>

#define LOG_LINES 32u
#define LOG_LINE_LEN 192u

static char ring[LOG_LINES][LOG_LINE_LEN];
static u32 write_index;
static u32 total_lines;
static spinlock_t log_lock;

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
    write_index = 0;
    total_lines = 0;
    for (usize i = 0; i < LOG_LINES; ++i) ring[i][0] = 0;
}

void log_write(log_level_t level, const char *component, const char *fmt, ...) {
    char msg[128];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);

    char line[LOG_LINE_LEN];
    ksnprintf(line, sizeof(line), "[%s] %s: %s\n", log_level_name(level), component ? component : "kernel", msg);
    u64 flags = spin_lock_irqsave(&log_lock);
    strncpy(ring[write_index], line, LOG_LINE_LEN - 1);
    ring[write_index][LOG_LINE_LEN - 1] = 0;
    write_index = (write_index + 1) % LOG_LINES;
    if (total_lines < LOG_LINES) ++total_lines;
    console_write(line);
    spin_unlock_irqrestore(&log_lock, flags);
}

void log_dump_ring(void (*writer)(const char *line)) {
    if (!writer) return;
    u64 flags = spin_lock_irqsave(&log_lock);
    u32 start = total_lines == LOG_LINES ? write_index : 0;
    for (u32 i = 0; i < total_lines; ++i) {
        u32 idx = (start + i) % LOG_LINES;
        writer(ring[idx]);
    }
    spin_unlock_irqrestore(&log_lock, flags);
}
