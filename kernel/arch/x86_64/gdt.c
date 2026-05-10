#include <aurora/arch/gdt.h>
#include <aurora/libc.h>
#include <aurora/log.h>

#define GDT_ENTRIES 7u
#define KERNEL_PRIV_STACK_SIZE 28672u
#define IST_STACK_SIZE 2048u

typedef struct AURORA_PACKED gdtr {
    u16 limit;
    u64 base;
} gdtr_t;

typedef struct AURORA_PACKED tss64 {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iopb_offset;
} tss64_t;

static u64 gdt[GDT_ENTRIES];
static tss64_t tss;
static u8 kernel_priv_stack[KERNEL_PRIV_STACK_SIZE] __attribute__((aligned(16)));
static u8 ist_stacks[3][IST_STACK_SIZE] __attribute__((aligned(16)));

static u64 make_tss_low(uptr base, u32 limit) {
    u64 desc = 0;
    desc |= (u64)(limit & 0xffffu);
    desc |= (u64)(base & 0xffffffu) << 16;
    desc |= 0x89ull << 40;
    desc |= (u64)((limit >> 16) & 0x0fu) << 48;
    desc |= (u64)((base >> 24) & 0xffu) << 56;
    return desc;
}

void gdt_set_kernel_stack(uptr rsp0) {
    tss.rsp0 = rsp0;
}

uptr gdt_kernel_stack_top(void) {
    return (uptr)(kernel_priv_stack + sizeof(kernel_priv_stack));
}

uptr gdt_ist_top(u8 index) {
    if (index == 0 || index > 3) return 0;
    return (uptr)(ist_stacks[index - 1u] + IST_STACK_SIZE);
}

void gdt_set_ist(u8 index, uptr rsp) {
    switch (index) {
        case 1: tss.ist1 = rsp; break;
        case 2: tss.ist2 = rsp; break;
        case 3: tss.ist3 = rsp; break;
        default: break;
    }
}

void gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt[1] = 0x00af9b000000ffffull;
    gdt[2] = 0x00cf93000000ffffull;
    gdt[3] = 0x00cff3000000ffffull;
    gdt[4] = 0x00affb000000ffffull;

    tss.iopb_offset = sizeof(tss64_t);
    gdt_set_kernel_stack(gdt_kernel_stack_top());
    gdt_set_ist(1, gdt_ist_top(1));
    gdt_set_ist(2, gdt_ist_top(2));
    gdt_set_ist(3, gdt_ist_top(3));
    uptr base = (uptr)&tss;
    u32 limit = (u32)(sizeof(tss64_t) - 1u);
    gdt[5] = make_tss_low(base, limit);
    gdt[6] = (u64)(base >> 32);

    gdtr_t gdtr = { .limit = (u16)(sizeof(gdt) - 1u), .base = (u64)(uptr)gdt };
    __asm__ volatile(
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        "movw $0x28, %%ax\n"
        "ltr %%ax\n"
        :
        : "m"(gdtr)
        : "rax", "memory");
    KLOG(LOG_INFO, "gdt", "loaded kernel/user segments tss=%p rsp0=%p", &tss, (void *)tss.rsp0);
}

bool gdt_selftest(void) {
    if (gdt[1] != 0x00af9b000000ffffull) return false;
    if (gdt[2] != 0x00cf93000000ffffull) return false;
    if (gdt[3] != 0x00cff3000000ffffull) return false;
    if (gdt[4] != 0x00affb000000ffffull) return false;
    if ((gdt[5] & (0xffull << 40)) != (0x89ull << 40) && (gdt[5] & (0xffull << 40)) != (0x8bull << 40)) return false;
    if (tss.rsp0 == 0 || (tss.rsp0 & 0xfu) != 0) return false;
    if (tss.ist1 == 0 || tss.ist2 == 0 || tss.ist3 == 0) return false;
    if ((tss.ist1 & 0xfu) != 0 || (tss.ist2 & 0xfu) != 0 || (tss.ist3 & 0xfu) != 0) return false;
    if (tss.iopb_offset != sizeof(tss64_t)) return false;
    return true;
}
