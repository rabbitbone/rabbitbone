#ifndef RABBITBONE_PANIC_H
#define RABBITBONE_PANIC_H
#include <rabbitbone/types.h>
#include <rabbitbone/arch/cpu.h>
#if defined(__cplusplus)
extern "C" {
#endif

RABBITBONE_NORETURN void panic_at(const char *file, int line, const char *fmt, ...);
RABBITBONE_NORETURN void panic_at_regs(const char *file, int line, const cpu_regs_t *regs, const char *fmt, ...);
#define PANIC(fmt, ...) panic_at(__FILE__, __LINE__, (fmt), ##__VA_ARGS__)
#define PANIC_REGS(regs, fmt, ...) panic_at_regs(__FILE__, __LINE__, (regs), (fmt), ##__VA_ARGS__)
#define ASSERT(expr) do { if (!(expr)) PANIC("assertion failed: %s", #expr); } while (0)

#if defined(__cplusplus)
}
#endif
#endif
