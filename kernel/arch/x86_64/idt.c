#include <aurora/arch/idt.h>
#include <aurora/arch/io.h>
#include <aurora/arch/gdt.h>
#include <aurora/console.h>
#include <aurora/drivers.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/libc.h>
#include <aurora/process.h>
#include <aurora/syscall.h>
#include <aurora/scheduler.h>

typedef struct AURORA_PACKED idt_entry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
} idt_entry_t;

typedef struct AURORA_PACKED idt_ptr {
    u16 limit;
    u64 base;
} idt_ptr_t;

static idt_entry_t idt[256];
static interrupt_handler_t handlers[256];

#define DECL_ISR(n) extern void isr##n(void)
DECL_ISR(0); DECL_ISR(1); DECL_ISR(2); DECL_ISR(3); DECL_ISR(4); DECL_ISR(5); DECL_ISR(6); DECL_ISR(7);
DECL_ISR(8); DECL_ISR(9); DECL_ISR(10); DECL_ISR(11); DECL_ISR(12); DECL_ISR(13); DECL_ISR(14); DECL_ISR(15);
DECL_ISR(16); DECL_ISR(17); DECL_ISR(18); DECL_ISR(19); DECL_ISR(20); DECL_ISR(21); DECL_ISR(22); DECL_ISR(23);
DECL_ISR(24); DECL_ISR(25); DECL_ISR(26); DECL_ISR(27); DECL_ISR(28); DECL_ISR(29); DECL_ISR(30); DECL_ISR(31);
DECL_ISR(32); DECL_ISR(33); DECL_ISR(34); DECL_ISR(35); DECL_ISR(36); DECL_ISR(37); DECL_ISR(38); DECL_ISR(39);
DECL_ISR(40); DECL_ISR(41); DECL_ISR(42); DECL_ISR(43); DECL_ISR(44); DECL_ISR(45); DECL_ISR(46); DECL_ISR(47);
DECL_ISR(128);

static void idt_set_gate_ist(u8 vector, void (*isr)(void), u8 flags, u8 ist) {
    u64 addr = (u64)(uptr)isr;
    idt[vector].offset_low = (u16)(addr & 0xffff);
    idt[vector].selector = 0x08;
    idt[vector].ist = ist & 0x7u;
    idt[vector].type_attr = flags;
    idt[vector].offset_mid = (u16)((addr >> 16) & 0xffff);
    idt[vector].offset_high = (u32)((addr >> 32) & 0xffffffff);
    idt[vector].zero = 0;
}

static void idt_set_gate(u8 vector, void (*isr)(void), u8 flags) { idt_set_gate_ist(vector, isr, flags, 0); }

static bool regs_from_user(const cpu_regs_t *regs) { return (regs->cs & 3u) == 3u; }

static void default_exception(cpu_regs_t *regs) {
    if (regs_from_user(regs) && process_user_active()) {
        u64 cr2 = regs->vector == 14 ? read_cr2() : 0;
        if (regs->vector == 14 && process_try_resolve_cow_fault(regs, cr2)) return;
        if (process_async_scheduler_active()) process_fault_current_from_interrupt(regs, regs->vector, cr2);
        else process_fault_from_interrupt(regs->vector, regs->rip, cr2);
        return;
    }
    if (regs->vector == 14) {
        PANIC_REGS(regs, "exception %llu %s rip=%p err=%llx cr2=%p",
              (unsigned long long)regs->vector,
              cpu_exception_name(regs->vector),
              (void *)(uptr)regs->rip,
              (unsigned long long)regs->error,
              (void *)(uptr)read_cr2());
    }
    PANIC_REGS(regs, "exception %llu %s rip=%p err=%llx",
          (unsigned long long)regs->vector,
          cpu_exception_name(regs->vector),
          (void *)(uptr)regs->rip,
          (unsigned long long)regs->error);
}

static void irq_dispatch(cpu_regs_t *regs) {
    u8 irq = (u8)(regs->vector - 32);
    bool do_preempt = false;
    if (irq == 0) { pit_irq(); do_preempt = scheduler_tick(regs); }
    else if (irq == 1) keyboard_irq();
    pic_send_eoi(irq);
    if (do_preempt && process_async_scheduler_active()) process_preempt_from_interrupt(regs);
}

void idt_set_handler(u8 vector, interrupt_handler_t handler) { handlers[vector] = handler; }

static void syscall_int80(cpu_regs_t *regs) {
    const u64 no = regs->rax;
    const u64 a0 = regs->rdi;

    if (no == AURORA_SYS_EXIT && regs_from_user(regs) && process_user_active()) {
        process_exit_current_from_syscall(regs, (i32)a0);
        return;
    }

    if (no == AURORA_SYS_SIGRETURN && regs_from_user(regs) && process_user_active()) {
        bool restored = process_signal_return((uptr)a0, regs);
        if (!restored) {
            regs->rax = (u64)-1ll;
            regs->rdx = (u64)-1ll;
        }
        (void)process_after_syscall(regs);
        return;
    }

    syscall_result_t r = syscall_dispatch(no, a0, regs->rsi, regs->rdx, regs->r10, regs->r8, regs->r9);
    regs->rax = (u64)r.value;
    regs->rdx = (u64)r.error;
    (void)process_after_syscall(regs);
}

void interrupt_dispatch(cpu_regs_t *regs) {
    if (regs->vector < 256 && handlers[regs->vector]) {
        handlers[regs->vector](regs);
        if (regs->vector >= 32 && regs->vector < 48) pic_send_eoi((u8)(regs->vector - 32));
        return;
    }
    if (regs->vector < 32) default_exception(regs);
    else if (regs->vector >= 32 && regs->vector < 48) irq_dispatch(regs);
    else KLOG(LOG_WARN, "idt", "unhandled vector %llu", (unsigned long long)regs->vector);
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));
    void (*isrs[48])(void) = {
        isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
        isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31,
        isr32,isr33,isr34,isr35,isr36,isr37,isr38,isr39,isr40,isr41,isr42,isr43,isr44,isr45,isr46,isr47
    };
    for (u8 i = 0; i < 48; ++i) idt_set_gate(i, isrs[i], 0x8e);
    idt_set_gate_ist(2, isr2, 0x8e, 1);
    idt_set_gate_ist(8, isr8, 0x8e, 1);
    idt_set_gate_ist(14, isr14, 0x8e, 2);
    idt_set_gate_ist(18, isr18, 0x8e, 3);
    idt_set_gate(128, isr128, 0xee);
    idt_set_handler(128, syscall_int80);
    idt_ptr_t ptr = { .limit = sizeof(idt) - 1, .base = (u64)(uptr)idt };
    __asm__ volatile("lidt %0" : : "m"(ptr));
    KLOG(LOG_INFO, "idt", "loaded");
}
