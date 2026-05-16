#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/smp.h>
#include <rabbitbone/vmm.h>

#define GDT_ENTRIES 7u
#define KERNEL_PRIV_STACK_SIZE 12288u
#define IST_STACK_SIZE 1024u
#define GDT_STACK_CANARY 0x7262737461636b5aull
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

typedef struct gdt_guarded_stack {
    void *allocation_base;
    usize allocation_pages;
    uptr guard_low;
    uptr usable_base;
    uptr usable_top;
    uptr guard_high;
    usize usable_size;
    bool guard_low_unmapped;
    bool guard_high_unmapped;
}
gdt_guarded_stack_t;

typedef struct gdt_cpu_state {
    u64 gdt[GDT_ENTRIES];
    tss64_t tss;
    void *dynamic_kernel_stack;
    void *dynamic_ist_stacks[3];
    usize dynamic_kernel_stack_size;
    usize dynamic_ist_stack_size;
    gdt_guarded_stack_t ring0_region;
    gdt_guarded_stack_t ist_regions[3];
    bool dynamic_stacks_installed;
    bool loaded;
}
gdt_cpu_state_t;

static gdt_cpu_state_t gdt_states[SMP_MAX_CPUS];

static uptr align_down16(uptr v) { return v & ~(uptr)0xfull; }

static u32 gdt_cpu_slot(void) {
    u32 id = smp_current_cpu_id();
    return id < SMP_MAX_CPUS ? id : 0u;
}

static gdt_cpu_state_t *gdt_current_state(void) {
    return &gdt_states[gdt_cpu_slot()];
}

static bool gdt_stack_top_checked(void *base, usize size, uptr *top_out) {
    if (!base || !top_out) return false;
    uptr sum = 0;
    if (__builtin_add_overflow((uptr)base, (uptr)size, &sum)) return false;
    *top_out = align_down16(sum);
    return *top_out != 0;
}

static usize gdt_pages_for_size(usize size) {
    if (size == 0) return 0;
    usize pages = (size + (usize)PAGE_SIZE - 1u) / (usize)PAGE_SIZE;
    return pages ? pages : 1u;
}

static bool gdt_stack_region_canary_ok(const gdt_guarded_stack_t *region) {
    if (!region || !region->usable_base || region->usable_size < sizeof(u64)) return false;
    const volatile u64 *canary = (const volatile u64 *)(uptr)region->usable_base;
    return *canary == GDT_STACK_CANARY;
}

static bool gdt_stack_guard_unmapped(uptr addr) {
    uptr phys = 0;
    u64 flags = 0;
    return addr != 0 && !vmm_translate(addr, &phys, &flags);
}

static bool gdt_stack_region_guarded(const gdt_guarded_stack_t *region) {
    return region && region->allocation_base && region->allocation_pages >= 3u &&
           region->guard_low && region->guard_high && region->usable_base && region->usable_top &&
           region->guard_low_unmapped && region->guard_high_unmapped &&
           gdt_stack_guard_unmapped(region->guard_low) && gdt_stack_guard_unmapped(region->guard_high) &&
           gdt_stack_region_canary_ok(region);
}

static bool gdt_allocate_guarded_stack(usize requested_size, gdt_guarded_stack_t *out) {
    if (!out || requested_size == 0) return false;
    memset(out, 0, sizeof(*out));
    usize usable_pages = gdt_pages_for_size(requested_size);
    if (usable_pages == 0 || usable_pages > ((usize)-1 / (usize)PAGE_SIZE) - 2u) return false;
    usize total_pages = usable_pages + 2u;
    void *alloc = memory_alloc_contiguous_pages_below(total_pages, MEMORY_KERNEL_DIRECT_LIMIT);
    if (!alloc) return false;
    uptr base = (uptr)alloc;
    uptr usable_base = base + PAGE_SIZE;
    uptr guard_high = usable_base + usable_pages * (uptr)PAGE_SIZE;
    usize usable_size = usable_pages * (usize)PAGE_SIZE;
    memset((void *)usable_base, 0, usable_size);
    *(volatile u64 *)(uptr)usable_base = GDT_STACK_CANARY;
    bool low_unmapped = vmm_unmap_4k(base);
    bool high_unmapped = vmm_unmap_4k(guard_high);
    out->allocation_base = alloc;
    out->allocation_pages = total_pages;
    out->guard_low = base;
    out->usable_base = usable_base;
    out->usable_top = align_down16(guard_high);
    out->guard_high = guard_high;
    out->usable_size = usable_size;
    out->guard_low_unmapped = low_unmapped;
    out->guard_high_unmapped = high_unmapped;
    if (!low_unmapped || !high_unmapped) {
        KLOG(LOG_WARN, "gdt", "cpu%u stack guard fallback base=%p low=%u high=%u", gdt_cpu_slot(), (void *)base, low_unmapped ? 1u : 0u, high_unmapped ? 1u : 0u);
    }
    return out->usable_top != 0 && out->usable_base < out->usable_top;
}

static bool gdt_all_regions_guarded(const gdt_cpu_state_t *state) {
    if (!state || !state->dynamic_stacks_installed) return false;
    if (!gdt_stack_region_guarded(&state->ring0_region)) return false;
    for (u32 i = 0; i < 3u; ++i) if (!gdt_stack_region_guarded(&state->ist_regions[i])) return false;
    return true;
}

static bool gdt_fill_stack_info(u32 cpu_id, gdt_stack_info_t *out) {
    if (!out || cpu_id >= SMP_MAX_CPUS) return false;
    const gdt_cpu_state_t *state = &gdt_states[cpu_id];
    memset(out, 0, sizeof(*out));
    out->cpu_id = cpu_id;
    out->loaded = state->loaded;
    out->dynamic_stacks_installed = state->dynamic_stacks_installed;
    out->guard_pages_installed = gdt_all_regions_guarded(state);
    out->canaries_ok = state->dynamic_stacks_installed && gdt_stack_region_canary_ok(&state->ring0_region) &&
                       gdt_stack_region_canary_ok(&state->ist_regions[0]) &&
                       gdt_stack_region_canary_ok(&state->ist_regions[1]) &&
                       gdt_stack_region_canary_ok(&state->ist_regions[2]);
    out->ring0_guard_low = state->ring0_region.guard_low;
    out->ring0_base = state->ring0_region.usable_base;
    out->ring0_top = state->ring0_region.usable_top;
    out->ring0_guard_high = state->ring0_region.guard_high;
    out->ring0_size = state->ring0_region.usable_size;
    out->ist_size = state->dynamic_ist_stack_size;
    for (u32 i = 0; i < 3u; ++i) {
        out->ist_guard_low[i] = state->ist_regions[i].guard_low;
        out->ist_base[i] = state->ist_regions[i].usable_base;
        out->ist_top[i] = state->ist_regions[i].usable_top;
        out->ist_guard_high[i] = state->ist_regions[i].guard_high;
    }
    return true;
}

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
    gdt_current_state()->tss.rsp0 = rsp0;
}

uptr gdt_current_kernel_stack(void) {
    return gdt_current_state()->tss.rsp0;
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
    tss64_t *tss = &gdt_current_state()->tss;
    switch (index) {
        case 1: tss->ist1 = rsp; break;
        case 2: tss->ist2 = rsp; break;
        case 3: tss->ist3 = rsp; break;
        default: break;
    }
}


bool gdt_install_dynamic_stacks(usize ring0_size, usize ist_size) {
    gdt_cpu_state_t *state = gdt_current_state();
    if (state->dynamic_stacks_installed) return true;
    if (ring0_size < KERNEL_PRIV_STACK_SIZE || ist_size < IST_STACK_SIZE) return false;

    gdt_guarded_stack_t ring0;
    gdt_guarded_stack_t ist0;
    gdt_guarded_stack_t ist1;
    gdt_guarded_stack_t ist2;
    if (!gdt_allocate_guarded_stack(ring0_size, &ring0) ||
        !gdt_allocate_guarded_stack(ist_size, &ist0) ||
        !gdt_allocate_guarded_stack(ist_size, &ist1) ||
        !gdt_allocate_guarded_stack(ist_size, &ist2)) {
        return false;
    }
    state->ring0_region = ring0;
    state->ist_regions[0] = ist0;
    state->ist_regions[1] = ist1;
    state->ist_regions[2] = ist2;
    state->dynamic_kernel_stack = (void *)(uptr)ring0.usable_base;
    state->dynamic_ist_stacks[0] = (void *)(uptr)ist0.usable_base;
    state->dynamic_ist_stacks[1] = (void *)(uptr)ist1.usable_base;
    state->dynamic_ist_stacks[2] = (void *)(uptr)ist2.usable_base;
    state->dynamic_kernel_stack_size = ring0.usable_size;
    state->dynamic_ist_stack_size = ist0.usable_size;
    state->dynamic_stacks_installed = true;
    gdt_set_kernel_stack(ring0.usable_top);
    gdt_set_ist(1, ist0.usable_top);
    gdt_set_ist(2, ist1.usable_top);
    gdt_set_ist(3, ist2.usable_top);
    KLOG(LOG_INFO, "gdt", "cpu%u installed guarded stacks rsp0=%p ring0=%llu ist=%llu guards=%u",
         gdt_cpu_slot(), (void *)state->tss.rsp0,
         (unsigned long long)state->dynamic_kernel_stack_size,
         (unsigned long long)state->dynamic_ist_stack_size,
         gdt_all_regions_guarded(state) ? 1u : 0u);
    return true;
}

void gdt_init(void) {
    gdt_cpu_state_t *state = gdt_current_state();
    memset(state->gdt, 0, sizeof(state->gdt));
    memset(&state->tss, 0, sizeof(state->tss));
    bool have_dynamic_stacks = state->dynamic_kernel_stack && state->dynamic_kernel_stack_size && state->dynamic_ist_stack_size &&
                               state->dynamic_ist_stacks[0] && state->dynamic_ist_stacks[1] && state->dynamic_ist_stacks[2];
    state->dynamic_stacks_installed = have_dynamic_stacks;

    state->gdt[1] = 0x00af9b000000ffffull;
    state->gdt[2] = 0x00cf93000000ffffull;
    state->gdt[3] = 0x00cff3000000ffffull;
    state->gdt[4] = 0x00affb000000ffffull;

    state->tss.iopb_offset = sizeof(tss64_t);
    if (have_dynamic_stacks) {
        uptr ring0_top = 0;
        uptr ist0_top = 0;
        uptr ist1_top = 0;
        uptr ist2_top = 0;
        if (gdt_stack_top_checked(state->dynamic_kernel_stack, state->dynamic_kernel_stack_size, &ring0_top) &&
            gdt_stack_top_checked(state->dynamic_ist_stacks[0], state->dynamic_ist_stack_size, &ist0_top) &&
            gdt_stack_top_checked(state->dynamic_ist_stacks[1], state->dynamic_ist_stack_size, &ist1_top) &&
            gdt_stack_top_checked(state->dynamic_ist_stacks[2], state->dynamic_ist_stack_size, &ist2_top)) {
            gdt_set_kernel_stack(ring0_top);
            gdt_set_ist(1, ist0_top);
            gdt_set_ist(2, ist1_top);
            gdt_set_ist(3, ist2_top);
        } else {
            state->dynamic_stacks_installed = false;
            gdt_set_kernel_stack(gdt_kernel_stack_top());
            gdt_set_ist(1, gdt_ist_top(1));
            gdt_set_ist(2, gdt_ist_top(2));
            gdt_set_ist(3, gdt_ist_top(3));
        }
    } else {
        gdt_set_kernel_stack(gdt_kernel_stack_top());
        gdt_set_ist(1, gdt_ist_top(1));
        gdt_set_ist(2, gdt_ist_top(2));
        gdt_set_ist(3, gdt_ist_top(3));
    }
    uptr base = (uptr)&state->tss;
    u32 limit = (u32)(sizeof(tss64_t) - 1u);
    state->gdt[5] = make_tss_low(base, limit);
    state->gdt[6] = (u64)(base >> 32);

    gdtr_t gdtr = { .limit = (u16)(sizeof(state->gdt) - 1u), .base = (u64)(uptr)state->gdt };
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
    state->loaded = true;
    KLOG(LOG_INFO, "gdt", "cpu%u loaded kernel/user segments tss=%p rsp0=%p", gdt_cpu_slot(), &state->tss, (void *)state->tss.rsp0);
}

bool gdt_selftest(void) {
    gdt_cpu_state_t *state = gdt_current_state();
    if (state->gdt[1] != 0x00af9b000000ffffull) return false;
    if (state->gdt[2] != 0x00cf93000000ffffull) return false;
    if (state->gdt[3] != 0x00cff3000000ffffull) return false;
    if (state->gdt[4] != 0x00affb000000ffffull) return false;
    if ((state->gdt[5] & (0xffull << 40)) != (0x89ull << 40) && (state->gdt[5] & (0xffull << 40)) != (0x8bull << 40)) return false;
    if (state->tss.rsp0 == 0 || (state->tss.rsp0 & 0xfu) != 0) return false;
    if (state->tss.ist1 == 0 || state->tss.ist2 == 0 || state->tss.ist3 == 0) return false;
    if ((state->tss.ist1 & 0xfu) != 0 || (state->tss.ist2 & 0xfu) != 0 || (state->tss.ist3 & 0xfu) != 0) return false;
    if (state->tss.iopb_offset != sizeof(tss64_t)) return false;
    return true;
}
bool gdt_cpu_stack_info(u32 cpu_id, gdt_stack_info_t *out) {
    return gdt_fill_stack_info(cpu_id, out);
}

bool gdt_current_stack_info(gdt_stack_info_t *out) {
    return gdt_fill_stack_info(gdt_cpu_slot(), out);
}

bool gdt_stack_hardening_selftest(void) {
    gdt_stack_info_t info;
    if (!gdt_current_stack_info(&info)) return false;
    if (!info.loaded || !info.dynamic_stacks_installed || !info.guard_pages_installed || !info.canaries_ok) return false;
    if (info.ring0_size < KERNEL_PRIV_STACK_SIZE || info.ist_size < IST_STACK_SIZE) return false;
    if (info.ring0_guard_low + PAGE_SIZE != info.ring0_base) return false;
    if (info.ring0_top != info.ring0_guard_high) return false;
    if ((info.ring0_top & 0xfu) != 0) return false;
    for (u32 i = 0; i < 3u; ++i) {
        if (!info.ist_base[i] || !info.ist_top[i] || !info.ist_guard_low[i] || !info.ist_guard_high[i]) return false;
        if (info.ist_guard_low[i] + PAGE_SIZE != info.ist_base[i]) return false;
        if (info.ist_top[i] != info.ist_guard_high[i]) return false;
        if ((info.ist_top[i] & 0xfu) != 0) return false;
    }
    return true;
}
