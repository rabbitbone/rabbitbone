#pragma once

#include <aurora/vmm.h>
#include <aurora/memory.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/spinlock.h>

#define PT_ENTRIES 512u
#define PTE_ADDR_MASK 0x000ffffffffff000ull
#define VMM_KERNEL_DIRECT_LIMIT MEMORY_KERNEL_DIRECT_LIMIT
#define VMM_ALLOWED_LEAF_FLAGS (VMM_WRITE | VMM_USER | VMM_WRITETHR | VMM_NOCACHE | VMM_GLOBAL | VMM_COW | VMM_NX)
#define VMM_ALLOWED_TABLE_FLAGS (VMM_WRITE | VMM_USER | VMM_WRITETHR | VMM_NOCACHE)
#define VMM_CANONICAL_LOW_TOP 0x0000800000000000ull
#define VMM_CANONICAL_HIGH_BASE 0xffff800000000000ull
#define VMM_USER_MIN PAGE_SIZE
#define VMM_USER_TOP VMM_CANONICAL_LOW_TOP

typedef u64 pte_t;

extern char __kernel_start[];
extern char __kernel_end[];

static vmm_space_t kernel_space_obj;
static vmm_space_t *current_space_obj;
static vmm_stats_t stats;
static spinlock_t vmm_lock;

static bool vmm_is_canonical(uptr virt) {
    return virt < VMM_CANONICAL_LOW_TOP || virt >= VMM_CANONICAL_HIGH_BASE;
}

static bool vmm_is_user_range(uptr virt, usize bytes) {
    if (bytes == 0 || virt < VMM_USER_MIN || virt >= VMM_USER_TOP) return false;
    uptr end = 0;
    if (__builtin_add_overflow(virt, (uptr)bytes - 1u, &end)) return false;
    return end < VMM_USER_TOP;
}

static bool vmm_ranges_overlap(uptr a_start, uptr a_end, uptr b_start, uptr b_end) {
    return a_start < a_end && b_start < b_end && a_start < b_end && b_start < a_end;
}

static bool vmm_range_overlaps_kernel_image(uptr virt, usize bytes) {
    if (bytes == 0) return false;
    uptr end = 0;
    if (__builtin_add_overflow(virt, (uptr)bytes, &end)) return true;
    return vmm_ranges_overlap(virt, end, (uptr)__kernel_start, (uptr)__kernel_end);
}

static bool vmm_leaf_flags_valid_for_space(const vmm_space_t *space, uptr virt, u64 flags) {
    if (!space || (flags & ~VMM_ALLOWED_LEAF_FLAGS) || !vmm_is_canonical(virt)) return false;
    if (flags & VMM_USER) return space->user_space && vmm_is_user_range(virt, PAGE_SIZE) && !vmm_range_overlaps_kernel_image(virt, PAGE_SIZE);
    return true;
}
