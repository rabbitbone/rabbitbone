static bool process_resolve_cow_page(active_process_t *p, uptr fault_addr) {
    if (!p || !p->active || !user_addr_range_valid(fault_addr, 1u)) return false;
    uptr page_virt = AURORA_ALIGN_DOWN(fault_addr, PAGE_SIZE);
    user_mapping_t *m = find_mapping(p, page_virt);
    if (!m || !(m->final_flags & VMM_COW) || (m->final_flags & VMM_WRITE)) return false;
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
    p->mapping_count = 0;
}

static user_mapping_t *find_mapping(active_process_t *p, uptr virt) {
    if (!p) return 0;
    for (usize i = 0; i < p->mapping_count; ++i) {
        if (p->mappings[i].virt == virt) return &p->mappings[i];
    }
    return 0;
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
    return PROC_OK;
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

