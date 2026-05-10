#ifndef AURORA_PANIC_H
#define AURORA_PANIC_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

AURORA_NORETURN void panic_at(const char *file, int line, const char *fmt, ...);
#define PANIC(fmt, ...) panic_at(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define ASSERT(expr) do { if (!(expr)) PANIC("assertion failed: %s", #expr); } while (0)

#if defined(__cplusplus)
}
#endif
#endif
