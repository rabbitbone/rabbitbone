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
    return vmm_space_translate(vmm_current_space_local(), virt, phys_out, flags_out);
}

void vmm_note_cpu_current_space(vmm_space_t *space) {
    if (!space) space = &kernel_space_obj;
    smp_set_current_address_space(space);
    vmm_mark_space_active_on_current_cpu(space);
}

bool vmm_space_tlb_shootdown(vmm_space_t *space, uptr virt, usize length, bool full_flush, u64 timeout_ticks) {
    if (!space || !space->pml4_physical) return false;
    u64 generation = __atomic_add_fetch(&space->tlb_generation, 1u, __ATOMIC_ACQ_REL);
    u32 mask = 0;
    if (space == &kernel_space_obj) {
        mask = smp_started_cpu_mask();
        if (mask == 0u) mask = 1u;
    } else {
        mask = __atomic_load_n(&space->active_cpu_mask, __ATOMIC_ACQUIRE) & smp_started_cpu_mask();
        if (mask == 0u) {
            u32 cpu = smp_current_cpu_id();
            if (cpu < 32u) mask = 1u << cpu;
        }
    }
    uptr target_cr3 = space == &kernel_space_obj ? 0u : space->pml4_physical;
    bool ok = smp_tlb_shootdown_mask(full_flush ? 0u : virt, full_flush ? 0u : length, target_cr3, mask, generation, timeout_ticks);
    if (ok) {
        for (u32 cpu = 0; cpu < SMP_MAX_CPUS && cpu < 32u; ++cpu) {
            if ((mask & (1u << cpu)) != 0u) __atomic_store_n(&space->tlb_seen_generation[cpu], generation, __ATOMIC_RELEASE);
        }
    }
    __atomic_add_fetch(&space->tlb_shootdowns, 1u, __ATOMIC_RELAXED);
    if (full_flush) __atomic_add_fetch(&space->tlb_full_shootdowns, 1u, __ATOMIC_RELAXED);
    if (!ok) __atomic_add_fetch(&space->tlb_failures, 1u, __ATOMIC_RELAXED);
    return ok;
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
    uptr old_cr3 = space->pml4_physical;
    if (space == vmm_current_space_local()) {
        smp_set_current_address_space(&kernel_space_obj);
        write_cr3(kernel_space_obj.pml4_physical);
        vmm_mark_space_active_on_current_cpu(&kernel_space_obj);
        current_space_obj = &kernel_space_obj;
    }
    u32 shoot_mask = __atomic_load_n(&space->active_cpu_mask, __ATOMIC_ACQUIRE) & smp_started_cpu_mask();
    u64 generation = __atomic_add_fetch(&space->tlb_generation, 1u, __ATOMIC_ACQ_REL);
    spin_unlock_irqrestore(&vmm_lock, irq);

    if (shoot_mask != 0u) {
        bool flushed = smp_tlb_shootdown_mask(0u, 0u, old_cr3, shoot_mask, generation, 0u);
        if (flushed) {
            for (u32 cpu = 0; cpu < SMP_MAX_CPUS && cpu < 32u; ++cpu) {
                if ((shoot_mask & (1u << cpu)) != 0u) __atomic_store_n(&space->tlb_seen_generation[cpu], generation, __ATOMIC_RELEASE);
            }
        } else {
            __atomic_add_fetch(&space->tlb_failures, 1u, __ATOMIC_RELAXED);
        }
    }

    irq = spin_lock_irqsave(&vmm_lock);
    if (space->pml4_physical != old_cr3) { spin_unlock_irqrestore(&vmm_lock, irq); return; }
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
    smp_set_current_address_space(space);
    write_cr3(space->pml4_physical);
    vmm_mark_space_active_on_current_cpu(space);
    current_space_obj = space;
    spin_unlock_irqrestore(&vmm_lock, irq);
}

void vmm_switch_kernel(void) {
    vmm_switch_space(&kernel_space_obj);
}

vmm_space_t *vmm_kernel_space(void) { return &kernel_space_obj; }
vmm_space_t *vmm_current_space(void) { return vmm_current_space_local(); }

