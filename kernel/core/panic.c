#include <aurora/panic.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/arch/io.h>
#include <aurora/libc.h>

static volatile u32 g_panic_active;

static AURORA_NORETURN void panic_halt_forever(void) {
    cpu_cli();
    for (;;) cpu_hlt();
}

static void panic_write_u64(const char *name, u64 value) {
    kprintf("%s=%p ", name, (void *)(uptr)value);
}

static void panic_dump_regs(const cpu_regs_t *r) {
    if (!r) return;
    kprintf("vector=%llu error=%llx cr2=%p cr3=%p\n",
            (unsigned long long)r->vector,
            (unsigned long long)r->error,
            (void *)(uptr)read_cr2(),
            (void *)(uptr)read_cr3());
    panic_write_u64("rip", r->rip);
    panic_write_u64("cs", r->cs);
    panic_write_u64("rflags", r->rflags);
    panic_write_u64("rsp", r->rsp);
    panic_write_u64("ss", r->ss);
    console_write("\n");
    panic_write_u64("rax", r->rax);
    panic_write_u64("rbx", r->rbx);
    panic_write_u64("rcx", r->rcx);
    panic_write_u64("rdx", r->rdx);
    console_write("\n");
    panic_write_u64("rsi", r->rsi);
    panic_write_u64("rdi", r->rdi);
    panic_write_u64("rbp", r->rbp);
    console_write("\n");
    panic_write_u64("r8", r->r8);
    panic_write_u64("r9", r->r9);
    panic_write_u64("r10", r->r10);
    panic_write_u64("r11", r->r11);
    console_write("\n");
    panic_write_u64("r12", r->r12);
    panic_write_u64("r13", r->r13);
    panic_write_u64("r14", r->r14);
    panic_write_u64("r15", r->r15);
    console_write("\n");
}

static AURORA_NORETURN void panic_emit(const char *file, int line, const cpu_regs_t *regs, const char *msg) {
    cpu_cli();
    if (__sync_lock_test_and_set(&g_panic_active, 1u) != 0) {
        console_write("\n*** AURORA KERNEL PANIC REENTERED ***\n");
        if (msg) {
            console_write(msg);
            console_write("\n");
        }
        panic_halt_forever();
    }

    console_set_color(15, 4);
    console_write("\n*** AURORA KERNEL PANIC ***\n");
    kprintf("at %s:%d\n", file ? file : "?", line);
    if (msg) {
        console_write(msg);
        console_write("\n");
        kprintf("[FATAL] panic: %s\n", msg);
    }
    panic_dump_regs(regs);
    console_write("System halted.\n");
    panic_halt_forever();
}

AURORA_NORETURN void panic_at(const char *file, int line, const char *fmt, ...) {
    char msg[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    panic_emit(file, line, 0, msg);
}

AURORA_NORETURN void panic_at_regs(const char *file, int line, const cpu_regs_t *regs, const char *fmt, ...) {
    char msg[512];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(msg, sizeof(msg), fmt, ap);
    __builtin_va_end(ap);
    panic_emit(file, line, regs, msg);
}

AURORA_NORETURN void aurora_rust_panic(const char *msg) {
    PANIC("rust panic: %s", msg ? msg : "unknown");
}
