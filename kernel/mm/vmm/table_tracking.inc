static inline void write_cr3(uptr value) { __asm__ volatile("mov %0, %%cr3" :: "r"(value) : "memory"); }
uptr vmm_read_cr3(void) { uptr v; __asm__ volatile("mov %%cr3, %0" : "=r"(v)); return v; }

static pte_t *space_pml4(const vmm_space_t *space) {
    return space && space->pml4_physical ? (pte_t *)(uptr)space->pml4_physical : 0;
}

static bool track_table(vmm_space_t *space, pte_t *table) {
    if (!space || !table) return false;
    if (space->owned_count >= VMM_SPACE_MAX_TABLES) return false;
    space->owned_tables[space->owned_count++] = (uptr)table;
    stats.table_pages++;
    return true;
}

static pte_t *alloc_table_for(vmm_space_t *space) {
    pte_t *p = (pte_t *)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!p) return 0;
    memset(p, 0, PAGE_SIZE);
    if (!track_table(space, p)) {
        memory_free_page(p);
        return 0;
    }
    return p;
}

static pte_t *alloc_kernel_table(void) {
    pte_t *p = alloc_table_for(&kernel_space_obj);
    if (!p) PANIC("vmm: out of page-table pages");
    return p;
}

static bool untrack_table(vmm_space_t *space, pte_t *table) {
    if (!space || !table) return false;
    for (usize i = 0; i < space->owned_count; ++i) {
        if (space->owned_tables[i] == (uptr)table) {
            space->owned_tables[i] = space->owned_tables[space->owned_count - 1u];
            space->owned_tables[space->owned_count - 1u] = 0;
            --space->owned_count;
            if (stats.table_pages) --stats.table_pages;
            return true;
        }
    }
    return false;
}

static void free_tracked_table(vmm_space_t *space, pte_t *table) {
    if (!table) return;
    if (untrack_table(space, table)) memory_free_page(table);
}

typedef struct vmm_created_table {
    pte_t *parent;
    usize index;
    pte_t *table;
} vmm_created_table_t;

static void rollback_created_tables(vmm_space_t *space, vmm_created_table_t *created, usize count) {
    while (count) {
        --count;
        if (created[count].parent && created[count].table &&
            (created[count].parent[created[count].index] & PTE_ADDR_MASK) == (uptr)created[count].table) {
            created[count].parent[created[count].index] = 0;
        }
        free_tracked_table(space, created[count].table);
    }
}

static pte_t *next_table(vmm_space_t *space, pte_t *parent, usize idx, bool create, u64 flags) {
    if ((flags & ~(VMM_ALLOWED_TABLE_FLAGS)) != 0) return 0;
    if (parent[idx] & VMM_PRESENT) {
        (void)create;
        if ((parent[idx] & VMM_HUGE) != 0) return 0;
        if ((flags & VMM_USER) && (parent[idx] & VMM_USER) == 0) return 0;
        return (pte_t *)(uptr)(parent[idx] & PTE_ADDR_MASK);
    }
    if (!create) return 0;
    pte_t *child = alloc_table_for(space);
    if (!child) return 0;
    parent[idx] = ((uptr)child & PTE_ADDR_MASK) | VMM_PRESENT | VMM_WRITE | (flags & (VMM_USER | VMM_WRITETHR | VMM_NOCACHE));
    return child;
}

static bool vmm_split_huge_pde_locked(vmm_space_t *space, pte_t *pd, usize idx) {
    if (!space || !pd || idx >= PT_ENTRIES) return false;
    pte_t huge = pd[idx];
    if ((huge & (VMM_PRESENT | VMM_HUGE)) != (VMM_PRESENT | VMM_HUGE)) return true;
    pte_t *pt = alloc_table_for(space);
    if (!pt) return false;
    uptr phys_base = huge & PTE_ADDR_MASK;
    u64 leaf_flags = huge & ~PTE_ADDR_MASK;
    leaf_flags &= ~VMM_HUGE;
    for (usize i = 0; i < PT_ENTRIES; ++i) {
        pt[i] = (phys_base + i * PAGE_SIZE) | leaf_flags | VMM_PRESENT;
    }
    u64 table_flags = VMM_PRESENT | VMM_WRITE | (huge & (VMM_USER | VMM_WRITETHR | VMM_NOCACHE));
    pd[idx] = ((uptr)pt & PTE_ADDR_MASK) | table_flags;
    if (stats.mapped_2m_pages) --stats.mapped_2m_pages;
    stats.mapped_4k_pages += PT_ENTRIES;
    return true;
}

static void split_indices(uptr virt, usize *pml4i, usize *pdpti, usize *pdi, usize *pti) {
    *pml4i = (virt >> 39) & 0x1ffu;
    *pdpti = (virt >> 30) & 0x1ffu;
    *pdi = (virt >> 21) & 0x1ffu;
    *pti = (virt >> 12) & 0x1ffu;
}

static bool vmm_user_table_entry_allows_user(pte_t entry) {
    return (entry & VMM_PRESENT) != 0 && (entry & VMM_USER) != 0 && (entry & VMM_HUGE) == 0;
}

static bool vmm_space_existing_path_is_user(const vmm_space_t *space, uptr virt) {
    if (!space || !space->user_space || !vmm_is_user_range(virt, PAGE_SIZE)) return false;
    pte_t *pml4 = space_pml4(space);
    if (!pml4) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    (void)d;
    pte_t pml4e = pml4[a];
    if (!vmm_user_table_entry_allows_user(pml4e)) return false;
    pte_t *pdpt = (pte_t *)(uptr)(pml4e & PTE_ADDR_MASK);
    pte_t pdpte = pdpt[b];
    if (!vmm_user_table_entry_allows_user(pdpte)) return false;
    pte_t *pd = (pte_t *)(uptr)(pdpte & PTE_ADDR_MASK);
    pte_t pde = pd[c];
    return vmm_user_table_entry_allows_user(pde);
}

