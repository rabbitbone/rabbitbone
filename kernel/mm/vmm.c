#include <aurora/vmm.h>
#include <aurora/memory.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/panic.h>

#define PT_ENTRIES 512u
#define PTE_ADDR_MASK 0x000ffffffffff000ull
#define VMM_KERNEL_DIRECT_LIMIT MEMORY_KERNEL_DIRECT_LIMIT
#define VMM_ALLOWED_LEAF_FLAGS (VMM_WRITE | VMM_USER | VMM_WRITETHR | VMM_NOCACHE | VMM_GLOBAL | VMM_NX)
#define VMM_ALLOWED_TABLE_FLAGS (VMM_WRITE | VMM_USER | VMM_WRITETHR | VMM_NOCACHE | VMM_NX)

typedef u64 pte_t;

static vmm_space_t kernel_space_obj;
static vmm_space_t *current_space_obj;
static vmm_stats_t stats;

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

static pte_t *next_table(vmm_space_t *space, pte_t *parent, usize idx, bool create, u64 flags) {
    if ((flags & ~(VMM_ALLOWED_TABLE_FLAGS)) != 0) return 0;
    if (parent[idx] & VMM_PRESENT) {
        (void)create;
        return (pte_t *)(uptr)(parent[idx] & PTE_ADDR_MASK);
    }
    if (!create) return 0;
    pte_t *child = alloc_table_for(space);
    if (!child) return 0;
    parent[idx] = ((uptr)child & PTE_ADDR_MASK) | VMM_PRESENT | VMM_WRITE | (flags & VMM_USER);
    return child;
}

static void split_indices(uptr virt, usize *pml4i, usize *pdpti, usize *pdi, usize *pti) {
    *pml4i = (virt >> 39) & 0x1ffu;
    *pdpti = (virt >> 30) & 0x1ffu;
    *pdi = (virt >> 21) & 0x1ffu;
    *pti = (virt >> 12) & 0x1ffu;
}

bool vmm_space_map_4k(vmm_space_t *space, uptr virt, uptr phys, u64 flags) {
    pte_t *pml4 = space_pml4(space);
    if (!pml4 || (virt & (PAGE_SIZE - 1u)) || (phys & (PAGE_SIZE - 1u)) || (flags & ~VMM_ALLOWED_LEAF_FLAGS)) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, true, flags);
    if (!pdpt) return false;
    pte_t *pd = next_table(space, pdpt, b, true, flags);
    if (!pd) return false;
    if (pd[c] & VMM_HUGE) return false;
    pte_t *pt = next_table(space, pd, c, true, flags);
    if (!pt || (pt[d] & VMM_PRESENT)) return false;
    pt[d] = (phys & PTE_ADDR_MASK) | flags | VMM_PRESENT;
    stats.mapped_4k_pages++;
    return true;
}

bool vmm_map_4k(uptr virt, uptr phys, u64 flags) {
    return vmm_space_map_4k(&kernel_space_obj, virt, phys, flags);
}

bool vmm_map_2m(uptr virt, uptr phys, u64 flags) {
    pte_t *pml4 = space_pml4(&kernel_space_obj);
    if (!pml4 || (virt & ((2ull * 1024 * 1024) - 1u)) || (phys & ((2ull * 1024 * 1024) - 1u)) || (flags & ~VMM_ALLOWED_LEAF_FLAGS)) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    (void)d;
    pte_t *pdpt = next_table(&kernel_space_obj, pml4, a, true, flags);
    if (!pdpt) return false;
    pte_t *pd = next_table(&kernel_space_obj, pdpt, b, true, flags);
    if (!pd || (pd[c] & VMM_PRESENT)) return false;
    pd[c] = (phys & PTE_ADDR_MASK) | flags | VMM_PRESENT | VMM_HUGE;
    stats.mapped_2m_pages++;
    return true;
}

bool vmm_space_unmap_4k(vmm_space_t *space, uptr virt) {
    pte_t *pml4 = space_pml4(space);
    if (!pml4 || (virt & (PAGE_SIZE - 1u))) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, false, 0);
    if (!pdpt) return false;
    pte_t *pd = next_table(space, pdpt, b, false, 0);
    if (!pd || (pd[c] & VMM_HUGE)) return false;
    pte_t *pt = next_table(space, pd, c, false, 0);
    if (!pt || !(pt[d] & VMM_PRESENT)) return false;
    pt[d] = 0;
    if (stats.mapped_4k_pages) --stats.mapped_4k_pages;
    if (space == current_space_obj) __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

bool vmm_space_protect_4k(vmm_space_t *space, uptr virt, u64 flags) {
    pte_t *pml4 = space_pml4(space);
    if (!pml4 || (virt & (PAGE_SIZE - 1u)) || (flags & ~VMM_ALLOWED_LEAF_FLAGS)) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t *pdpt = next_table(space, pml4, a, false, 0);
    if (!pdpt) return false;
    pte_t *pd = next_table(space, pdpt, b, false, 0);
    if (!pd || (pd[c] & VMM_HUGE)) return false;
    pte_t *pt = next_table(space, pd, c, false, 0);
    if (!pt || !(pt[d] & VMM_PRESENT)) return false;
    uptr phys = pt[d] & PTE_ADDR_MASK;
    pt[d] = phys | (flags & ~PTE_ADDR_MASK) | VMM_PRESENT;
    if (space == current_space_obj) __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
    return true;
}

bool vmm_unmap_4k(uptr virt) {
    return vmm_space_unmap_4k(&kernel_space_obj, virt);
}

static u64 effective_leaf_flags(pte_t pml4e, pte_t pdpte, pte_t pde, pte_t pte, bool huge) {
    u64 effective = VMM_PRESENT;
    if ((pml4e & VMM_WRITE) && (pdpte & VMM_WRITE) && (pde & VMM_WRITE) && (huge || (pte & VMM_WRITE))) effective |= VMM_WRITE;
    if ((pml4e & VMM_USER) && (pdpte & VMM_USER) && (pde & VMM_USER) && (huge || (pte & VMM_USER))) effective |= VMM_USER;
    if ((pml4e | pdpte | pde | (huge ? 0ull : pte)) & VMM_NX) effective |= VMM_NX;
    if (!huge && (pte & VMM_GLOBAL)) effective |= VMM_GLOBAL;
    if (huge && (pde & VMM_GLOBAL)) effective |= VMM_GLOBAL;
    if (!huge && (pte & VMM_DIRTY)) effective |= VMM_DIRTY;
    if (!huge && (pte & VMM_ACCESSED)) effective |= VMM_ACCESSED;
    if (huge) effective |= VMM_HUGE;
    return effective;
}

bool vmm_space_translate(const vmm_space_t *space, uptr virt, uptr *phys_out, u64 *flags_out) {
    pte_t *pml4 = space_pml4(space);
    if (!pml4) return false;
    usize a, b, c, d;
    split_indices(virt, &a, &b, &c, &d);
    pte_t pml4e = pml4[a];
    if (!(pml4e & VMM_PRESENT)) return false;
    pte_t *pdpt = (pte_t *)(uptr)(pml4e & PTE_ADDR_MASK);
    pte_t pdpte = pdpt[b];
    if (!(pdpte & VMM_PRESENT)) return false;
    pte_t *pd = (pte_t *)(uptr)(pdpte & PTE_ADDR_MASK);
    pte_t pde = pd[c];
    if (!(pde & VMM_PRESENT)) return false;
    if (pde & VMM_HUGE) {
        if (phys_out) *phys_out = (pde & PTE_ADDR_MASK) + (virt & ((2ull * 1024 * 1024) - 1u));
        if (flags_out) *flags_out = effective_leaf_flags(pml4e, pdpte, pde, 0, true);
        return true;
    }
    pte_t *pt = (pte_t *)(uptr)(pde & PTE_ADDR_MASK);
    pte_t leaf = pt[d];
    if (!(leaf & VMM_PRESENT)) return false;
    if (phys_out) *phys_out = (leaf & PTE_ADDR_MASK) + (virt & (PAGE_SIZE - 1u));
    if (flags_out) *flags_out = effective_leaf_flags(pml4e, pdpte, pde, leaf, false);
    return true;
}

bool vmm_translate(uptr virt, uptr *phys_out, u64 *flags_out) {
    return vmm_space_translate(current_space_obj ? current_space_obj : &kernel_space_obj, virt, phys_out, flags_out);
}

bool vmm_space_create_user(vmm_space_t *space) {
    if (!space || !kernel_space_obj.pml4_physical) return false;
    memset(space, 0, sizeof(*space));
    space->user_space = true;
    pte_t *root = (pte_t *)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!root) return false;
    memcpy(root, (const void *)(uptr)kernel_space_obj.pml4_physical, PAGE_SIZE);
    space->pml4_physical = (uptr)root;
    if (!track_table(space, root)) {
        memory_free_page(root);
        memset(space, 0, sizeof(*space));
        return false;
    }
    stats.spaces_created++;
    return true;
}

void vmm_space_destroy(vmm_space_t *space) {
    if (!space || !space->pml4_physical) return;
    if (space == current_space_obj) vmm_switch_kernel();
    for (usize i = space->owned_count; i > 0; --i) {
        uptr p = space->owned_tables[i - 1u];
        if (p) {
            memory_free_page((void *)p);
            if (stats.table_pages) --stats.table_pages;
        }
    }
    memset(space, 0, sizeof(*space));
    stats.spaces_destroyed++;
}

void vmm_switch_space(vmm_space_t *space) {
    if (!space || !space->pml4_physical) PANIC("vmm: switch to invalid space");
    current_space_obj = space;
    write_cr3(space->pml4_physical);
}

void vmm_switch_kernel(void) {
    vmm_switch_space(&kernel_space_obj);
}

vmm_space_t *vmm_kernel_space(void) { return &kernel_space_obj; }
vmm_space_t *vmm_current_space(void) { return current_space_obj ? current_space_obj : &kernel_space_obj; }

void vmm_init(u64 identity_bytes) {
    memset(&stats, 0, sizeof(stats));
    memset(&kernel_space_obj, 0, sizeof(kernel_space_obj));
    kernel_space_obj.user_space = false;
    pte_t *root = alloc_kernel_table();
    kernel_space_obj.pml4_physical = (uptr)root;
    current_space_obj = &kernel_space_obj;
    if (identity_bytes < 64ull * 1024ull * 1024ull) identity_bytes = 64ull * 1024ull * 1024ull;
    identity_bytes = (identity_bytes + (2ull * 1024ull * 1024ull - 1u)) & ~((2ull * 1024ull * 1024ull) - 1u);
    for (u64 addr = 0; addr < identity_bytes; addr += 2ull * 1024ull * 1024ull) {
        u64 flags = VMM_WRITE;
        if (addr >= 2ull * 1024ull * 1024ull) flags |= VMM_NX;
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
    *out = stats;
    out->current_pml4_physical = current_space_obj ? current_space_obj->pml4_physical : 0;
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
    if (!vmm_map_4k(virt, phys, VMM_WRITE | VMM_GLOBAL)) {
        memory_free_page((void *)phys);
        return false;
    }
    uptr translated = 0;
    u64 flags = 0;
    bool ok = vmm_translate(virt, &translated, &flags);
    bool good = ok && translated == phys && (flags & VMM_PRESENT) && (flags & VMM_WRITE);
    if (vmm_map_4k(virt, phys, VMM_WRITE | VMM_GLOBAL)) good = false;
    if (!vmm_unmap_4k(virt)) good = false;
    memory_free_page((void *)phys);

    vmm_space_t space;
    if (!vmm_space_create_user(&space)) return false;
    uptr uphys = (uptr)memory_alloc_page_below(VMM_KERNEL_DIRECT_LIMIT);
    if (!uphys) { vmm_space_destroy(&space); return false; }
    uptr uvirt = 0x0000010000200000ull;
    if (!vmm_space_map_4k(&space, uvirt, uphys, VMM_USER | VMM_WRITE)) good = false;
    if (vmm_space_map_4k(&space, uvirt, uphys, VMM_USER | VMM_WRITE)) good = false;
    uptr got = 0;
    u64 uflags = 0;
    if (!vmm_space_translate(&space, uvirt, &got, &uflags) || got != uphys || !(uflags & VMM_USER) || !(uflags & VMM_WRITE)) good = false;
    if (!vmm_space_protect_4k(&space, uvirt, VMM_USER | VMM_NX)) good = false;
    if (!vmm_space_translate(&space, uvirt, &got, &uflags) || got != uphys || !(uflags & VMM_USER) || (uflags & VMM_WRITE) || !(uflags & VMM_NX)) good = false;
    uptr kern_got = 0;
    if (vmm_space_translate(&kernel_space_obj, uvirt, &kern_got, 0)) good = false;
    if (!vmm_space_unmap_4k(&space, uvirt)) good = false;
    memory_free_page((void *)uphys);
    vmm_space_destroy(&space);
    return good;
}
