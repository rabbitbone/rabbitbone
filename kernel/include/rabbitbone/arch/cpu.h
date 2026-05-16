#ifndef RABBITBONE_ARCH_CPU_H
#define RABBITBONE_ARCH_CPU_H
#include <rabbitbone/types.h>


#define CPU_ARCH_FLAG_FPU        (1ull << 0)
#define CPU_ARCH_FLAG_FXSR       (1ull << 1)
#define CPU_ARCH_FLAG_SSE        (1ull << 2)
#define CPU_ARCH_FLAG_SSE2       (1ull << 3)
#define CPU_ARCH_FLAG_XSAVE      (1ull << 4)
#define CPU_ARCH_FLAG_OSXSAVE    (1ull << 5)
#define CPU_ARCH_FLAG_AVX        (1ull << 6)
#define CPU_ARCH_FLAG_NX         (1ull << 7)
#define CPU_ARCH_FLAG_PAT        (1ull << 8)
#define CPU_ARCH_FLAG_MTRR       (1ull << 9)
#define CPU_ARCH_FLAG_INVARIANT_TSC (1ull << 10)
#define CPU_ARCH_FLAG_FSGSBASE   (1ull << 11)

typedef struct cpu_arch_state {
    u32 initialized;
    u32 family;
    u32 model;
    u32 stepping;
    u32 max_basic_leaf;
    u32 max_extended_leaf;
    u64 features;
    u64 cr0;
    u64 cr4;
    u64 efer;
    u64 pat;
    u64 xcr0;
    u64 mtrr_cap;
    u64 mtrr_default_type;
    u32 xsave_area_size;
    u32 xsave_area_max_size;
    u32 xsave_xcr0_low;
    u32 xsave_xcr0_high;
    u32 fpu_ready;
    u32 sse_ready;
    u32 xsave_ready;
    u32 pat_wc_ready;
    u32 nxe_ready;
    u32 invariant_tsc_ready;
} cpu_arch_state_t;

typedef struct cpu_regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rsi, rdi, rbp, rdx, rcx, rbx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;
} cpu_regs_t;

void cpu_init(void);
u64 cpu_read_xcr0(void);
bool cpu_xsave_enabled(void);
void cpu_capture_arch_state(cpu_arch_state_t *out);
void cpu_dump_regs(const cpu_regs_t *regs);
void cpu_reboot(void);
RABBITBONE_NORETURN void cpu_halt_forever(void);
const char *cpu_exception_name(u64 vector);
u64 cpu_read_efer(void);
bool cpu_nxe_enabled(void);
bool cpu_sse_enabled(void);
void cpu_cpuid(u32 leaf, u32 subleaf, u32 *a, u32 *b, u32 *c, u32 *d);
u64 cpu_read_msr(u32 msr);
void cpu_write_msr(u32 msr, u64 value);
u64 cpu_read_tsc(void);
bool cpu_invariant_tsc_supported(void);

#endif
