#ifndef AURORA_ARCH_GDT_H
#define AURORA_ARCH_GDT_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define AURORA_KCODE_SEL 0x08u
#define AURORA_KDATA_SEL 0x10u
#define AURORA_UDATA_SEL 0x1bu
#define AURORA_UCODE_SEL 0x23u
#define AURORA_TSS_SEL   0x28u

void gdt_init(void);
void gdt_set_kernel_stack(uptr rsp0);
uptr gdt_kernel_stack_top(void);
void gdt_set_ist(u8 index, uptr rsp);
uptr gdt_ist_top(u8 index);
bool gdt_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
