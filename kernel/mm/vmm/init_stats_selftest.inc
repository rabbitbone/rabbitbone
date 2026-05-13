extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];

static bool page_overlaps(uptr page, uptr start, uptr end) {
    return start < end && page < end && page + PAGE_SIZE > start;
}

static uptr align_down_uptr(uptr value, uptr align) {
    return value & ~(align - 1u);
}

static uptr align_up_uptr(uptr value, uptr align) {
    return (value + align - 1u) & ~(align - 1u);
}

static u64 identity_4k_flags(uptr addr) {
    const uptr text_start = (uptr)__text_start;
    const uptr text_end = (uptr)__text_end;
    const uptr rodata_start = (uptr)__rodata_start;
    const uptr rodata_end = (uptr)__rodata_end;
    const uptr data_start = (uptr)__data_start;
    const uptr data_end = (uptr)__data_end;
    const uptr bss_start = (uptr)__bss_start;
    const uptr bss_end = (uptr)__bss_end;

    if (page_overlaps(addr, text_start, text_end)) return VMM_GLOBAL;
    if (page_overlaps(addr, rodata_start, rodata_end)) return VMM_GLOBAL | VMM_NX;
    if (page_overlaps(addr, data_start, data_end) || page_overlaps(addr, bss_start, bss_end)) return VMM_WRITE | VMM_GLOBAL | VMM_NX;
    return VMM_WRITE | VMM_NX;
}

static bool identity_2m_window_overlaps_kernel(uptr base) {
    const uptr huge_size = 2ull * 1024ull * 1024ull;
    return vmm_ranges_overlap(base, base + huge_size, (uptr)__kernel_start, (uptr)__kernel_end);
}

static bool vmm_range_has_flags(uptr start, uptr end, u64 required, u64 forbidden) {
    if (start >= end) return true;
    const uptr first = align_down_uptr(start, PAGE_SIZE);
    const uptr last = align_up_uptr(end, PAGE_SIZE);
    for (uptr page = first; page < last; page += PAGE_SIZE) {
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&kernel_space_obj, page, &phys, &flags)) return false;
        if (phys != page) return false;
        if ((flags & required) != required) return false;
        if ((flags & forbidden) != 0) return false;
        if ((flags & VMM_HUGE) != 0) return false;
    }
    return true;
}

static void vmm_kernel_protection_status(bool *text_ro, bool *rodata_nx, bool *data_nx) {
    if (text_ro) {
        *text_ro = vmm_range_has_flags((uptr)__text_start, (uptr)__text_end,
                                       VMM_PRESENT | VMM_GLOBAL,
                                       VMM_WRITE | VMM_NX | VMM_USER | VMM_HUGE);
    }
    if (rodata_nx) {
        *rodata_nx = vmm_range_has_flags((uptr)__rodata_start, (uptr)__rodata_end,
                                         VMM_PRESENT | VMM_GLOBAL | VMM_NX,
                                         VMM_WRITE | VMM_USER | VMM_HUGE);
    }
    if (data_nx) {
        const bool data_ok = vmm_range_has_flags((uptr)__data_start, (uptr)__data_end,
                                                 VMM_PRESENT | VMM_GLOBAL | VMM_WRITE | VMM_NX,
                                                 VMM_USER | VMM_HUGE);
        const bool bss_ok = vmm_range_has_flags((uptr)__bss_start, (uptr)__bss_end,
                                                VMM_PRESENT | VMM_GLOBAL | VMM_WRITE | VMM_NX,
                                                VMM_USER | VMM_HUGE);
        *data_nx = data_ok && bss_ok;
    }
}

void vmm_init(u64 identity_bytes) {
    memset(&stats, 0, sizeof(stats));
    memset(&kernel_space_obj, 0, sizeof(kernel_space_obj));
    kernel_space_obj.user_space = false;
    pte_t *root = alloc_kernel_table();
    kernel_space_obj.pml4_physical = (uptr)root;
    current_space_obj = &kernel_space_obj;
    if (identity_bytes < 64ull * 1024ull * 1024ull) identity_bytes = 64ull * 1024ull * 1024ull;
    if (identity_bytes < (u64)(uptr)__kernel_end) identity_bytes = (u64)(uptr)__kernel_end;
    identity_bytes = (identity_bytes + (2ull * 1024ull * 1024ull - 1u)) & ~((2ull * 1024ull * 1024ull) - 1u);
    const u64 huge_size = 2ull * 1024ull * 1024ull;
    for (u64 addr = 0; addr < identity_bytes; addr += huge_size) {
        if (identity_2m_window_overlaps_kernel((uptr)addr)) {
            for (u64 page = addr; page < addr + huge_size && page < identity_bytes; page += PAGE_SIZE) {
                if (!vmm_map_4k((uptr)page, (uptr)page, identity_4k_flags((uptr)page))) PANIC("vmm: 4K identity map failed at %p", (void *)(uptr)page);
            }
        } else {
            if (!vmm_map_2m((uptr)addr, (uptr)addr, VMM_WRITE | VMM_NX)) PANIC("vmm: identity map failed at %p", (void *)(uptr)addr);
        }
    }
    stats.identity_bytes = identity_bytes;
    stats.pml4_physical = kernel_space_obj.pml4_physical;
    write_cr3(kernel_space_obj.pml4_physical);
    KLOG(LOG_INFO, "vmm", "loaded pml4=%p identity=%llu MiB cr3=%p", (void *)kernel_space_obj.pml4_physical,
         (unsigned long long)(identity_bytes / 1024 / 1024), (void *)vmm_read_cr3());
}

void vmm_get_stats(vmm_stats_t *out) {
    if (!out) return;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    *out = stats;
    out->current_pml4_physical = current_space_obj ? current_space_obj->pml4_physical : 0;
    spin_unlock_irqrestore(&vmm_lock, irq);
    vmm_kernel_protection_status(&out->kernel_text_ro, &out->kernel_rodata_nx, &out->kernel_data_nx);
}

void vmm_dump(void) {
    vmm_stats_t s;
    vmm_get_stats(&s);
    kprintf("vmm: kernel_pml4=%p current=%p identity=%llu MiB tables=%llu 2M=%llu 4K=%llu spaces=%llu/%llu kernel_text_ro=%u kernel_rodata_nx=%u kernel_data_nx=%u\n",
            (void *)s.pml4_physical,
            (void *)s.current_pml4_physical,
            (unsigned long long)(s.identity_bytes / 1024 / 1024),
            (unsigned long long)s.table_pages,
            (unsigned long long)s.mapped_2m_pages,
            (unsigned long long)s.mapped_4k_pages,
            (unsigned long long)s.spaces_created,
            (unsigned long long)s.spaces_destroyed,
            s.kernel_text_ro ? 1u : 0u,
            s.kernel_rodata_nx ? 1u : 0u,
            s.kernel_data_nx ? 1u : 0u);
}

bool vmm_kernel_protection_selftest(void) {
    bool text_ro = false;
    bool rodata_nx = false;
    bool data_nx = false;
    vmm_kernel_protection_status(&text_ro, &rodata_nx, &data_nx);
    if (!text_ro || !rodata_nx || !data_nx) return false;

    vmm_space_t space;
    if (!vmm_space_create_user(&space)) return false;
    uptr phys = (uptr)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!phys) {
        vmm_space_destroy(&space);
        return false;
    }

    const uptr kernel_page = align_down_uptr((uptr)__kernel_start, PAGE_SIZE);
    bool good = !vmm_space_map_4k(&space, kernel_page, phys, VMM_USER | VMM_WRITE | VMM_NX);
    if (vmm_space_protect_4k(&space, kernel_page, VMM_USER | VMM_NX)) good = false;

    memory_free_page((void *)phys);
    vmm_space_destroy(&space);
    return good;
}

bool vmm_selftest(void) {
    uptr phys = (uptr)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!phys) return false;
    uptr virt = 0xffff800000400000ull;
    (void)vmm_unmap_4k(virt);
    if (!vmm_map_4k(virt, phys, VMM_WRITE | VMM_GLOBAL | VMM_NX)) {
        memory_free_page((void *)phys);
        return false;
    }
    uptr translated = 0;
    u64 flags = 0;
    bool ok = vmm_space_translate(&kernel_space_obj, virt, &translated, &flags);
    bool good = ok && translated == phys && (flags & VMM_PRESENT) && (flags & VMM_WRITE) && (flags & VMM_NX);
    if (vmm_map_4k(virt, phys, VMM_WRITE | VMM_GLOBAL | VMM_NX)) good = false;
    if (!vmm_unmap_4k(virt)) good = false;
    memory_free_page((void *)phys);

    vmm_space_t space;
    if (!vmm_space_create_user(&space)) return false;
    uptr uphys = (uptr)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!uphys) { vmm_space_destroy(&space); return false; }
    uptr uvirt = 0x0000010000200000ull;
    if (!vmm_space_map_4k(&space, uvirt, uphys, VMM_USER | VMM_WRITE | VMM_NX)) good = false;
    if (vmm_space_map_4k(&space, uvirt, uphys, VMM_USER | VMM_WRITE | VMM_NX)) good = false;
    uptr got = 0;
    u64 uflags = 0;
    if (!vmm_space_translate(&space, uvirt, &got, &uflags) || got != uphys || !(uflags & VMM_USER) || !(uflags & VMM_WRITE) || !(uflags & VMM_NX)) good = false;
    if (!vmm_space_protect_4k(&space, uvirt, VMM_USER | VMM_NX)) good = false;
    if (!vmm_space_translate(&space, uvirt, &got, &uflags) || got != uphys || !(uflags & VMM_USER) || (uflags & VMM_WRITE) || !(uflags & VMM_NX)) good = false;
    uptr kern_got = 0;
    if (vmm_space_translate(&kernel_space_obj, uvirt, &kern_got, 0)) good = false;
    if (vmm_space_map_4k(&kernel_space_obj, uvirt, uphys, VMM_USER | VMM_WRITE)) good = false;
    if (!vmm_space_unmap_4k(&space, uvirt)) good = false;
    memory_free_page((void *)uphys);
    vmm_space_destroy(&space);
    return good;
}
