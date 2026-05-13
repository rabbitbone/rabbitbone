static bool process_vma_allows_write_at(const active_process_t *p, uptr page_virt) {
    if (!p) return false;
    uptr page_end = page_virt + PAGE_SIZE;
    for (usize i = 0; i < p->vma_count; ++i) {
        const user_vma_t *v = &p->vmas[i];
        if (v->start <= page_virt && v->end >= page_end) {
            return (v->prot & AURORA_PROT_WRITE) != 0;
        }
    }
    return false;
}

static bool process_resolve_cow_page(active_process_t *p, uptr fault_addr) {
    if (!p || !p->active || !user_addr_range_valid(fault_addr, 1u)) return false;
    uptr page_virt = AURORA_ALIGN_DOWN(fault_addr, PAGE_SIZE);
    user_mapping_t *m = find_mapping(p, page_virt);
    if (!m || !(m->final_flags & VMM_COW) || (m->final_flags & VMM_WRITE)) return false;
    if (!process_vma_allows_write_at(p, page_virt)) return false;
    uptr phys = 0;
    u64 flags = 0;
    if (!vmm_space_translate(&p->space, page_virt, &phys, &flags)) return false;
    phys = AURORA_ALIGN_DOWN(phys, PAGE_SIZE);
    if (phys != m->phys || !(flags & VMM_USER) || !(flags & VMM_COW) || (flags & VMM_WRITE)) return false;
    u32 refs = memory_page_refcount((void *)phys);
    if (refs == 0u) return false;
    u64 writable_flags = (m->final_flags | VMM_WRITE) & ~VMM_COW;
    if (refs == 1u) {
        if (!vmm_space_protect_4k(&p->space, page_virt, writable_flags)) return false;
        m->final_flags = writable_flags;
        return true;
    }
    void *new_page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
    if (!new_page) return false;
    memcpy(new_page, (const void *)phys, PAGE_SIZE);
    if (!vmm_space_remap_4k(&p->space, page_virt, (uptr)new_page, writable_flags)) {
        memory_free_page(new_page);
        return false;
    }
    if (!memory_unref_page((void *)phys)) {
        (void)vmm_space_remap_4k(&p->space, page_virt, phys, m->final_flags);
        memory_free_page(new_page);
        return false;
    }
    m->phys = (uptr)new_page;
    m->final_flags = writable_flags;
    return true;
}

static bool ensure_process_user_page_writable(active_process_t *p, uptr addr, uptr *phys_out, u64 *flags_out) {
    uptr phys = 0;
    u64 flags = 0;
    if (!vmm_space_translate(&p->space, addr, &phys, &flags) || !(flags & VMM_USER)) return false;
    if (!(flags & VMM_WRITE)) {
        if (!(flags & VMM_COW) || !process_resolve_cow_page(p, addr)) return false;
        if (!vmm_space_translate(&p->space, addr, &phys, &flags) || !(flags & VMM_USER) || !(flags & VMM_WRITE)) return false;
    }
    if (phys_out) *phys_out = phys;
    if (flags_out) *flags_out = flags;
    return true;
}

static bool copy_to_process_user_space(active_process_t *p, uptr user, const void *src, usize size) {
    if (!p || !p->active || (!src && size) || !user_addr_range_valid(user, size)) return false;
    const u8 *s = (const u8 *)src;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return false;
        uptr phys = 0;
        u64 flags = 0;
        if (!ensure_process_user_page_writable(p, addr, &phys, &flags)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return true;
}

bool process_copy_string_from_user(uptr user, char *out, usize out_size) {
    if (!out || out_size == 0) return false;
    for (usize i = 0; i < out_size; ++i) {
        char c = 0;
        uptr ch_addr = 0;
        if (__builtin_add_overflow(user, (uptr)i, &ch_addr) || !process_copy_from_user(&c, ch_addr, 1u)) {
            out[0] = 0;
            return false;
        }
        out[i] = c;
        if (c == 0) return true;
    }
    out[out_size - 1u] = 0;
    return false;
}

static void release_mappings(active_process_t *p) {
    if (!p) return;
    for (usize i = 0; i < p->mapping_count; ++i) {
        (void)vmm_space_unmap_4k(&p->space, p->mappings[i].virt);
        if (p->mappings[i].phys) (void)memory_unref_page((void *)p->mappings[i].phys);
    }
    /*
     * Keep result.mapped_pages as terminal process metadata.  finish_current()
     * records the process after tearing down its live address space; clearing
     * the diagnostic counter here makes completed processes look as if they
     * had never mapped an image and breaks the registry contract once tests
     * query the just-finished child.
     *
     * Live mapping counts are maintained by track_mapping()/release_mapping_at()
     * while the process is running.  Full release is ownership teardown, not a
     * lifecycle-stat reset; callers that reuse an active_process_t already
     * memset or explicitly reinitialize result.mapped_pages before loading a
     * new image.
     */
    p->mapping_count = 0;
    p->vma_count = 0;
    p->mmap_cursor = USER_MMAP_BASE;
}

static user_mapping_t *find_mapping(active_process_t *p, uptr virt) {
    if (!p) return 0;
    for (usize i = 0; i < p->mapping_count; ++i) {
        if (p->mappings[i].virt == virt) return &p->mappings[i];
    }
    return 0;
}

static bool user_page_aligned(uptr value) {
    return (value & (PAGE_SIZE - 1u)) == 0;
}

static bool process_vma_ranges_overlap(uptr a0, uptr a1, uptr b0, uptr b1) {
    return a0 < a1 && b0 < b1 && a0 < b1 && b0 < a1;
}

static bool process_prot_valid(u32 prot) {
    if ((prot & ~AURORA_PROT_SUPPORTED) != 0) return false;
    if ((prot & AURORA_PROT_WRITE) && (prot & AURORA_PROT_EXEC)) return false;
    return prot != 0;
}

static bool process_mmap_flags_valid(u32 flags) {
    if ((flags & ~AURORA_MAP_SUPPORTED) != 0) return false;
    return (flags & AURORA_MAP_ANON) && (flags & AURORA_MAP_PRIVATE);
}

static u64 process_flags_from_prot(u32 prot) {
    u64 flags = VMM_USER;
    if (prot & AURORA_PROT_WRITE) flags |= VMM_WRITE;
    if ((prot & AURORA_PROT_EXEC) == 0) flags |= VMM_NX;
    return flags;
}

static u32 process_prot_from_flags(u64 flags) {
    u32 prot = AURORA_PROT_READ;
    if (flags & VMM_WRITE) prot |= AURORA_PROT_WRITE;
    if ((flags & VMM_NX) == 0) prot |= AURORA_PROT_EXEC;
    return prot;
}

static bool process_range_free(const active_process_t *p, uptr start, uptr end) {
    if (!p || start >= end) return false;
    for (usize i = 0; i < p->vma_count; ++i) {
        if (process_vma_ranges_overlap(start, end, p->vmas[i].start, p->vmas[i].end)) return false;
    }
    return true;
}

static process_status_t process_add_vma(active_process_t *p, uptr start, uptr end, u32 prot, u32 flags, u32 kind) {
    if (!p || start >= end || !user_page_aligned(start) || !user_page_aligned(end)) return PROC_ERR_INVAL;
    if (!user_addr_range_valid(start, (usize)(end - start))) return PROC_ERR_RANGE;
    if (!process_prot_valid(prot)) return PROC_ERR_INVAL;
    if (p->vma_count >= USER_MAX_VMAS) return PROC_ERR_NOMEM;
    if (!process_range_free(p, start, end)) return PROC_ERR_RANGE;
    user_vma_t v = { .start = start, .end = end, .prot = prot, .flags = flags, .kind = kind, .reserved = 0 };
    usize pos = 0;
    while (pos < p->vma_count && p->vmas[pos].start < start) ++pos;
    for (usize i = p->vma_count; i > pos; --i) p->vmas[i] = p->vmas[i - 1u];
    p->vmas[pos] = v;
    ++p->vma_count;
    return PROC_OK;
}

static void process_remove_vma_at(active_process_t *p, usize idx) {
    if (!p || idx >= p->vma_count) return;
    for (usize i = idx + 1u; i < p->vma_count; ++i) p->vmas[i - 1u] = p->vmas[i];
    --p->vma_count;
}

static bool process_range_has_vma(const active_process_t *p, uptr start, uptr end, u32 kind) {
    if (!p || start >= end) return false;
    uptr cursor = start;
    for (usize i = 0; i < p->vma_count && cursor < end; ++i) {
        const user_vma_t *v = &p->vmas[i];
        if (v->end <= cursor) continue;
        if (v->start > cursor) return false;
        if (kind && v->kind != kind) return false;
        if (v->end > cursor) cursor = v->end < end ? v->end : end;
    }
    return cursor >= end;
}

static process_status_t process_split_vma_at(active_process_t *p, usize idx, uptr cut) {
    if (!p || idx >= p->vma_count) return PROC_ERR_INVAL;
    user_vma_t *v = &p->vmas[idx];
    if (cut <= v->start || cut >= v->end) return PROC_OK;
    if (p->vma_count >= USER_MAX_VMAS) return PROC_ERR_NOMEM;
    user_vma_t right = *v;
    right.start = cut;
    v->end = cut;
    for (usize i = p->vma_count; i > idx + 1u; --i) p->vmas[i] = p->vmas[i - 1u];
    p->vmas[idx + 1u] = right;
    ++p->vma_count;
    return PROC_OK;
}

static process_status_t process_split_vmas_for_range(active_process_t *p, uptr start, uptr end) {
    if (!p || start >= end) return PROC_ERR_INVAL;
    for (usize i = 0; i < p->vma_count; ++i) {
        if (p->vmas[i].start < start && p->vmas[i].end > start) {
            process_status_t st = process_split_vma_at(p, i, start);
            if (st != PROC_OK) return st;
            break;
        }
    }
    for (usize i = 0; i < p->vma_count; ++i) {
        if (p->vmas[i].start < end && p->vmas[i].end > end) {
            process_status_t st = process_split_vma_at(p, i, end);
            if (st != PROC_OK) return st;
            break;
        }
    }
    return PROC_OK;
}

static bool merge_page_flags(u64 old_flags, u64 segment_flags, u64 *merged_out) {
    bool wants_write = ((old_flags | segment_flags) & VMM_WRITE) != 0;
    bool wants_exec = ((old_flags & VMM_NX) == 0) || ((segment_flags & VMM_NX) == 0);
    if (wants_write && wants_exec) return false;
    u64 merged = VMM_USER;
    if (wants_write) merged |= VMM_WRITE;
    if (!wants_exec) merged |= VMM_NX;
    if (merged_out) *merged_out = merged;
    return true;
}

static process_status_t track_mapping(active_process_t *p, uptr virt, uptr phys, u64 final_flags) {
    if (p->mapping_count >= USER_MAX_MAPPINGS) return PROC_ERR_NOMEM;
    p->mappings[p->mapping_count].virt = virt;
    p->mappings[p->mapping_count].phys = phys;
    p->mappings[p->mapping_count].final_flags = final_flags;
    ++p->mapping_count;
    p->result.mapped_pages = p->mapping_count;
    return PROC_OK;
}


static void release_mapping_at(active_process_t *p, usize idx) {
    if (!p || idx >= p->mapping_count) return;
    (void)vmm_space_unmap_4k(&p->space, p->mappings[idx].virt);
    if (p->mappings[idx].phys) (void)memory_unref_page((void *)p->mappings[idx].phys);
    for (usize i = idx + 1u; i < p->mapping_count; ++i) p->mappings[i - 1u] = p->mappings[i];
    --p->mapping_count;
    p->result.mapped_pages = p->mapping_count;
}

static process_status_t process_init_heap_for(active_process_t *p, usize first_mapping) {
    if (!p || first_mapping > p->mapping_count) return PROC_ERR_INVAL;
    uptr image_end = USER_IMAGE_BASE;
    for (usize i = first_mapping; i < p->mapping_count; ++i) {
        uptr end = 0;
        if (__builtin_add_overflow(p->mappings[i].virt, (uptr)PAGE_SIZE, &end)) return PROC_ERR_RANGE;
        if (end > image_end) image_end = end;
    }
    uptr heap_base = 0;
    uptr heap_limit = 0;
    if (__builtin_add_overflow(image_end, (uptr)(USER_HEAP_GAP_PAGES * PAGE_SIZE), &heap_base)) return PROC_ERR_RANGE;
    heap_base = AURORA_ALIGN_UP(heap_base, PAGE_SIZE);
    if (__builtin_add_overflow(heap_base, (uptr)(USER_HEAP_MAX_PAGES * PAGE_SIZE), &heap_limit)) return PROC_ERR_RANGE;
    if (!user_addr_range_valid(heap_base, 0) || !user_addr_range_valid(heap_limit - 1u, 1u)) return PROC_ERR_RANGE;
    uptr lowest_stack_base = USER_STACK_TOP - ((uptr)USER_STACK_ASLR_PAGES + USER_STACK_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE;
    if (heap_limit >= lowest_stack_base) return PROC_ERR_RANGE;
    p->heap_base = heap_base;
    p->heap_break = heap_base;
    p->heap_limit = heap_limit;
    p->mmap_cursor = USER_MMAP_BASE;
    return process_add_vma(p, heap_base, heap_limit, AURORA_PROT_READ | AURORA_PROT_WRITE, AURORA_MAP_ANON | AURORA_MAP_PRIVATE, USER_VMA_HEAP);
}

static process_status_t map_user_page(active_process_t *p, uptr virt, u64 flags) {
    if (!user_addr_range_valid(virt, PAGE_SIZE) || (virt & (PAGE_SIZE - 1u))) return PROC_ERR_RANGE;
    if (find_mapping(p, virt)) return PROC_ERR_RANGE;
    void *page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
    if (!page) return PROC_ERR_NOMEM;
    memset(page, 0, PAGE_SIZE);
    (void)vmm_space_unmap_4k(&p->space, virt);
    u64 final_flags = VMM_USER | flags;
    if (!vmm_space_map_4k(&p->space, virt, (uptr)page, final_flags)) {
        memory_free_page(page);
        return PROC_ERR_NOMEM;
    }
    process_status_t st = track_mapping(p, virt, (uptr)page, final_flags);
    if (st != PROC_OK) {
        (void)vmm_space_unmap_4k(&p->space, virt);
        memory_free_page(page);
    }
    return st;
}


static process_status_t process_set_brk_for(active_process_t *p, uptr new_break, uptr *current_out) {
    if (!p || !p->active || !p->heap_base || p->heap_base > p->heap_limit) return PROC_ERR_INVAL;
    if (new_break == 0) {
        if (current_out) *current_out = p->heap_break;
        return PROC_OK;
    }
    if (new_break < p->heap_base || new_break > p->heap_limit) return PROC_ERR_RANGE;
    uptr old_end = AURORA_ALIGN_UP(p->heap_break, PAGE_SIZE);
    uptr new_end = AURORA_ALIGN_UP(new_break, PAGE_SIZE);
    if (new_end > old_end) {
        uptr v = old_end;
        for (; v < new_end; v += PAGE_SIZE) {
            process_status_t st = map_user_page(p, v, VMM_WRITE | VMM_NX);
            if (st != PROC_OK) {
                for (uptr rollback = old_end; rollback < v; rollback += PAGE_SIZE) {
                    user_mapping_t *m = find_mapping(p, rollback);
                    if (m) release_mapping_at(p, (usize)(m - p->mappings));
                }
                return st;
            }
        }
    } else if (new_end < old_end) {
        uptr v = new_end;
        while (v < old_end) {
            user_mapping_t *m = find_mapping(p, v);
            if (!m) return PROC_ERR_RANGE;
            release_mapping_at(p, (usize)(m - p->mappings));
            v += PAGE_SIZE;
        }
    }
    p->heap_break = new_break;
    if (current_out) *current_out = p->heap_break;
    return PROC_OK;
}

process_status_t process_current_brk(uptr requested, uptr *current_out) {
    if (!current_proc.active) return PROC_ERR_INVAL;
    return process_set_brk_for(&current_proc, requested, current_out);
}

process_status_t process_current_sbrk(i64 increment, uptr *old_break_out) {
    if (!current_proc.active) return PROC_ERR_INVAL;
    uptr old_break = current_proc.heap_break;
    uptr new_break = old_break;
    if (increment >= 0) {
        if (__builtin_add_overflow(old_break, (uptr)(u64)increment, &new_break)) return PROC_ERR_RANGE;
    } else {
        u64 delta = (u64)(-(increment + 1)) + 1u;
        if (delta > old_break) return PROC_ERR_RANGE;
        new_break = old_break - (uptr)delta;
    }
    uptr ignored = 0;
    process_status_t st = process_set_brk_for(&current_proc, new_break, &ignored);
    if (st != PROC_OK) return st;
    if (old_break_out) *old_break_out = old_break;
    return PROC_OK;
}

static uptr process_mmap_arena_end(void) {
    uptr lowest_stack_base = USER_STACK_TOP - ((uptr)USER_STACK_ASLR_PAGES + USER_STACK_PAGES + USER_STACK_GUARD_PAGES) * PAGE_SIZE;
    uptr max_by_pages = USER_MMAP_BASE + (uptr)USER_MMAP_MAX_PAGES * PAGE_SIZE;
    return max_by_pages < lowest_stack_base ? max_by_pages : lowest_stack_base;
}

static bool process_normalize_user_len(uptr addr, usize length, uptr *start_out, uptr *end_out) {
    if (length == 0) return false;
    uptr start = AURORA_ALIGN_DOWN(addr, PAGE_SIZE);
    uptr raw_end = 0;
    if (__builtin_add_overflow(addr, (uptr)length, &raw_end)) return false;
    uptr end = AURORA_ALIGN_UP(raw_end, PAGE_SIZE);
    if (end <= start || !user_addr_range_valid(start, (usize)(end - start))) return false;
    if (start_out) *start_out = start;
    if (end_out) *end_out = end;
    return true;
}

static uptr process_find_free_mmap_range(active_process_t *p, usize size) {
    if (!p || size == 0) return 0;
    uptr arena_end = process_mmap_arena_end();
    uptr start = p->mmap_cursor;
    if (start < USER_MMAP_BASE || start >= arena_end) start = USER_MMAP_BASE;
    for (unsigned pass = 0; pass < 2u; ++pass) {
        for (uptr candidate = start; candidate < arena_end;) {
            uptr end = 0;
            if (__builtin_add_overflow(candidate, (uptr)size, &end) || end > arena_end) break;
            bool conflict = false;
            uptr next = candidate + PAGE_SIZE;
            for (usize i = 0; i < p->vma_count; ++i) {
                const user_vma_t *v = &p->vmas[i];
                if (process_vma_ranges_overlap(candidate, end, v->start, v->end)) {
                    conflict = true;
                    next = AURORA_ALIGN_UP(v->end, PAGE_SIZE);
                    break;
                }
            }
            if (!conflict) return candidate;
            if (next <= candidate) break;
            candidate = next;
        }
        start = USER_MMAP_BASE;
    }
    return 0;
}

process_status_t process_current_mmap(uptr requested, usize length, u32 prot, u32 flags, uptr *addr_out) {
    if (!current_proc.active || !addr_out) return PROC_ERR_INVAL;
    if (!process_prot_valid(prot) || !process_mmap_flags_valid(flags)) return PROC_ERR_INVAL;
    usize size = 0;
    if (!aurora_align_up_usize_checked(length, PAGE_SIZE, &size) || size == 0) return PROC_ERR_RANGE;
    uptr arena_end = process_mmap_arena_end();
    uptr start = 0;
    if (flags & AURORA_MAP_FIXED) {
        if (!requested || !user_page_aligned(requested)) return PROC_ERR_INVAL;
        start = requested;
        uptr end = 0;
        if (__builtin_add_overflow(start, (uptr)size, &end) || end > arena_end || start < USER_MMAP_BASE) return PROC_ERR_RANGE;
        if (!process_range_free(&current_proc, start, end)) return PROC_ERR_RANGE;
    } else {
        if (requested) {
            if (!user_page_aligned(requested)) return PROC_ERR_INVAL;
            uptr end = 0;
            if (!__builtin_add_overflow(requested, (uptr)size, &end) && requested >= USER_MMAP_BASE && end <= arena_end && process_range_free(&current_proc, requested, end)) {
                start = requested;
            }
        }
        if (!start) start = process_find_free_mmap_range(&current_proc, size);
        if (!start) return PROC_ERR_NOMEM;
    }
    uptr end = start + (uptr)size;
    process_status_t st = process_add_vma(&current_proc, start, end, prot, flags, USER_VMA_MMAP);
    if (st != PROC_OK) return st;
    u64 pte_flags = process_flags_from_prot(prot);
    uptr v = start;
    for (; v < end; v += PAGE_SIZE) {
        st = map_user_page(&current_proc, v, pte_flags & ~VMM_USER);
        if (st != PROC_OK) {
            uptr rollback = start;
            while (rollback < v) {
                user_mapping_t *m = find_mapping(&current_proc, rollback);
                if (m) release_mapping_at(&current_proc, (usize)(m - current_proc.mappings));
                rollback += PAGE_SIZE;
            }
            for (usize i = 0; i < current_proc.vma_count; ++i) {
                if (current_proc.vmas[i].start == start && current_proc.vmas[i].end == end && current_proc.vmas[i].kind == USER_VMA_MMAP) {
                    process_remove_vma_at(&current_proc, i);
                    break;
                }
            }
            return st;
        }
    }
    current_proc.mmap_cursor = end < arena_end ? end : USER_MMAP_BASE;
    *addr_out = start;
    return PROC_OK;
}

process_status_t process_current_munmap(uptr addr, usize length) {
    if (!current_proc.active || !user_page_aligned(addr)) return PROC_ERR_INVAL;
    uptr start = 0, end = 0;
    if (!process_normalize_user_len(addr, length, &start, &end) || start != addr) return PROC_ERR_RANGE;
    if (!process_range_has_vma(&current_proc, start, end, USER_VMA_MMAP)) return PROC_ERR_RANGE;
    process_status_t st = process_split_vmas_for_range(&current_proc, start, end);
    if (st != PROC_OK) return st;
    for (uptr v = start; v < end; v += PAGE_SIZE) {
        user_mapping_t *m = find_mapping(&current_proc, v);
        if (!m) return PROC_ERR_RANGE;
        release_mapping_at(&current_proc, (usize)(m - current_proc.mappings));
    }
    for (usize i = 0; i < current_proc.vma_count;) {
        if (current_proc.vmas[i].kind == USER_VMA_MMAP && current_proc.vmas[i].start >= start && current_proc.vmas[i].end <= end) {
            process_remove_vma_at(&current_proc, i);
        } else {
            ++i;
        }
    }
    if (start < current_proc.mmap_cursor) current_proc.mmap_cursor = start;
    return PROC_OK;
}

process_status_t process_current_mprotect(uptr addr, usize length, u32 prot) {
    if (!current_proc.active || !user_page_aligned(addr)) return PROC_ERR_INVAL;
    if (!process_prot_valid(prot)) return PROC_ERR_INVAL;
    uptr start = 0, end = 0;
    if (!process_normalize_user_len(addr, length, &start, &end) || start != addr) return PROC_ERR_RANGE;
    if (!process_range_has_vma(&current_proc, start, end, USER_VMA_MMAP)) return PROC_ERR_RANGE;
    process_status_t st = process_split_vmas_for_range(&current_proc, start, end);
    if (st != PROC_OK) return st;
    u64 new_flags = process_flags_from_prot(prot);
    for (uptr v = start; v < end; v += PAGE_SIZE) {
        user_mapping_t *m = find_mapping(&current_proc, v);
        if (!m) return PROC_ERR_RANGE;
        u64 pte_flags = new_flags;
        if (m->final_flags & VMM_COW) pte_flags = (new_flags & ~VMM_WRITE) | VMM_COW;
        if (!vmm_space_protect_4k(&current_proc.space, v, pte_flags)) return PROC_ERR_RANGE;
        m->final_flags = pte_flags;
    }
    for (usize i = 0; i < current_proc.vma_count; ++i) {
        user_vma_t *v = &current_proc.vmas[i];
        if (v->kind == USER_VMA_MMAP && v->start >= start && v->end <= end) v->prot = prot;
    }
    return PROC_OK;
}

static process_status_t ensure_image_page(active_process_t *p, uptr virt, u64 segment_final_flags) {
    if (!user_addr_range_valid(virt, PAGE_SIZE) || (virt & (PAGE_SIZE - 1u))) return PROC_ERR_RANGE;
    user_mapping_t *m = find_mapping(p, virt);
    if (m) {
        u64 merged = 0;
        if (!merge_page_flags(m->final_flags, segment_final_flags, &merged)) return PROC_ERR_FORMAT;
        m->final_flags = merged;
        if (!vmm_space_protect_4k(&p->space, virt, m->final_flags | VMM_WRITE)) return PROC_ERR_RANGE;
        return PROC_OK;
    }
    void *page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
    if (!page) return PROC_ERR_NOMEM;
    memset(page, 0, PAGE_SIZE);
    if (!vmm_space_map_4k(&p->space, virt, (uptr)page, segment_final_flags | VMM_WRITE)) {
        memory_free_page(page);
        return PROC_ERR_NOMEM;
    }
    process_status_t st = track_mapping(p, virt, (uptr)page, segment_final_flags);
    if (st != PROC_OK) {
        (void)vmm_space_unmap_4k(&p->space, virt);
        memory_free_page(page);
    }
    return st;
}

static process_status_t protect_loaded_image(active_process_t *p, usize first_mapping, usize last_mapping) {
    if (!p || first_mapping > last_mapping || last_mapping > p->mapping_count) return PROC_ERR_INVAL;
    for (usize i = first_mapping; i < last_mapping; ++i) {
        if (!vmm_space_protect_4k(&p->space, p->mappings[i].virt, p->mappings[i].final_flags)) return PROC_ERR_RANGE;
    }
    return PROC_OK;
}

static process_status_t copy_to_user_space(active_process_t *p, uptr user, const void *src, usize size) {
    if (!user_addr_range_valid(user, size)) return PROC_ERR_RANGE;
    const u8 *s = (const u8 *)src;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return PROC_ERR_RANGE;
        uptr phys = 0;
        u64 flags = 0;
        if (!ensure_process_user_page_writable(p, addr, &phys, &flags)) return PROC_ERR_RANGE;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return PROC_ERR_RANGE;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return PROC_OK;
}

static process_status_t zero_user_space(active_process_t *p, uptr user, usize size) {
    static const u8 zeros[64] = {0};
    while (size) {
        usize n = size > sizeof(zeros) ? sizeof(zeros) : size;
        process_status_t st = copy_to_user_space(p, user, zeros, n);
        if (st != PROC_OK) return st;
        if (__builtin_add_overflow(user, (uptr)n, &user)) return PROC_ERR_RANGE;
        size -= n;
    }
    return PROC_OK;
}

static process_status_t read_vfs_to_user(active_process_t *p, const char *path, u64 file_off, uptr user, usize size) {
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return PROC_ERR_RANGE;
        uptr phys = 0;
        u64 flags = 0;
        if (!ensure_process_user_page_writable(p, addr, &phys, &flags)) return PROC_ERR_RANGE;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return PROC_ERR_RANGE;
        usize n = step_info.chunk;
        u64 disk_off = 0;
        if (__builtin_add_overflow(file_off, (u64)done, &disk_off)) return PROC_ERR_RANGE;
        usize got = 0;
        vfs_status_t vs = vfs_read(path, disk_off, (void *)phys, n, &got);
        if (vs != VFS_OK) return PROC_ERR_IO;
        if (got != n) return PROC_ERR_IO;
        done += n;
    }
    return PROC_OK;
}

static u64 elf_segment_page_flags(const elf64_phdr_t *ph) {
    u64 flags = VMM_USER;
    if (ph->p_flags & ELF64_PF_W) flags |= VMM_WRITE;
    if (!(ph->p_flags & ELF64_PF_X)) flags |= VMM_NX;
    return flags;
}

