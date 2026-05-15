#include <rabbitbone/panic.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/drivers.h>

#define PANIC_MSG_BUF_SIZE 512u
#define PANIC_SERIAL_CHUNK_SIZE 256u

static volatile u32 g_panic_active;
static char g_panic_msg[PANIC_MSG_BUF_SIZE];
static char g_panic_serial_chunk[PANIC_SERIAL_CHUNK_SIZE];

static RABBITBONE_NORETURN void panic_halt_forever(void) {
    cpu_cli();
    for (;;) cpu_hlt();
}

static void panic_serial_line(const char *s) {
    if (!s) return;
    serial_write(s);
}

static void panic_serial_emit(const char *s, usize n, void *ctx) {
    (void)ctx;
    if (s && n) serial_write_n(s, n);
}

static void panic_serial_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    (void)kvprintf_emit_buffered(panic_serial_emit, 0, g_panic_serial_chunk, sizeof(g_panic_serial_chunk), fmt, ap);
    __builtin_va_end(ap);
}

static const char *panic_format_message(const char *fmt, __builtin_va_list ap) {
    if (!fmt) fmt = "panic";
    kvsnprintf(g_panic_msg, sizeof(g_panic_msg), fmt, ap);
    g_panic_msg[sizeof(g_panic_msg) - 1u] = 0;
    return g_panic_msg;
}

static RABBITBONE_NORETURN void panic_reentered(const char *msg) {
    serial_write("\n*** RABBITBONE KERNEL PANIC REENTERED ***\n");
    console_write("\n*** RABBITBONE KERNEL PANIC REENTERED ***\n");
    if (msg) {
        serial_write(msg);
        serial_write("\n");
        console_write(msg);
        console_write("\n");
    }
    panic_halt_forever();
}

static bool panic_try_claim(void) {
    cpu_cli();
    return __sync_lock_test_and_set(&g_panic_active, 1u) == 0;
}

static void panic_write_u64_console(const char *name, u64 value) {
    kprintf("%s=%p ", name, (void *)(uptr)value);
}

static void panic_write_u64_serial(const char *name, u64 value) {
    panic_serial_printf("%s=%p ", name, (void *)(uptr)value);
}

static void panic_dump_regs_console(const cpu_regs_t *r) {
    if (!r) return;
    kprintf("vector=%llu error=%llx cr2=%p cr3=%p\n",
            (unsigned long long)r->vector,
            (unsigned long long)r->error,
            (void *)(uptr)read_cr2(),
            (void *)(uptr)read_cr3());
    panic_write_u64_console("rip", r->rip);
    panic_write_u64_console("cs", r->cs);
    panic_write_u64_console("rflags", r->rflags);
    panic_write_u64_console("rsp", r->rsp);
    panic_write_u64_console("ss", r->ss);
    console_write("\n");
    panic_write_u64_console("rax", r->rax);
    panic_write_u64_console("rbx", r->rbx);
    panic_write_u64_console("rcx", r->rcx);
    panic_write_u64_console("rdx", r->rdx);
    console_write("\n");
    panic_write_u64_console("rsi", r->rsi);
    panic_write_u64_console("rdi", r->rdi);
    panic_write_u64_console("rbp", r->rbp);
    console_write("\n");
    panic_write_u64_console("r8", r->r8);
    panic_write_u64_console("r9", r->r9);
    panic_write_u64_console("r10", r->r10);
    panic_write_u64_console("r11", r->r11);
    console_write("\n");
    panic_write_u64_console("r12", r->r12);
    panic_write_u64_console("r13", r->r13);
    panic_write_u64_console("r14", r->r14);
    panic_write_u64_console("r15", r->r15);
    console_write("\n");
}

static void panic_dump_regs_serial(const cpu_regs_t *r) {
    if (!r) return;
    panic_serial_printf("vector=%llu error=%llx cr2=%p cr3=%p\n",
                        (unsigned long long)r->vector,
                        (unsigned long long)r->error,
                        (void *)(uptr)read_cr2(),
                        (void *)(uptr)read_cr3());
    panic_write_u64_serial("rip", r->rip);
    panic_write_u64_serial("cs", r->cs);
    panic_write_u64_serial("rflags", r->rflags);
    panic_write_u64_serial("rsp", r->rsp);
    panic_write_u64_serial("ss", r->ss);
    panic_serial_line("\n");
    panic_write_u64_serial("rax", r->rax);
    panic_write_u64_serial("rbx", r->rbx);
    panic_write_u64_serial("rcx", r->rcx);
    panic_write_u64_serial("rdx", r->rdx);
    panic_serial_line("\n");
    panic_write_u64_serial("rsi", r->rsi);
    panic_write_u64_serial("rdi", r->rdi);
    panic_write_u64_serial("rbp", r->rbp);
    panic_serial_line("\n");
    panic_write_u64_serial("r8", r->r8);
    panic_write_u64_serial("r9", r->r9);
    panic_write_u64_serial("r10", r->r10);
    panic_write_u64_serial("r11", r->r11);
    panic_serial_line("\n");
    panic_write_u64_serial("r12", r->r12);
    panic_write_u64_serial("r13", r->r13);
    panic_write_u64_serial("r14", r->r14);
    panic_write_u64_serial("r15", r->r15);
    panic_serial_line("\n");
}

static RABBITBONE_NORETURN void panic_emit_claimed(const char *file, int line, const cpu_regs_t *regs, const char *msg) {
    panic_serial_printf("\n========== RABBITBONE KERNEL PANIC ==========" "\n");
    panic_serial_printf("theme=%s at %s:%d\n", console_theme_name(console_theme()), file ? file : "?", line);
    if (msg) panic_serial_printf("message=%s\n", msg);
    panic_dump_regs_serial(regs);
    panic_serial_line("--- kernel log ring ---\n");
    log_dump_ring(panic_serial_line);
    panic_serial_line("--- end panic log, system halted ---\n");

    console_panic_begin();
    console_write("================================================================================\n");
    console_write("                            RABBITBONE KERNEL PANIC                                 \n");
    console_write("================================================================================\n\n");
    kprintf("Location: %s:%d\n", file ? file : "?", line);
    kprintf("Theme: %s\n", console_theme_name(console_theme()));
    if (msg) {
        console_write("Message: ");
        console_write(msg);
        console_write("\n\n");
        kprintf("[FATAL] panic: %s\n", msg);
    }
    panic_dump_regs_console(regs);
    console_write("\nFull panic record and kernel log were written to COM1 / VMware rabbitbone-com1.log.\n");
    console_write("System halted.\n");
    panic_halt_forever();
}

RABBITBONE_NORETURN void panic_at(const char *file, int line, const char *fmt, ...) {
    if (!panic_try_claim()) panic_reentered(0);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *msg = panic_format_message(fmt, ap);
    __builtin_va_end(ap);
    panic_emit_claimed(file, line, 0, msg);
}

RABBITBONE_NORETURN void panic_at_regs(const char *file, int line, const cpu_regs_t *regs, const char *fmt, ...) {
    if (!panic_try_claim()) panic_reentered(0);
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    const char *msg = panic_format_message(fmt, ap);
    __builtin_va_end(ap);
    panic_emit_claimed(file, line, regs, msg);
}

RABBITBONE_NORETURN void rabbitbone_rust_panic(const char *msg) {
    PANIC("rust panic: %s", msg ? msg : "unknown");
}
