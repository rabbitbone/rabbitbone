syscall_result_t aurora_sys_brk(u64 requested) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    uptr current = 0;
    process_status_t st = process_current_brk((uptr)requested, &current);
    return st == PROC_OK ? ok((i64)current) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_sbrk(u64 increment_raw) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    uptr old_break = 0;
    process_status_t st = process_current_sbrk((i64)increment_raw, &old_break);
    return st == PROC_OK ? ok((i64)old_break) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_mmap(u64 requested, u64 length, u64 prot64, u64 flags64) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (length == 0 || length > SYSCALL_MMAP_MAX_BYTES || prot64 > 0xffffffffull || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    uptr addr = 0;
    process_status_t st = process_current_mmap((uptr)requested, (usize)length, (u32)prot64, (u32)flags64, &addr);
    return st == PROC_OK ? ok((i64)addr) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_munmap(u64 addr, u64 length) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (length == 0 || length > SYSCALL_MMAP_MAX_BYTES) return err(VFS_ERR_INVAL);
    process_status_t st = process_current_munmap((uptr)addr, (usize)length);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_mprotect(u64 addr, u64 length, u64 prot64) {
    if (!process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    if (length == 0 || length > SYSCALL_MMAP_MAX_BYTES || prot64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    process_status_t st = process_current_mprotect((uptr)addr, (usize)length, (u32)prot64);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}
