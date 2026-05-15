bool vmm_space_translate(const vmm_space_t *space, uptr virt, uptr *phys_out, u64 *flags_out) {
    if (!space || !vmm_is_canonical(virt)) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    pte_t *pml4 = space_pml4(space);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t pml4e = pml4[a];
    if (!(pml4e & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pdpt = (pte_t *)(uptr)(pml4e & PTE_ADDR_MASK);
    pte_t pdpte = pdpt[b];
    if (!(pdpte & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pd = (pte_t *)(uptr)(pdpte & PTE_ADDR_MASK);
    pte_t pde = pd[c];
    if (!(pde & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    if (pde & VMM_HUGE) {
        if (phys_out) *phys_out = (pde & PTE_ADDR_MASK) + (virt & ((2ull * 1024 * 1024) - 1u));
        if (flags_out) *flags_out = effective_leaf_flags(pml4e, pdpte, pde, 0, true);
        spin_unlock_irqrestore(&vmm_lock, irq);
        return true;
    }
    pte_t *pt = (pte_t *)(uptr)(pde & PTE_ADDR_MASK);
    pte_t leaf = pt[d];
    if (!(leaf & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    if (phys_out) *phys_out = (leaf & PTE_ADDR_MASK) + (virt & (PAGE_SIZE - 1u));
    if (flags_out) *flags_out = effective_leaf_flags(pml4e, pdpte, pde, leaf, false);
    spin_unlock_irqrestore(&vmm_lock, irq);
    return true;
}

bool vmm_translate(uptr virt, uptr *phys_out, u64 *flags_out) {
    return vmm_space_translate(current_space_obj ? current_space_obj : &kernel_space_obj, virt, phys_out, flags_out);
}

bool vmm_space_create_user(vmm_space_t *space) {
    if (!space) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (!kernel_space_obj.pml4_physical) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    memset(space, 0, sizeof(*space));
    space->user_space = true;
    pte_t *root = (pte_t *)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!root) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    memset(root, 0, PAGE_SIZE);
    /* Rabbitbone currently links the kernel in the low identity map. Keep the
       supervisor-only kernel entries present in user CR3 so interrupts and
       syscalls can enter the kernel safely, but never propagate user bits. */
    const pte_t *kernel_root = (const pte_t *)(uptr)kernel_space_obj.pml4_physical;
    for (usize i = 0; i < PT_ENTRIES; ++i) root[i] = kernel_root[i] & ~VMM_USER;
    space->pml4_physical = (uptr)root;
    if (!track_table(space, root)) {
        memory_free_page(root);
        memset(space, 0, sizeof(*space));
        spin_unlock_irqrestore(&vmm_lock, irq);
        return false;
    }
    stats.spaces_created++;
    spin_unlock_irqrestore(&vmm_lock, irq);
    return true;
}

void vmm_space_destroy(vmm_space_t *space) {
    if (!space) return;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (!space->pml4_physical) { spin_unlock_irqrestore(&vmm_lock, irq); return; }
    if (space == current_space_obj) {
        current_space_obj = &kernel_space_obj;
        write_cr3(kernel_space_obj.pml4_physical);
    }
    for (usize i = space->owned_count; i > 0; --i) {
        uptr p = space->owned_tables[i - 1u];
        if (p) {
            memory_free_page((void *)p);
            if (stats.table_pages) --stats.table_pages;
        }
    }
    memset(space, 0, sizeof(*space));
    stats.spaces_destroyed++;
    spin_unlock_irqrestore(&vmm_lock, irq);
}

void vmm_switch_space(vmm_space_t *space) {
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (!space || !space->pml4_physical) PANIC("vmm: switch to invalid space");
    current_space_obj = space;
    write_cr3(space->pml4_physical);
    spin_unlock_irqrestore(&vmm_lock, irq);
}

void vmm_switch_kernel(void) {
    vmm_switch_space(&kernel_space_obj);
}

vmm_space_t *vmm_kernel_space(void) { return &kernel_space_obj; }
vmm_space_t *vmm_current_space(void) { return current_space_obj ? current_space_obj : &kernel_space_obj; }

