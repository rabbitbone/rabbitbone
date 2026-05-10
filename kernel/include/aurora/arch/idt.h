#ifndef AURORA_ARCH_IDT_H
#define AURORA_ARCH_IDT_H
#include <aurora/types.h>
#include <aurora/arch/cpu.h>

typedef void (*interrupt_handler_t)(cpu_regs_t *regs);
void idt_init(void);
void idt_set_handler(u8 vector, interrupt_handler_t handler);
void interrupt_dispatch(cpu_regs_t *regs);

#endif
