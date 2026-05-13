#ifndef AURORA_LOG_H
#define AURORA_LOG_H
#include <aurora/types.h>
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
void log_write(log_level_t level, const char *component, const char *fmt, ...);
void log_dump_ring(void (*writer)(const char *line));
const char *log_level_name(log_level_t level);

#if defined(AURORA_HOST_TEST)
#define KLOG(level, component, fmt, ...) ((void)0)
#else
#define KLOG(level, component, fmt, ...) log_write((level), (component), (fmt), ##__VA_ARGS__)
#endif

#if defined(__cplusplus)
}
#endif
#endif
