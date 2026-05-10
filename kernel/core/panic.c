#include <aurora/panic.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/arch/io.h>

AURORA_NORETURN void panic_at(const char *file, int line, const char *fmt, ...) {
    cpu_cli();
    char msg[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);

    console_set_color(15, 4);
    console_write("\n*** AURORA KERNEL PANIC ***\n");
    kprintf("at %s:%d\n%s\n", file, line, msg);
    log_write(LOG_FATAL, "panic", "%s", msg);
    console_write("System halted.\n");
    for (;;) cpu_hlt();
}

AURORA_NORETURN void aurora_rust_panic(const char *msg) {
    PANIC("rust panic: %s", msg ? msg : "unknown");
}
