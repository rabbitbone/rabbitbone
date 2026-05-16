#include <rabbitbone/log.h>
#include <rabbitbone/console.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/format.h>
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
static log_level_t serial_min_level = LOG_WARN;
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

static bool log_line_has_attention_marker(const char *line) {
    if (!line) return false;
    return strstr(line, "[ fail  ]") != 0 ||
           strstr(line, "KTEST_STATUS: FAIL") != 0 ||
           strstr(line, "[ ambiguous ]") != 0 ||
           strstr(line, "[ inconclusive ]") != 0 ||
           strstr(line, "[ unknown ]") != 0;
}

bool log_formatted_line_should_emit_serial(const char *formatted_line) {
    if (!formatted_line) return false;
    return strncmp(formatted_line, "[WARN]", 6u) == 0 ||
           strncmp(formatted_line, "[ERROR]", 7u) == 0 ||
           strncmp(formatted_line, "[FATAL]", 7u) == 0 ||
           log_line_has_attention_marker(formatted_line);
}

bool log_line_should_emit_serial(log_level_t level, const char *formatted_line) {
    return level >= serial_min_level || log_formatted_line_should_emit_serial(formatted_line);
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
        strlcpy(dst, src, LOG_HEAP_LINE_LEN);
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

void log_vwrite(log_level_t level, const char *component, const char *fmt, __builtin_va_list ap) {
    u64 flags = spin_lock_irqsave(&log_lock);
    char *dst = ring_line(write_index);
    rabbitbone_buf_out_t line;
    rabbitbone_buf_init(&line, dst, ring_line_len);
    rabbitbone_buf_appendf(&line, "[%s] %s: ", log_level_name(level), component ? component : "kernel");
    rabbitbone_buf_vappendf(&line, fmt ? fmt : "", ap);
    rabbitbone_buf_append_raw(&line, "\n");

    dst[ring_line_len - 1u] = 0;
    write_index = (write_index + 1u) % ring_lines;
    if (total_lines < ring_lines) ++total_lines;
    if (level >= console_min_level) console_write(dst);
    if (log_line_should_emit_serial(level, dst)) serial_write(dst);
    spin_unlock_irqrestore(&log_lock, flags);
}

void log_write(log_level_t level, const char *component, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    log_vwrite(level, component, fmt, ap);
    __builtin_va_end(ap);
}

void log_dump_ring_ctx(log_line_writer_ctx_fn writer, void *ctx) {
    if (!writer) return;
    u64 flags = spin_lock_irqsave(&log_lock);
    u32 start = total_lines == ring_lines ? write_index : 0u;
    for (u32 i = 0; i < total_lines; ++i) {
        u32 idx = (start + i) % ring_lines;
        writer(ring_line(idx), ctx);
    }
    spin_unlock_irqrestore(&log_lock, flags);
}

void log_dump_ring_tail_ctx(log_line_writer_ctx_fn writer, void *ctx, usize max_bytes) {
    if (!writer) return;
    u64 flags = spin_lock_irqsave(&log_lock);
    u32 count = total_lines;
    u32 start = total_lines == ring_lines ? write_index : 0u;
    u32 emit_count = 0;
    usize used = 0;

    if (count != 0 && max_bytes != 0) {
        for (u32 n = 0; n < count; ++n) {
            u32 logical = count - 1u - n;
            u32 idx = (start + logical) % ring_lines;
            usize len = strnlen(ring_line(idx), ring_line_len);
            if (emit_count != 0 && len > max_bytes - used) break;
            if (emit_count == 0 && len > max_bytes) {
                emit_count = 1u;
                break;
            }
            used += len;
            ++emit_count;
            if (used >= max_bytes) break;
        }
    }

    u32 first = count >= emit_count ? count - emit_count : 0u;
    for (u32 i = first; i < count; ++i) {
        u32 idx = (start + i) % ring_lines;
        writer(ring_line(idx), ctx);
    }
    spin_unlock_irqrestore(&log_lock, flags);
}

typedef struct log_dump_compat_ctx {
    log_line_writer_fn writer;
} log_dump_compat_ctx_t;

static void log_dump_compat_line(const char *line, void *ctx) {
    log_dump_compat_ctx_t *compat = (log_dump_compat_ctx_t *)ctx;
    if (compat && compat->writer) compat->writer(line);
}

void log_dump_ring(log_line_writer_fn writer) {
    log_dump_compat_ctx_t compat = { writer };
    log_dump_ring_ctx(log_dump_compat_line, &compat);
}
