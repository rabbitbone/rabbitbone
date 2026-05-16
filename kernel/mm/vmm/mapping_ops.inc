bool vmm_space_map_4k(vmm_space_t *space, uptr virt, uptr phys, u64 flags) {
    if ((virt & (PAGE_SIZE - 1u)) || (phys & (PAGE_SIZE - 1u)) || !vmm_leaf_flags_valid_for_space(space, virt, flags)) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    pte_t *pml4 = space_pml4(space);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    u64 table_flags = vmm_table_pte_flags(flags);
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    vmm_created_table_t created[3];
    usize created_count = 0;
    memset(created, 0, sizeof(created));

    bool created_pdpt = (pml4[a] & VMM_PRESENT) == 0;
    pte_t *pdpt = next_table(space, pml4, a, true, table_flags);
    if (!pdpt) goto fail;
    if (created_pdpt) created[created_count++] = (vmm_created_table_t){ .parent = pml4, .index = a, .table = pdpt };

    bool created_pd = (pdpt[b] & VMM_PRESENT) == 0;
    pte_t *pd = next_table(space, pdpt, b, true, table_flags);
    if (!pd) goto fail;
    if (created_pd) created[created_count++] = (vmm_created_table_t){ .parent = pdpt, .index = b, .table = pd };
    if (pd[c] & VMM_HUGE) goto fail;

    bool created_pt = (pd[c] & VMM_PRESENT) == 0;
    pte_t *pt = next_table(space, pd, c, true, table_flags);
    if (!pt || (pt[d] & VMM_PRESENT)) goto fail;
    if (created_pt) created[created_count++] = (vmm_created_table_t){ .parent = pd, .index = c, .table = pt };

    pt[d] = (phys & PTE_ADDR_MASK) | vmm_leaf_pte_flags(flags) | VMM_PRESENT;
    stats.mapped_4k_pages++;
    spin_unlock_irqrestore(&vmm_lock, irq);
    return true;

fail:
    rollback_created_tables(space, created, created_count);
    spin_unlock_irqrestore(&vmm_lock, irq);
    return false;
}

bool vmm_map_4k(uptr virt, uptr phys, u64 flags) {
    return vmm_space_map_4k(&kernel_space_obj, virt, phys, flags);
}

bool vmm_map_2m(uptr virt, uptr phys, u64 flags) {
    const uptr huge_size = 2ull * 1024ull * 1024ull;
    if (!vmm_is_canonical(virt) || (flags & VMM_USER) || (flags & VMM_GLOBAL) || (flags & VMM_COW) ||
        (flags & ~VMM_ALLOWED_LEAF_FLAGS) || (virt & (huge_size - 1u)) || (phys & (huge_size - 1u))) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    pte_t *pml4 = space_pml4(&kernel_space_obj);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    u64 table_flags = vmm_table_pte_flags(flags);
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    (void)d;
    vmm_created_table_t created[2];
    usize created_count = 0;
    memset(created, 0, sizeof(created));

    bool created_pdpt = (pml4[a] & VMM_PRESENT) == 0;
    pte_t *pdpt = next_table(&kernel_space_obj, pml4, a, true, table_flags);
    if (!pdpt) goto fail;
    if (created_pdpt) created[created_count++] = (vmm_created_table_t){ .parent = pml4, .index = a, .table = pdpt };

    bool created_pd = (pdpt[b] & VMM_PRESENT) == 0;
    pte_t *pd = next_table(&kernel_space_obj, pdpt, b, true, table_flags);
    if (!pd || (pd[c] & VMM_PRESENT)) goto fail;
    if (created_pd) created[created_count++] = (vmm_created_table_t){ .parent = pdpt, .index = b, .table = pd };

    pd[c] = (phys & PTE_ADDR_MASK) | vmm_leaf_pte_flags(flags) | VMM_PRESENT | VMM_HUGE;
    stats.mapped_2m_pages++;
    spin_unlock_irqrestore(&vmm_lock, irq);
    return true;

fail:
    rollback_created_tables(&kernel_space_obj, created, created_count);
    spin_unlock_irqrestore(&vmm_lock, irq);
    return false;
}

bool vmm_space_unmap_4k(vmm_space_t *space, uptr virt) {
    if (!space || (virt & (PAGE_SIZE - 1u)) || !vmm_is_canonical(virt)) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (space->user_space && !vmm_space_existing_path_is_user(space, virt)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pml4 = space_pml4(space);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, false, 0);
    if (!pdpt) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pd = next_table(space, pdpt, b, false, 0);
    if (!pd) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    if (pd[c] & VMM_HUGE) {
        if (space->user_space || !vmm_split_huge_pde_locked(space, pd, c)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    }
    pte_t *pt = next_table(space, pd, c, false, 0);
    if (!pt || !(pt[d] & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pt[d] = 0;
    if (stats.mapped_4k_pages) --stats.mapped_4k_pages;
    spin_unlock_irqrestore(&vmm_lock, irq);
    (void)vmm_space_tlb_shootdown(space, virt, PAGE_SIZE, false, 0);
    return true;
}

bool vmm_space_protect_4k(vmm_space_t *space, uptr virt, u64 flags) {
    if ((virt & (PAGE_SIZE - 1u)) || !vmm_leaf_flags_valid_for_space(space, virt, flags)) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (space->user_space && !vmm_space_existing_path_is_user(space, virt)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pml4 = space_pml4(space);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, false, 0);
    if (!pdpt) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pd = next_table(space, pdpt, b, false, 0);
    if (!pd) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    if (pd[c] & VMM_HUGE) {
        if (space->user_space || !vmm_split_huge_pde_locked(space, pd, c)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    }
    pte_t *pt = next_table(space, pd, c, false, 0);
    if (!pt || !(pt[d] & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    uptr phys = pt[d] & PTE_ADDR_MASK;
    pt[d] = phys | (vmm_leaf_pte_flags(flags) & ~PTE_ADDR_MASK) | VMM_PRESENT;
    spin_unlock_irqrestore(&vmm_lock, irq);
    (void)vmm_space_tlb_shootdown(space, virt, PAGE_SIZE, false, 0);
    return true;
}

bool vmm_unmap_4k(uptr virt) {
    return vmm_space_unmap_4k(&kernel_space_obj, virt);
}


bool vmm_space_remap_4k(vmm_space_t *space, uptr virt, uptr phys, u64 flags) {
    if ((virt & (PAGE_SIZE - 1u)) || (phys & (PAGE_SIZE - 1u)) || !vmm_leaf_flags_valid_for_space(space, virt, flags)) return false;
    u64 irq = spin_lock_irqsave(&vmm_lock);
    if (space->user_space && !vmm_space_existing_path_is_user(space, virt)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pml4 = space_pml4(space);
    if (!pml4) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, false, 0);
    if (!pdpt) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pte_t *pd = next_table(space, pdpt, b, false, 0);
    if (!pd) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    if (pd[c] & VMM_HUGE) {
        if (space->user_space || !vmm_split_huge_pde_locked(space, pd, c)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    }
    pte_t *pt = next_table(space, pd, c, false, 0);
    if (!pt || !(pt[d] & VMM_PRESENT)) { spin_unlock_irqrestore(&vmm_lock, irq); return false; }
    pt[d] = (phys & PTE_ADDR_MASK) | (vmm_leaf_pte_flags(flags) & ~PTE_ADDR_MASK) | VMM_PRESENT;
    spin_unlock_irqrestore(&vmm_lock, irq);
    (void)vmm_space_tlb_shootdown(space, virt, PAGE_SIZE, false, 0);
    return true;
}

static u64 effective_leaf_flags(pte_t pml4e, pte_t pdpte, pte_t pde, pte_t pte, bool huge) {
    u64 effective = VMM_PRESENT;
    if ((pml4e & VMM_WRITE) && (pdpte & VMM_WRITE) && (pde & VMM_WRITE) && (huge || (pte & VMM_WRITE))) effective |= VMM_WRITE;
    if ((pml4e & VMM_USER) && (pdpte & VMM_USER) && (pde & VMM_USER) && (huge || (pte & VMM_USER))) effective |= VMM_USER;
    if ((pml4e | pdpte | pde | (huge ? 0ull : pte)) & VMM_NX) effective |= VMM_NX;
    if (!huge && (pte & VMM_GLOBAL)) effective |= VMM_GLOBAL;
    if (!huge && (pte & VMM_COW)) effective |= VMM_COW;
    if (huge && (pde & VMM_GLOBAL)) effective |= VMM_GLOBAL;
    if (!huge && (pte & VMM_DIRTY)) effective |= VMM_DIRTY;
    if (!huge && (pte & VMM_ACCESSED)) effective |= VMM_ACCESSED;
    if (huge) effective |= VMM_HUGE;
    return effective;
}

