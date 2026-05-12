syscall_result_t aurora_sys_open(u64 path_ptr, u64 flags64) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 flags = (u32)flags64;
    if (!open_flags_valid(flags)) return err(VFS_ERR_INVAL);

    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    vfs_status_t vs = vfs_stat(path, &st);
    if (vs == VFS_ERR_NOENT && (flags & AURORA_O_CREAT)) {
        if (flags & AURORA_O_DIRECTORY) return err(VFS_ERR_NOTDIR);
        if (!sys_can_modify_parent(path)) return err(VFS_ERR_PERM);
        vs = vfs_create(path, 0, 0);
        if (vs != VFS_OK) return err(vs);
        aurora_credinfo_t c;
        if (sys_current_cred(&c)) { (void)vfs_chown(path, c.euid, c.egid); (void)vfs_chmod(path, 0666u & ~c.umask); }
        vs = vfs_stat(path, &st);
    } else if (vs == VFS_OK && (flags & AURORA_O_EXCL)) {
        return err(VFS_ERR_EXIST);
    }
    if (vs != VFS_OK) return err(vs);

    if ((flags & AURORA_O_DIRECTORY) && st.type != VFS_NODE_DIR) return err(VFS_ERR_NOTDIR);
    if (open_flags_can_read(flags) && !sys_can_read_stat(&st)) return err(VFS_ERR_PERM);
    if (open_flags_can_write(flags) && !sys_can_write_stat(&st)) return err(VFS_ERR_PERM);
    if (st.type == VFS_NODE_DIR && !sys_can_exec_stat(&st)) return err(VFS_ERR_PERM);
    if (st.type == VFS_NODE_DIR && (flags & AURORA_O_TRUNC)) return err(VFS_ERR_ISDIR);
    if (st.type == VFS_NODE_DIR && open_flags_can_write(flags)) return err(VFS_ERR_ISDIR);
    if ((flags & AURORA_O_TRUNC) && st.type != VFS_NODE_FILE) return err(VFS_ERR_INVAL);
    if (flags & AURORA_O_TRUNC) {
        vs = vfs_truncate(path, 0);
        if (vs != VFS_OK) return err(vs);
        vs = vfs_stat(path, &st);
        if (vs != VFS_OK) return err(vs);
    }

    i64 h = alloc_handle(active_handles(), path, &st, flags);
    return h >= 0 ? ok(h) : err(VFS_ERR_NOSPC);
}

syscall_result_t aurora_sys_close(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    close_handle(handles, h);
    return ok(0);
}

syscall_result_t aurora_sys_read(u64 handle, u64 buf, u64 len) {
    if (len > SYSCALL_MAX_IO_BYTES || (usize)len != len) return err(VFS_ERR_INVAL);
    return sys_read_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_write(u64 handle, u64 buf, u64 len) {
    if (len > SYSCALL_MAX_IO_BYTES || (usize)len != len) return err(VFS_ERR_INVAL);
    return sys_write_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_seek(u64 handle, u64 off64, u64 whence) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    i64 off = (i64)off64;
    if (!valid_handle(handles, h) || whence > AURORA_SEEK_END || handles[h].kind != SYS_HANDLE_VFS) return err(VFS_ERR_INVAL);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    u64 ff = spin_lock_irqsave(&f->lock);
    vfs_stat_t st;
    if (vfs_stat_ref(&f->ref, &st) == VFS_OK) {
        f->type = st.type;
        f->size = st.size;
        f->inode = st.inode;
        f->fs_id = st.fs_id;
        f->ref.type = st.type;
        f->ref.size = st.size;
    }
    i64 base = 0;
    if (whence == AURORA_SEEK_SET) base = 0;
    else if (whence == AURORA_SEEK_CUR) base = (i64)f->offset;
    else base = (i64)f->size;
    i64 next = 0;
    if (__builtin_add_overflow(base, off, &next) || next < 0) {
        spin_unlock_irqrestore(&f->lock, ff);
        return err(VFS_ERR_INVAL);
    }
    f->offset = (u64)next;
    sync_handle_from_file(&handles[h], f);
    spin_unlock_irqrestore(&f->lock, ff);
    return ok((i64)handles[h].offset);
}

syscall_result_t aurora_sys_stat(u64 path_ptr, u64 stat_out) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || !stat_out) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_stat(path, &st);
    if (vs != VFS_OK) return err(vs);
    return copy_out_buf(stat_out, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_lstat(u64 path_ptr, u64 stat_out) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || !stat_out) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_lstat(path, &st);
    if (vs != VFS_OK) return err(vs);
    return copy_out_buf(stat_out, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_symlink(u64 target_ptr, u64 link_path_ptr) {
    char target[SYSCALL_PATH_MAX];
    char link_path[SYSCALL_PATH_MAX];
    if (!copy_symlink_target_arg(target_ptr, target) || !copy_path_arg(link_path_ptr, link_path)) return err(VFS_ERR_INVAL);
    if (!sys_can_modify_parent(link_path)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_symlink(target, link_path);
    if (vs == VFS_OK) { aurora_credinfo_t c; if (sys_current_cred(&c)) { (void)vfs_chown(link_path, c.euid, c.egid); } }
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_readlink(u64 path_ptr, u64 out_ptr, u64 size64) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || (!out_ptr && size64)) return err(VFS_ERR_INVAL);
    if (size64 > SYSCALL_PATH_MAX) return err(VFS_ERR_INVAL);
    char target[SYSCALL_PATH_MAX];
    usize got = 0;
    vfs_status_t vs = vfs_readlink(path, target, (usize)size64, &got);
    if (vs != VFS_OK) return err(vs);
    return copy_out_buf(out_ptr, target, got) ? ok((i64)got) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_link(u64 old_path_ptr, u64 new_path_ptr) {
    char old_path[SYSCALL_PATH_MAX];
    char new_path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(old_path_ptr, old_path) || !copy_path_arg(new_path_ptr, new_path)) return err(VFS_ERR_INVAL);
    if (!sys_can_modify_parent(new_path)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_link(old_path, new_path);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_list(u64 path_ptr, u64 callback, u64 ctx64) {
    if (process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    char path[SYSCALL_PATH_MAX];
    vfs_dir_iter_fn fn = (vfs_dir_iter_fn)(uptr)callback;
    void *ctx = (void *)(uptr)ctx64;
    if (!copy_path_arg(path_ptr, path) || !fn) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_list(path, fn, ctx);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_create(u64 path_ptr, u64 data_ptr, u64 len64) {
    char path[SYSCALL_PATH_MAX];
    usize len = (usize)len64;
    if (!copy_path_arg(path_ptr, path) || (!data_ptr && len) || len64 > 65536u) return err(VFS_ERR_INVAL);
    void *tmp = 0;
    if (len) {
        tmp = kmalloc(len);
        if (!tmp) return err(VFS_ERR_NOMEM);
        if (!copy_in_buf(tmp, data_ptr, len)) { kfree(tmp); return err(VFS_ERR_INVAL); }
    }
    if (!sys_can_modify_parent(path)) { if (tmp) kfree(tmp); return err(VFS_ERR_PERM); }
    vfs_status_t vs = vfs_create(path, tmp, len);
    if (vs == VFS_OK) { aurora_credinfo_t c; if (sys_current_cred(&c)) { (void)vfs_chown(path, c.euid, c.egid); (void)vfs_chmod(path, 0666u & ~c.umask); } }
    if (tmp) kfree(tmp);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_mkdir(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    if (!sys_can_modify_parent(path)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_mkdir(path);
    if (vs == VFS_OK) { aurora_credinfo_t c; if (sys_current_cred(&c)) { (void)vfs_chown(path, c.euid, c.egid); (void)vfs_chmod(path, 0777u & ~c.umask); } }
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_unlink(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    if (!sys_can_modify_parent(path)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_unlink(path);
    return vs == VFS_OK ? ok(0) : err(vs);
}


syscall_result_t aurora_sys_truncate(u64 path_ptr, u64 size) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || size > SYSCALL_MAX_FILE_SIZE) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t sv = vfs_stat(path, &st);
    if (sv != VFS_OK) return err(sv);
    if (!sys_can_write_stat(&st)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_truncate(path, size);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_preallocate(u64 path_ptr, u64 size) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || size > SYSCALL_MAX_FILE_SIZE) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t sv = vfs_stat(path, &st);
    if (sv != VFS_OK) return err(sv);
    if (!sys_can_write_stat(&st)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_preallocate(path, size);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_ftruncate(u64 handle, u64 size) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || handles[h].kind != SYS_HANDLE_VFS || size > SYSCALL_MAX_FILE_SIZE) return err(VFS_ERR_INVAL);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (!open_flags_can_write(f->open_flags)) return err(VFS_ERR_PERM);
    u64 ff = spin_lock_irqsave(&f->lock);
    if (f->type == VFS_NODE_DIR) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_ISDIR); }
    vfs_status_t vs = vfs_truncate_ref(&f->ref, size);
    if (vs == VFS_OK) {
        vfs_stat_t st;
        if (vfs_stat_ref(&f->ref, &st) == VFS_OK) {
            f->type = st.type;
            f->size = st.size;
            f->inode = st.inode;
            f->fs_id = st.fs_id;
            f->ref.type = st.type;
            f->ref.size = st.size;
        } else {
            f->size = size;
            f->ref.size = size;
        }
        sync_handle_from_file(&handles[h], f);
    }
    spin_unlock_irqrestore(&f->lock, ff);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_fpreallocate(u64 handle, u64 size) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || handles[h].kind != SYS_HANDLE_VFS || size > SYSCALL_MAX_FILE_SIZE) return err(VFS_ERR_INVAL);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (!open_flags_can_write(f->open_flags)) return err(VFS_ERR_PERM);
    u64 ff = spin_lock_irqsave(&f->lock);
    if (f->type == VFS_NODE_DIR) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_ISDIR); }
    vfs_status_t vs = vfs_preallocate_ref(&f->ref, size);
    if (vs == VFS_OK) {
        vfs_stat_t st;
        if (vfs_stat_ref(&f->ref, &st) == VFS_OK) {
            f->type = st.type;
            f->size = st.size;
            f->inode = st.inode;
            f->fs_id = st.fs_id;
            f->ref.type = st.type;
            f->ref.size = st.size;
        } else if (size > f->size) {
            f->size = size;
            f->ref.size = size;
        }
        sync_handle_from_file(&handles[h], f);
    }
    spin_unlock_irqrestore(&f->lock, ff);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_rename(u64 old_path_ptr, u64 new_path_ptr) {
    char old_path[SYSCALL_PATH_MAX];
    char new_path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(old_path_ptr, old_path) || !copy_path_arg(new_path_ptr, new_path)) return err(VFS_ERR_INVAL);
    if (!sys_can_modify_parent(old_path) || !sys_can_modify_parent(new_path)) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_rename(old_path, new_path);
    return vs == VFS_OK ? ok(0) : err(vs);
}


syscall_result_t aurora_sys_install_commit(u64 staged_path_ptr, u64 final_path_ptr) {
    char staged_path[SYSCALL_PATH_MAX];
    char final_path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(staged_path_ptr, staged_path) || !copy_path_arg(final_path_ptr, final_path)) return err(VFS_ERR_INVAL);
    if (!sys_cred_root()) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_install_commit(staged_path, final_path);
    return vs == VFS_OK ? ok(0) : err(vs);
}


syscall_result_t aurora_sys_chmod(u64 path_ptr, u64 mode64) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || mode64 > 07777u) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t sv = vfs_stat(path, &st);
    if (sv != VFS_OK) return err(sv);
    aurora_credinfo_t c;
    if (!sys_current_cred(&c)) return err(VFS_ERR_INVAL);
    if (c.euid != AURORA_UID_ROOT && c.euid != st.uid) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_chmod(path, (u32)mode64);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_chown(u64 path_ptr, u64 uid64, u64 gid64) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || uid64 > 0xffffffffull || gid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    if (!sys_cred_root()) return err(VFS_ERR_PERM);
    vfs_status_t vs = vfs_chown(path, (u32)uid64, (u32)gid64);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_sync(void) {
    vfs_status_t vs = vfs_sync_all();
    return vs == VFS_OK ? ok(0) : err(vs);
}

static syscall_result_t sys_fsync_common(u64 handle, bool data_only) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || handles[h].kind != SYS_HANDLE_VFS) return err(VFS_ERR_INVAL);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    u64 ff = spin_lock_irqsave(&f->lock);
    vfs_status_t vs = vfs_sync_ref(&f->ref, data_only);
    spin_unlock_irqrestore(&f->lock, ff);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_fsync(u64 handle) {
    return sys_fsync_common(handle, false);
}

syscall_result_t aurora_sys_fdatasync(u64 handle) {
    return sys_fsync_common(handle, true);
}

syscall_result_t aurora_sys_statvfs(u64 path_ptr, u64 out_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || !out_ptr) return err(VFS_ERR_INVAL);
    vfs_statvfs_t st;
    vfs_status_t vs = vfs_statvfs(path, &st);
    if (vs != VFS_OK) return err(vs);
    aurora_statvfs_t out;
    memset(&out, 0, sizeof(out));
    out.block_size = st.block_size;
    out.total_blocks = st.total_blocks;
    out.free_blocks = st.free_blocks;
    out.avail_blocks = st.avail_blocks;
    out.total_inodes = st.total_inodes;
    out.free_inodes = st.free_inodes;
    out.fs_id = st.fs_id;
    out.flags = st.flags;
    out.max_name_len = st.max_name_len;
    strncpy(out.mount_path, st.mount_path, sizeof(out.mount_path) - 1u);
    strncpy(out.fs_name, st.fs_name, sizeof(out.fs_name) - 1u);
    return copy_out_buf(out_ptr, &out, sizeof(out)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_ticks(void) {
    return ok((i64)pit_ticks());
}

syscall_result_t aurora_sys_getpid(void) {
    return ok((i64)process_current_pid());
}

syscall_result_t aurora_sys_procinfo(u64 pid64, u64 out_ptr) {
    if (!out_ptr || pid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    process_info_t info;
    u32 pid = (u32)pid64;
    if (process_user_active()) {
        u32 self = process_current_pid();
        if (pid == 0) pid = self;
        if (pid != self) return err(VFS_ERR_PERM);
    }
    bool found = false;
    if (pid == 0) {
        found = process_current_info(&info);
    } else {
        found = process_lookup(pid, &info);
    }
    if (!found) return err(VFS_ERR_NOENT);
    info.address_space = 0;
    if (!copy_out_buf(out_ptr, &info, sizeof(info))) return err(VFS_ERR_INVAL);
    return ok(0);
}

