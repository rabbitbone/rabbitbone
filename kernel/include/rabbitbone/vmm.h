#ifndef RABBITBONE_VMM_H
#define RABBITBONE_VMM_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define VMM_PRESENT  (1ull << 0)
#define VMM_WRITE    (1ull << 1)
#define VMM_USER     (1ull << 2)
#define VMM_WRITETHR (1ull << 3)
#define VMM_NOCACHE  (1ull << 4)
#define VMM_ACCESSED (1ull << 5)
#define VMM_DIRTY    (1ull << 6)
#define VMM_HUGE     (1ull << 7)
#define VMM_GLOBAL   (1ull << 8)
#define VMM_COW      (1ull << 9)  /* x86 software-available PTE bit */
#define VMM_WRITECOMB (1ull << 10) /* request PAT WC; encoded as PWT in hardware PTE */
#define VMM_NX       (1ull << 63)

#define VMM_SPACE_MAX_TABLES 128u
#ifndef SMP_MAX_CPUS
#define SMP_MAX_CPUS 16u
#endif

typedef struct vmm_space {
    uptr pml4_physical;
    uptr owned_tables[VMM_SPACE_MAX_TABLES];
    usize owned_count;
    bool user_space;
    volatile u32 active_cpu_mask;
    volatile u64 tlb_generation;
    volatile u64 tlb_seen_generation[SMP_MAX_CPUS];
    volatile u64 tlb_shootdowns;
    volatile u64 tlb_full_shootdowns;
    volatile u64 tlb_failures;
} vmm_space_t;

typedef struct vmm_stats {
    u64 mapped_4k_pages;
    u64 mapped_2m_pages;
    u64 table_pages;
    u64 identity_bytes;
    uptr pml4_physical;
    uptr current_pml4_physical;
    u64 spaces_created;
    u64 spaces_destroyed;
    bool kernel_text_ro;
    bool kernel_rodata_nx;
    bool kernel_data_nx;
} vmm_stats_t;

void vmm_init(u64 identity_bytes);
bool vmm_map_4k(uptr virt, uptr phys, u64 flags);
bool vmm_map_2m(uptr virt, uptr phys, u64 flags);
bool vmm_unmap_4k(uptr virt);
bool vmm_translate(uptr virt, uptr *phys_out, u64 *flags_out);

vmm_space_t *vmm_kernel_space(void);
vmm_space_t *vmm_current_space(void);
bool vmm_space_create_user(vmm_space_t *space);
void vmm_space_destroy(vmm_space_t *space);
bool vmm_space_map_4k(vmm_space_t *space, uptr virt, uptr phys, u64 flags);
bool vmm_space_unmap_4k(vmm_space_t *space, uptr virt);
bool vmm_space_protect_4k(vmm_space_t *space, uptr virt, u64 flags);
bool vmm_space_remap_4k(vmm_space_t *space, uptr virt, uptr phys, u64 flags);
bool vmm_space_translate(const vmm_space_t *space, uptr virt, uptr *phys_out, u64 *flags_out);
void vmm_switch_space(vmm_space_t *space);
void vmm_switch_kernel(void);
void vmm_note_cpu_current_space(vmm_space_t *space);
bool vmm_space_tlb_shootdown(vmm_space_t *space, uptr virt, usize length, bool full_flush, u64 timeout_ticks);
uptr vmm_read_cr3(void);

void vmm_get_stats(vmm_stats_t *out);
void vmm_dump(void);
bool vmm_selftest(void);
bool vmm_kernel_protection_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
