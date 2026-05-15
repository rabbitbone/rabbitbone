#ifndef RABBITBONE_ARCH_GDT_H
#define RABBITBONE_ARCH_GDT_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define RABBITBONE_KCODE_SEL 0x08u
#define RABBITBONE_KDATA_SEL 0x10u
#define RABBITBONE_UDATA_SEL 0x1bu
#define RABBITBONE_UCODE_SEL 0x23u
#define RABBITBONE_TSS_SEL   0x28u

void gdt_init(void);
void gdt_set_kernel_stack(uptr rsp0);
uptr gdt_current_kernel_stack(void);
uptr gdt_kernel_stack_top(void);
void gdt_set_ist(u8 index, uptr rsp);
uptr gdt_ist_top(u8 index);
bool gdt_selftest(void);
bool gdt_install_dynamic_stacks(usize ring0_size, usize ist_size);

#if defined(__cplusplus)
}
#endif
#endif
