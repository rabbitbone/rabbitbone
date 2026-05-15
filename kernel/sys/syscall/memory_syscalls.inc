static bool sys_mmap_prot_valid(u32 prot) {
    if (prot == 0) return false;
    if ((prot & ~RABBITBONE_PROT_SUPPORTED) != 0) return false;
    if ((prot & RABBITBONE_PROT_WRITE) && (prot & RABBITBONE_PROT_EXEC)) return false;
    return true;
}

static bool sys_mmap_flags_valid(u32 flags) {
    if ((flags & ~RABBITBONE_MAP_SUPPORTED) != 0) return false;
    u32 sharing = flags & (RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_SHARED);
    return sharing == RABBITBONE_MAP_PRIVATE || sharing == RABBITBONE_MAP_SHARED;
}

static bool sys_mmap_addr_valid(u64 requested, u32 flags) {
    if ((flags & RABBITBONE_MAP_FIXED) != 0) {
        return requested != 0 && (requested & (PAGE_SIZE - 1u)) == 0;
    }
    return requested == 0 || (requested & (PAGE_SIZE - 1u)) == 0;
}

static bool sys_user_mapping_range_valid(u64 addr, u64 length) {
    return addr != 0 && (addr & (PAGE_SIZE - 1u)) == 0 && length != 0 && length <= SYSCALL_MMAP_MAX_BYTES;
}

syscall_result_t rabbitbone_sys_brk(u64 requested) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    uptr current = 0;
    process_status_t st = process_current_brk((uptr)requested, &current);
    return st == PROC_OK ? ok((i64)current) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_sbrk(u64 increment_raw) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    uptr old_break = 0;
    process_status_t st = process_current_sbrk((i64)increment_raw, &old_break);
    return st == PROC_OK ? ok((i64)old_break) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_mmap(u64 requested, u64 length, u64 prot64, u64 flags64, u64 fd64, u64 offset64) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (length == 0 || length > SYSCALL_MMAP_MAX_BYTES || prot64 > 0xffffffffull || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 prot = (u32)prot64;
    u32 flags = (u32)flags64;
    if (!sys_mmap_prot_valid(prot) || !sys_mmap_flags_valid(flags) || !sys_mmap_addr_valid(requested, flags)) return err(VFS_ERR_INVAL);
    bool is_anon = (flags & RABBITBONE_MAP_ANON) != 0;
    bool is_shared = (flags & RABBITBONE_MAP_SHARED) != 0;
    vfs_node_ref_t ref;
    memset(&ref, 0, sizeof(ref));
    const vfs_node_ref_t *ref_ptr = 0;
    u64 file_size = 0;
    if (is_anon) {
        if (fd64 != 0xffffffffffffffffull || offset64 != 0) return err(VFS_ERR_INVAL);
    } else {
        if (is_shared) return err(VFS_ERR_INVAL);
        if ((offset64 & (PAGE_SIZE - 1u)) != 0 || fd64 >= SYSCALL_MAX_HANDLES) return err(VFS_ERR_INVAL);
        sys_handle_t *handles = active_handles();
        usize h = (usize)fd64;
        if (!valid_handle(handles, h) || handles[h].kind != SYS_HANDLE_VFS) return err(VFS_ERR_INVAL);
        refresh_handle_stat(&handles[h]);
        sys_file_t *f = handle_file_acquire(&handles[h]);
        if (!f) return err(VFS_ERR_NOENT);
        if (!open_flags_can_read(f->open_flags)) { file_put(f); return err(VFS_ERR_PERM); }
        if (handles[h].type == VFS_NODE_DIR) { file_put(f); return err(VFS_ERR_ISDIR); }
        u64 ff = spin_lock_irqsave(&f->lock);
        ref = f->ref;
        file_size = f->size;
        spin_unlock_irqrestore(&f->lock, ff);
        file_put(f);
        ref_ptr = &ref;
    }
    uptr addr = 0;
    process_status_t st = process_current_mmap((uptr)requested, (usize)length, prot, flags, fd64, offset64, ref_ptr, file_size, &addr);
    return st == PROC_OK ? ok((i64)addr) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_munmap(u64 addr, u64 length) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (!sys_user_mapping_range_valid(addr, length)) return err(VFS_ERR_INVAL);
    process_status_t st = process_current_munmap((uptr)addr, (usize)length);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_mprotect(u64 addr, u64 length, u64 prot64) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (prot64 > 0xffffffffull || !sys_user_mapping_range_valid(addr, length)) return err(VFS_ERR_INVAL);
    u32 prot = (u32)prot64;
    if (!sys_mmap_prot_valid(prot)) return err(VFS_ERR_INVAL);
    process_status_t st = process_current_mprotect((uptr)addr, (usize)length, prot);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}
