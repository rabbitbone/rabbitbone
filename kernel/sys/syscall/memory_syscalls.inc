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
