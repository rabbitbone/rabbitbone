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

static u64 bootstrap_4k_flags(uptr addr) {
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
    return VMM_WRITE | VMM_GLOBAL | VMM_NX;
}

void vmm_init(u64 identity_bytes) {
    memset(&stats, 0, sizeof(stats));
    memset(&kernel_space_obj, 0, sizeof(kernel_space_obj));
    kernel_space_obj.user_space = false;
    pte_t *root = alloc_kernel_table();
    kernel_space_obj.pml4_physical = (uptr)root;
    current_space_obj = &kernel_space_obj;
    if (identity_bytes < 64ull * 1024ull * 1024ull) identity_bytes = 64ull * 1024ull * 1024ull;
    identity_bytes = (identity_bytes + (2ull * 1024ull * 1024ull - 1u)) & ~((2ull * 1024ull * 1024ull) - 1u);
    const u64 fine_grained_bytes = 2ull * 1024ull * 1024ull;
    for (u64 addr = 0; addr < fine_grained_bytes; addr += PAGE_SIZE) {
        if (!vmm_map_4k((uptr)addr, (uptr)addr, bootstrap_4k_flags((uptr)addr))) PANIC("vmm: 4K identity map failed at %p", (void *)(uptr)addr);
    }
    for (u64 addr = fine_grained_bytes; addr < identity_bytes; addr += 2ull * 1024ull * 1024ull) {
        u64 flags = VMM_WRITE | VMM_NX;
        if (!vmm_map_2m((uptr)addr, (uptr)addr, flags)) PANIC("vmm: identity map failed at %p", (void *)(uptr)addr);
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
}

void vmm_dump(void) {
    vmm_stats_t s;
    vmm_get_stats(&s);
    kprintf("vmm: kernel_pml4=%p current=%p identity=%llu MiB tables=%llu 2M=%llu 4K=%llu spaces=%llu/%llu\n",
            (void *)s.pml4_physical,
            (void *)s.current_pml4_physical,
            (unsigned long long)(s.identity_bytes / 1024 / 1024),
            (unsigned long long)s.table_pages,
            (unsigned long long)s.mapped_2m_pages,
            (unsigned long long)s.mapped_4k_pages,
            (unsigned long long)s.spaces_created,
            (unsigned long long)s.spaces_destroyed);
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
