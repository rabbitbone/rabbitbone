#ifndef AURORA_ARCH_CPU_H
#define AURORA_ARCH_CPU_H
#include <aurora/types.h>

typedef struct cpu_regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rsi, rdi, rbp, rdx, rcx, rbx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;
} cpu_regs_t;

void cpu_init(void);
void cpu_dump_regs(const cpu_regs_t *regs);
void cpu_reboot(void);
AURORA_NORETURN void cpu_halt_forever(void);
const char *cpu_exception_name(u64 vector);
u64 cpu_read_efer(void);
bool cpu_nxe_enabled(void);
bool cpu_sse_enabled(void);

#endif
