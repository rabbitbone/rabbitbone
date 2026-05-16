#ifndef RABBITBONE_LOG_H
#define RABBITBONE_LOG_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef enum log_level {
    LOG_TRACE = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_FATAL = 4,
} log_level_t;

void log_init(void);
void log_enable_heap_ring(void);
void log_vwrite(log_level_t level, const char *component, const char *fmt, __builtin_va_list ap);
void log_write(log_level_t level, const char *component, const char *fmt, ...);
typedef void (*log_line_writer_fn)(const char *line);
typedef void (*log_line_writer_ctx_fn)(const char *line, void *ctx);
void log_dump_ring_ctx(log_line_writer_ctx_fn writer, void *ctx);
void log_dump_ring_tail_ctx(log_line_writer_ctx_fn writer, void *ctx, usize max_bytes);
void log_dump_ring(log_line_writer_fn writer);
const char *log_level_name(log_level_t level);
bool log_formatted_line_should_emit_serial(const char *formatted_line);
bool log_line_should_emit_serial(log_level_t level, const char *formatted_line);

#define KLOG(level, component, fmt, ...) log_write((level), (component), (fmt), ##__VA_ARGS__)

#if defined(__cplusplus)
}
#endif
#endif
