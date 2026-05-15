#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/kmem.h>

#define GDT_ENTRIES 7u
#define KERNEL_PRIV_STACK_SIZE 12288u
#define IST_STACK_SIZE 1024u
#define EARLY_KERNEL_RSP0 0x1e0000ull
#define EARLY_IST1_TOP 0x1df000ull
#define EARLY_IST2_TOP 0x1de000ull
#define EARLY_IST3_TOP 0x1dd000ull

typedef struct RABBITBONE_PACKED gdtr {
    u16 limit;
    u64 base;
} gdtr_t;

typedef struct RABBITBONE_PACKED tss64 {
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
static void *dynamic_kernel_stack;
static void *dynamic_ist_stacks[3];
static usize dynamic_kernel_stack_size;
static usize dynamic_ist_stack_size;
static bool dynamic_stacks_installed;

static uptr align_down16(uptr v) { return v & ~(uptr)0xfull; }

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

uptr gdt_current_kernel_stack(void) {
    return tss.rsp0;
}

uptr gdt_kernel_stack_top(void) {
    return align_down16((uptr)EARLY_KERNEL_RSP0);
}

uptr gdt_ist_top(u8 index) {
    switch (index) {
        case 1: return align_down16((uptr)EARLY_IST1_TOP);
        case 2: return align_down16((uptr)EARLY_IST2_TOP);
        case 3: return align_down16((uptr)EARLY_IST3_TOP);
        default: return 0;
    }
}

void gdt_set_ist(u8 index, uptr rsp) {
    switch (index) {
        case 1: tss.ist1 = rsp; break;
        case 2: tss.ist2 = rsp; break;
        case 3: tss.ist3 = rsp; break;
        default: break;
    }
}


bool gdt_install_dynamic_stacks(usize ring0_size, usize ist_size) {
    if (dynamic_stacks_installed) return true;
    if (ring0_size < KERNEL_PRIV_STACK_SIZE || ist_size < IST_STACK_SIZE) return false;
    void *ring0 = kmalloc(ring0_size);
    if (!ring0) return false;
    memset(ring0, 0, ring0_size);
    void *ist0 = kmalloc(ist_size);
    void *ist1 = kmalloc(ist_size);
    void *ist2 = kmalloc(ist_size);
    if (!ist0 || !ist1 || !ist2) {
        if (ist0) kfree(ist0);
        if (ist1) kfree(ist1);
        if (ist2) kfree(ist2);
        kfree(ring0);
        return false;
    }
    memset(ist0, 0, ist_size);
    memset(ist1, 0, ist_size);
    memset(ist2, 0, ist_size);
    dynamic_kernel_stack = ring0;
    dynamic_ist_stacks[0] = ist0;
    dynamic_ist_stacks[1] = ist1;
    dynamic_ist_stacks[2] = ist2;
    dynamic_kernel_stack_size = ring0_size;
    dynamic_ist_stack_size = ist_size;
    dynamic_stacks_installed = true;
    gdt_set_kernel_stack(align_down16((uptr)ring0 + ring0_size));
    gdt_set_ist(1, align_down16((uptr)ist0 + ist_size));
    gdt_set_ist(2, align_down16((uptr)ist1 + ist_size));
    gdt_set_ist(3, align_down16((uptr)ist2 + ist_size));
    KLOG(LOG_INFO, "gdt", "installed heap-backed stacks rsp0=%p ist=%llu", (void *)tss.rsp0, (unsigned long long)ist_size);
    return true;
}

void gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));
    bool have_dynamic_stacks = dynamic_kernel_stack && dynamic_kernel_stack_size && dynamic_ist_stack_size &&
                               dynamic_ist_stacks[0] && dynamic_ist_stacks[1] && dynamic_ist_stacks[2];
    dynamic_stacks_installed = have_dynamic_stacks;

    gdt[1] = 0x00af9b000000ffffull;
    gdt[2] = 0x00cf93000000ffffull;
    gdt[3] = 0x00cff3000000ffffull;
    gdt[4] = 0x00affb000000ffffull;

    tss.iopb_offset = sizeof(tss64_t);
    if (have_dynamic_stacks) {
        gdt_set_kernel_stack(align_down16((uptr)dynamic_kernel_stack + dynamic_kernel_stack_size));
        gdt_set_ist(1, align_down16((uptr)dynamic_ist_stacks[0] + dynamic_ist_stack_size));
        gdt_set_ist(2, align_down16((uptr)dynamic_ist_stacks[1] + dynamic_ist_stack_size));
        gdt_set_ist(3, align_down16((uptr)dynamic_ist_stacks[2] + dynamic_ist_stack_size));
    } else {
        gdt_set_kernel_stack(gdt_kernel_stack_top());
        gdt_set_ist(1, gdt_ist_top(1));
        gdt_set_ist(2, gdt_ist_top(2));
        gdt_set_ist(3, gdt_ist_top(3));
    }
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
