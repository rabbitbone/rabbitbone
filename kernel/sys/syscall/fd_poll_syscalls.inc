syscall_result_t rabbitbone_sys_dup(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    sys_handle_t cloned = handles[h];
    if (!handle_retain_resource(&cloned)) return err(VFS_ERR_NOENT);
    i64 nh = -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) { nh = (i64)i; break; }
    }
    if (nh < 0) { handle_release_resource(&cloned); return err(VFS_ERR_NOSPC); }
    if (!handle_table_can_add_kind(handles, (usize)nh, cloned.kind)) { handle_release_resource(&cloned); return err(VFS_ERR_NOSPC); }
    cloned.used = true;
    handles[(usize)nh] = cloned;
    return ok(nh);
}


syscall_result_t rabbitbone_sys_dup2(u64 src_handle, u64 target_handle, u64 flags64) {
    sys_handle_t *handles = active_handles();
    usize src = (usize)src_handle;
    usize target = (usize)target_handle;
    if (!valid_handle(handles, src) || target >= SYSCALL_MAX_HANDLES || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 flags = (u32)flags64;
    if (flags & ~RABBITBONE_FD_CLOEXEC) return err(VFS_ERR_INVAL);
    if (src == target) {
        handles[target].flags = flags;
        return ok((i64)target);
    }
    sys_handle_t cloned = handles[src];
    cloned.used = true;
    cloned.flags = flags;
    if (!handle_table_can_add_kind(handles, target, cloned.kind)) return err(VFS_ERR_NOSPC);
    if (!handle_retain_resource(&cloned)) return err(VFS_ERR_NOENT);
    if (handles[target].used) close_handle(handles, target);
    handles[target] = cloned;
    return ok((i64)target);
}

syscall_result_t rabbitbone_sys_poll(u64 handle, u64 events64) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || events64 == 0 || events64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 events = (u32)events64;
    if (events & ~(RABBITBONE_POLL_READ | RABBITBONE_POLL_WRITE | RABBITBONE_POLL_HUP)) return err(VFS_ERR_INVAL);
    u32 ready = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN) {
        if ((events & RABBITBONE_POLL_READ) && keyboard_pending() > 0) ready |= RABBITBONE_POLL_READ;
        return ok((i64)ready);
    }
    if (handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        if (events & RABBITBONE_POLL_WRITE) ready |= RABBITBONE_POLL_WRITE;
        return ok((i64)ready);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return ok(RABBITBONE_POLL_BADF);
        u64 pf = spin_lock_irqsave(&p->lock);
        if (handles[h].kind == SYS_HANDLE_PIPE_READ) {
            if ((events & RABBITBONE_POLL_READ) && p->len > 0) ready |= RABBITBONE_POLL_READ;
            if (p->write_refs == 0) ready |= RABBITBONE_POLL_HUP;
        } else {
            if ((events & RABBITBONE_POLL_WRITE) && p->read_refs > 0 && p->len < SYSCALL_PIPE_BUFFER) ready |= RABBITBONE_POLL_WRITE;
            if (p->read_refs == 0) ready |= RABBITBONE_POLL_HUP;
        }
        spin_unlock_irqrestore(&p->lock, pf);
        return ok((i64)ready);
    }
    refresh_handle_stat(&handles[h]);
    if (handles[h].type == VFS_NODE_DIR) return ok(0);
    if ((events & RABBITBONE_POLL_READ) && (handles[h].type == VFS_NODE_FILE || handles[h].type == VFS_NODE_DEV)) {
        if (handles[h].type == VFS_NODE_DEV || handles[h].offset < handles[h].size) ready |= RABBITBONE_POLL_READ;
    }
    if ((events & RABBITBONE_POLL_WRITE) && (handles[h].type == VFS_NODE_FILE || handles[h].type == VFS_NODE_DEV)) ready |= RABBITBONE_POLL_WRITE;
    return ok((i64)ready);
}

syscall_result_t rabbitbone_sys_fdctl(u64 handle, u64 op, u64 flags64) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 flags = (u32)flags64;
    if (flags & ~RABBITBONE_FD_CLOEXEC) return err(VFS_ERR_INVAL);
    switch ((u32)op) {
        case RABBITBONE_FDCTL_GET:
            return ok((i64)handles[h].flags);
        case RABBITBONE_FDCTL_SET:
            handles[h].flags = flags;
            return ok((i64)handles[h].flags);
        default:
            return err(VFS_ERR_INVAL);
    }
}

syscall_result_t rabbitbone_sys_tell(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_VFS) {
        sys_file_t *f = handle_file(&handles[h]);
        if (!f) return err(VFS_ERR_NOENT);
        u64 ff = spin_lock_irqsave(&f->lock);
        sync_handle_from_file(&handles[h], f);
        i64 off = (i64)f->offset;
        spin_unlock_irqrestore(&f->lock, ff);
        return ok(off);
    }
    return ok((i64)handles[h].offset);
}

syscall_result_t rabbitbone_sys_fstat(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN || handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        st.type = VFS_NODE_DEV;
        st.size = 0;
        st.mode = handles[h].kind == SYS_HANDLE_CONSOLE_IN ? 0400u : 0200u;
        st.inode = handles[h].inode;
        st.fs_id = handles[h].fs_id;
        return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        rabbitbone_pipeinfo_t snap;
        memset(&snap, 0, sizeof(snap));
        pipe_snapshot(p, &snap);
        st.type = VFS_NODE_DEV;
        st.size = snap.bytes_available;
        st.mode = handles[h].kind == SYS_HANDLE_PIPE_READ ? 0400u : 0200u;
        st.inode = snap.pipe_id;
        st.fs_id = handles[h].fs_id;
        handles[h].size = snap.bytes_available;
        return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    vfs_status_t vs = vfs_stat_ref(&f->ref, &st);
    if (vs != VFS_OK) return err(vs);
    u64 ff = spin_lock_irqsave(&f->lock);
    f->type = st.type;
    f->size = st.size;
    f->inode = st.inode;
    f->fs_id = st.fs_id;
    f->ref.type = st.type;
    f->ref.size = st.size;
    sync_handle_from_file(&handles[h], f);
    spin_unlock_irqrestore(&f->lock, ff);
    return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_fdinfo(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        rabbitbone_pipeinfo_t snap;
        memset(&snap, 0, sizeof(snap));
        pipe_snapshot(p, &snap);
        handles[h].size = snap.bytes_available;
        handles[h].inode = snap.pipe_id;
    }
    rabbitbone_fdinfo_t info;
    memset(&info, 0, sizeof(info));
    info.handle = (u32)h;
    info.type = (u32)handles[h].type;
    info.offset = handles[h].offset;
    info.size = handles[h].size;
    info.inode = handles[h].inode;
    info.fs_id = handles[h].fs_id;
    info.flags = handles[h].flags;
    info.open_flags = 0;
    sys_file_t *vf = handle_file(&handles[h]);
    if (vf) {
        u64 ff = spin_lock_irqsave(&vf->lock);
        info.open_flags = vf->open_flags;
        spin_unlock_irqrestore(&vf->lock, ff);
    }
    strncpy(info.path, handles[h].path, sizeof(info.path) - 1u);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

typedef struct readdir_ctx {
    u64 target;
    u64 current;
    bool found;
    rabbitbone_dirent_t out;
} readdir_ctx_t;

static bool readdir_pick(const vfs_dirent_t *entry, void *raw) {
    readdir_ctx_t *ctx = (readdir_ctx_t *)raw;
    if (ctx->current == ctx->target) {
        memset(&ctx->out, 0, sizeof(ctx->out));
        strncpy(ctx->out.name, entry->name, sizeof(ctx->out.name) - 1u);
        ctx->out.type = (u32)entry->type;
        ctx->out.size = entry->size;
        ctx->out.inode = entry->inode;
        ctx->found = true;
        return false;
    }
    ++ctx->current;
    return true;
}

syscall_result_t rabbitbone_sys_readdir(u64 handle, u64 index, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].type != VFS_NODE_DIR) return err(VFS_ERR_NOTDIR);
    readdir_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.target = index;
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    vfs_status_t vs = vfs_list_ref(&f->ref, readdir_pick, &ctx);
    if (vs != VFS_OK) return err(vs);
    if (!ctx.found) {
        rabbitbone_dirent_t zero;
        memset(&zero, 0, sizeof(zero));
        return copy_out_buf(out_ptr, &zero, sizeof(zero)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    ctx.out.fs_id = handles[h].fs_id;
    return copy_out_buf(out_ptr, &ctx.out, sizeof(ctx.out)) ? ok(1) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_pipe(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    if (count_pipe_endpoints(handles) + 2u > SYSCALL_PIPE_PER_PROCESS_ENDPOINT_CAP) return err(VFS_ERR_NOSPC);
    sys_pipe_t *p = alloc_pipe();
    if (!p) return err(VFS_ERR_NOSPC);
    i64 rh = alloc_pipe_handle(handles, SYS_HANDLE_PIPE_READ, p->id);
    if (rh < 0) { release_pipe(p); return err(VFS_ERR_NOSPC); }
    i64 wh = alloc_pipe_handle(handles, SYS_HANDLE_PIPE_WRITE, p->id);
    if (wh < 0) {
        close_handle(handles, (usize)rh);
        return err(VFS_ERR_NOSPC);
    }
    u32 pair[2] = { (u32)rh, (u32)wh };
    if (!copy_out_buf(out_ptr, pair, sizeof(pair))) {
        close_handle(handles, (usize)rh);
        close_handle(handles, (usize)wh);
        return err(VFS_ERR_INVAL);
    }
    return ok(0);
}

syscall_result_t rabbitbone_sys_pipeinfo(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || (handles[h].kind != SYS_HANDLE_PIPE_READ && handles[h].kind != SYS_HANDLE_PIPE_WRITE)) return err(VFS_ERR_INVAL);
    sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
    if (!p) return err(VFS_ERR_NOENT);
    rabbitbone_pipeinfo_t info;
    memset(&info, 0, sizeof(info));
    pipe_snapshot(p, &info);
    info.endpoint = handles[h].kind == SYS_HANDLE_PIPE_READ ? 1u : 2u;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && handles[i].pipe_id == p->id) {
            if (handles[i].kind == SYS_HANDLE_PIPE_READ && !info.read_handle) info.read_handle = (u32)i;
            if (handles[i].kind == SYS_HANDLE_PIPE_WRITE && !info.write_handle) info.write_handle = (u32)i;
        }
    }
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

