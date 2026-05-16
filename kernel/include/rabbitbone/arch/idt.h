#ifndef RABBITBONE_ARCH_IDT_H
#define RABBITBONE_ARCH_IDT_H
#include <rabbitbone/types.h>
#include <rabbitbone/arch/cpu.h>

typedef void (*interrupt_handler_t)(cpu_regs_t *regs);
void idt_init(void);
void idt_load(void);
void idt_set_handler(u8 vector, interrupt_handler_t handler);
void interrupt_dispatch(cpu_regs_t *regs);

#endif
