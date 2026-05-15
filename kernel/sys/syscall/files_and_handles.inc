static sys_file_t *file_acquire(u32 id) {
    if (!id) return 0;
    u64 flags = spin_lock_irqsave(&file_table_lock);
    sys_file_t *f = file_by_id(id);
    if (!f || f->refs == 0xffffffffu) {
        spin_unlock_irqrestore(&file_table_lock, flags);
        return 0;
    }
    ++f->refs;
    spin_unlock_irqrestore(&file_table_lock, flags);
    return f;
}

static void file_release(u32 id) {
    if (!id) return;
    vfs_node_ref_t ref_to_release;
    bool release_ref = false;
    memset(&ref_to_release, 0, sizeof(ref_to_release));
    u64 flags = spin_lock_irqsave(&file_table_lock);
    sys_file_t *f = file_by_id(id);
    if (!f) {
        spin_unlock_irqrestore(&file_table_lock, flags);
        return;
    }
    if (f->refs == 0) PANIC("open-file object double close");
    --f->refs;
    if (f->refs == 0) {
        ref_to_release = f->ref;
        release_ref = true;
        memset(f, 0, sizeof(*f));
    }
    spin_unlock_irqrestore(&file_table_lock, flags);
    if (release_ref) vfs_release_ref(&ref_to_release);
}

static bool file_retain(u32 id) {
    return file_acquire(id) != 0;
}

static void file_put(sys_file_t *f) {
    if (!f) return;
    file_release(f->id);
}

static bool file_id_in_use_locked(u32 id) {
    if (!id) return true;
    for (usize i = 0; i < SYSCALL_FILE_CAP; ++i) {
        if (files[i].used && files[i].id == id) return true;
    }
    return false;
}

static u32 alloc_file_id_locked(void) {
    for (usize tries = 0; tries < SYSCALL_FILE_CAP + 1u; ++tries) {
        u32 id = (u32)next_file_id++;
        if (id == 0) continue;
        if (!file_id_in_use_locked(id)) return id;
    }
    return 0;
}

static usize count_vfs_handles(const sys_handle_t *handles) {
    if (!handles) return SYSCALL_MAX_HANDLES;
    usize count = 0;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && handles[i].kind == SYS_HANDLE_VFS) ++count;
    }
    return count;
}

static usize count_pipe_endpoints(const sys_handle_t *handles) {
    if (!handles) return SYSCALL_MAX_HANDLES;
    usize count = 0;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && (handles[i].kind == SYS_HANDLE_PIPE_READ || handles[i].kind == SYS_HANDLE_PIPE_WRITE)) ++count;
    }
    return count;
}

static bool handle_kind_is_pipe_endpoint(u32 kind) {
    return kind == SYS_HANDLE_PIPE_READ || kind == SYS_HANDLE_PIPE_WRITE;
}

static bool handle_table_can_add_kind(const sys_handle_t *handles, usize target_fd, u32 kind) {
    if (!handles || target_fd >= SYSCALL_MAX_HANDLES) return false;
    if (kind == SYS_HANDLE_VFS) {
        usize count = count_vfs_handles(handles);
        if (handles[target_fd].used && handles[target_fd].kind == SYS_HANDLE_VFS && count > 0) --count;
        return count < SYSCALL_FILE_PER_PROCESS_CAP;
    }
    if (handle_kind_is_pipe_endpoint(kind)) {
        usize count = count_pipe_endpoints(handles);
        if (handles[target_fd].used && handle_kind_is_pipe_endpoint(handles[target_fd].kind) && count > 0) --count;
        return count < SYSCALL_PIPE_PER_PROCESS_ENDPOINT_CAP;
    }
    return true;
}

static sys_file_t *alloc_file_object(const char *path, const vfs_stat_t *st, u32 open_flags) {
    if (!path || !st) return 0;
    vfs_node_ref_t ref;
    if (vfs_get_ref(path, &ref) != VFS_OK) return 0;
    if (!vfs_retain_ref(&ref)) return 0;
    u64 flags = spin_lock_irqsave(&file_table_lock);
    for (usize i = 0; i < SYSCALL_FILE_CAP; ++i) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(files[i]));
            u32 id = alloc_file_id_locked();
            if (!id) {
                spin_unlock_irqrestore(&file_table_lock, flags);
                vfs_release_ref(&ref);
                return 0;
            }
            files[i].used = true;
            files[i].id = id;
            files[i].refs = 1u;
            strlcpy(files[i].path, path, sizeof(files[i].path));
            files[i].offset = (open_flags & RABBITBONE_O_APPEND) ? st->size : 0;
            files[i].open_flags = open_flags;
            files[i].type = st->type;
            files[i].size = st->size;
            files[i].inode = st->inode;
            files[i].fs_id = st->fs_id;
            files[i].ref = ref;
            spinlock_init(&files[i].lock);
            spin_unlock_irqrestore(&file_table_lock, flags);
            return &files[i];
        }
    }
    spin_unlock_irqrestore(&file_table_lock, flags);
    vfs_release_ref(&ref);
    return 0;
}

static void sync_handle_from_file(sys_handle_t *h, const sys_file_t *f) {
    if (!h || !f) return;
    h->type = f->type;
    h->size = f->size;
    h->inode = f->inode;
    h->fs_id = f->fs_id;
    h->offset = f->offset;
    strlcpy(h->path, f->path, sizeof(h->path));
}

static void refresh_handle_stat(sys_handle_t *h) {
    if (!h || !h->used || h->kind != SYS_HANDLE_VFS) return;
    sys_file_t *f = handle_file_acquire(h);
    if (!f) return;
    vfs_stat_t st;
    if (vfs_stat_ref(&f->ref, &st) == VFS_OK) {
        u64 flags = spin_lock_irqsave(&f->lock);
        f->type = st.type;
        f->size = st.size;
        f->inode = st.inode;
        f->fs_id = st.fs_id;
        f->ref.type = st.type;
        f->ref.size = st.size;
        sync_handle_from_file(h, f);
        spin_unlock_irqrestore(&f->lock, flags);
    }
    file_put(f);
}

static i64 alloc_handle(sys_handle_t *handles, const char *path, const vfs_stat_t *st, u32 open_flags) {
    if (count_vfs_handles(handles) >= SYSCALL_FILE_PER_PROCESS_CAP) return -1;
    sys_file_t *file = alloc_file_object(path, st, open_flags);
    if (!file) return -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) {
            memset(&handles[i], 0, sizeof(handles[i]));
            handles[i].used = true;
            handles[i].kind = SYS_HANDLE_VFS;
            handles[i].file_id = file->id;
            handles[i].flags = open_flags_to_fd_flags(open_flags);
            sync_handle_from_file(&handles[i], file);
            return (i64)i;
        }
    }
    file_release(file->id);
    return -1;
}

static bool valid_handle(sys_handle_t *handles, usize h) {
    return h < SYSCALL_MAX_HANDLES && handles[h].used;
}

static sys_pipe_t *pipe_by_id(u32 id) {
    if (!id) return 0;
    for (usize i = 0; i < SYSCALL_PIPE_CAP; ++i) {
        if (pipes[i].used && pipes[i].id == id) return &pipes[i];
    }
    return 0;
}

static bool pipe_is_endpoint(const sys_handle_t *h) {
    return h && h->used && (h->kind == SYS_HANDLE_PIPE_READ || h->kind == SYS_HANDLE_PIPE_WRITE);
}

static void release_pipe_locked(sys_pipe_t *p) {
    if (!p) return;
    u8 *data = p->data;
    memset(p, 0, sizeof(*p));
    if (data) kfree(data);
}

static bool pipe_ref(sys_pipe_t *p, sys_handle_kind_t kind) {
    if (!p || !p->used) return false;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (kind == SYS_HANDLE_PIPE_READ) {
        if (p->read_refs == 0xffffffffu) { spin_unlock_irqrestore(&p->lock, flags); return false; }
        ++p->read_refs;
    } else if (kind == SYS_HANDLE_PIPE_WRITE) {
        if (p->write_refs == 0xffffffffu) { spin_unlock_irqrestore(&p->lock, flags); return false; }
        ++p->write_refs;
    } else {
        spin_unlock_irqrestore(&p->lock, flags);
        return false;
    }
    spin_unlock_irqrestore(&p->lock, flags);
    return true;
}

static void pipe_unref(sys_pipe_t *p, sys_handle_kind_t kind) {
    if (!p || !p->used) return;
    bool free_now = false;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (kind == SYS_HANDLE_PIPE_READ) {
        if (p->read_refs == 0) PANIC("pipe read endpoint double close");
        --p->read_refs;
    } else if (kind == SYS_HANDLE_PIPE_WRITE) {
        if (p->write_refs == 0) PANIC("pipe write endpoint double close");
        --p->write_refs;
    }
    free_now = p->read_refs == 0 && p->write_refs == 0;
    spin_unlock_irqrestore(&p->lock, flags);
    if (free_now && p->used && p->read_refs == 0 && p->write_refs == 0) release_pipe_locked(p);
}

static bool handle_retain_resource(const sys_handle_t *h) {
    if (!h || !h->used) return true;
    if (h->kind == SYS_HANDLE_VFS) return h->file_id && file_retain(h->file_id);
    if (!pipe_is_endpoint(h)) return true;
    u64 table_flags = spin_lock_irqsave(&pipe_table_lock);
    sys_pipe_t *p = pipe_by_id(h->pipe_id);
    bool ok = p && pipe_ref(p, (sys_handle_kind_t)h->kind);
    spin_unlock_irqrestore(&pipe_table_lock, table_flags);
    return ok;
}

static void handle_release_resource(sys_handle_t *h) {
    if (!h || !h->used) return;
    if (h->kind == SYS_HANDLE_VFS) {
        file_release(h->file_id);
        return;
    }
    if (!pipe_is_endpoint(h)) return;
    u64 table_flags = spin_lock_irqsave(&pipe_table_lock);
    sys_pipe_t *p = pipe_by_id(h->pipe_id);
    if (p) pipe_unref(p, (sys_handle_kind_t)h->kind);
    spin_unlock_irqrestore(&pipe_table_lock, table_flags);
}

static void close_handle(sys_handle_t *handles, usize h) {
    if (!handles || h >= SYSCALL_MAX_HANDLES || !handles[h].used) return;
    handle_release_resource(&handles[h]);
    memset(&handles[h], 0, sizeof(handles[h]));
}

static bool snapshot_valid(void *snapshot, usize snapshot_size) {
    return snapshot && snapshot_size >= SYSCALL_HANDLE_TABLE_BYTES;
}

static bool install_owned_handle(sys_handle_t *handles, u32 target_fd, sys_handle_t *owned) {
    if (!handles || !owned || !owned->used || target_fd >= SYSCALL_MAX_HANDLES) return false;
    if (!handle_table_can_add_kind(handles, target_fd, owned->kind)) return false;
    if (handles[target_fd].used) close_handle(handles, target_fd);
    handles[target_fd] = *owned;
    memset(owned, 0, sizeof(*owned));
    return true;
}

bool syscall_snapshot_open_vfs(void *snapshot, usize snapshot_size, u32 target_fd, const char *path, u32 flags, bool create_truncate) {
    if (!snapshot_valid(snapshot, snapshot_size) || !path || !*path || target_fd >= SYSCALL_MAX_HANDLES) return false;
    if (flags & ~RABBITBONE_FD_CLOEXEC) return false;
    if (!handle_table_can_add_kind((const sys_handle_t *)snapshot, target_fd, SYS_HANDLE_VFS)) return false;
    char resolved[SYSCALL_PATH_MAX];
    if (!copy_path_arg((u64)(uptr)path, resolved)) return false;
    if (create_truncate) {
        vfs_status_t cs = vfs_create(resolved, "", 0);
        if (cs != VFS_OK && cs != VFS_ERR_PERM) return false;
    }
    vfs_stat_t st;
    if (vfs_stat(resolved, &st) != VFS_OK || st.type == VFS_NODE_DIR) return false;
    sys_file_t *file = alloc_file_object(resolved, &st, RABBITBONE_O_RDONLY);
    if (!file) return false;
    sys_handle_t h;
    memset(&h, 0, sizeof(h));
    h.used = true;
    h.kind = SYS_HANDLE_VFS;
    h.flags = flags;
    h.file_id = file->id;
    sync_handle_from_file(&h, file);
    if (!install_owned_handle((sys_handle_t *)snapshot, target_fd, &h)) {
        file_release(file->id);
        return false;
    }
    return true;
}

bool syscall_snapshot_pipe_between(void *writer_snapshot, usize writer_size, u32 writer_fd, void *reader_snapshot, usize reader_size, u32 reader_fd) {
    if (!snapshot_valid(writer_snapshot, writer_size) || !snapshot_valid(reader_snapshot, reader_size)) return false;
    if (writer_fd >= SYSCALL_MAX_HANDLES || reader_fd >= SYSCALL_MAX_HANDLES) return false;
    if (writer_snapshot == reader_snapshot && writer_fd == reader_fd) return false;
    if (writer_snapshot == reader_snapshot) {
        sys_handle_t probe[SYSCALL_MAX_HANDLES];
        memcpy(probe, writer_snapshot, sizeof(probe));
        if (!handle_table_can_add_kind(probe, reader_fd, SYS_HANDLE_PIPE_READ)) return false;
        memset(&probe[reader_fd], 0, sizeof(probe[reader_fd]));
        probe[reader_fd].used = true;
        probe[reader_fd].kind = SYS_HANDLE_PIPE_READ;
        if (!handle_table_can_add_kind(probe, writer_fd, SYS_HANDLE_PIPE_WRITE)) return false;
    } else {
        if (!handle_table_can_add_kind((const sys_handle_t *)writer_snapshot, writer_fd, SYS_HANDLE_PIPE_WRITE)) return false;
        if (!handle_table_can_add_kind((const sys_handle_t *)reader_snapshot, reader_fd, SYS_HANDLE_PIPE_READ)) return false;
    }
    sys_pipe_t *p = alloc_pipe();
    if (!p) return false;
    sys_handle_t rh;
    sys_handle_t wh;
    memset(&rh, 0, sizeof(rh));
    memset(&wh, 0, sizeof(wh));
    rh.used = true;
    rh.kind = SYS_HANDLE_PIPE_READ;
    rh.type = VFS_NODE_DEV;
    rh.pipe_id = p->id;
    rh.fs_id = 0x50495045u;
    rh.inode = p->id;
    ksnprintf(rh.path, sizeof(rh.path), "pipe:[%u]:read", p->id);
    wh.used = true;
    wh.kind = SYS_HANDLE_PIPE_WRITE;
    wh.type = VFS_NODE_DEV;
    wh.pipe_id = p->id;
    wh.fs_id = 0x50495045u;
    wh.inode = p->id;
    ksnprintf(wh.path, sizeof(wh.path), "pipe:[%u]:write", p->id);
    if (!handle_retain_resource(&rh)) { release_pipe(p); return false; }
    if (!handle_retain_resource(&wh)) { handle_release_resource(&rh); return false; }
    if (!install_owned_handle((sys_handle_t *)reader_snapshot, reader_fd, &rh)) {
        handle_release_resource(&rh);
        handle_release_resource(&wh);
        return false;
    }
    if (!install_owned_handle((sys_handle_t *)writer_snapshot, writer_fd, &wh)) {
        close_handle((sys_handle_t *)reader_snapshot, reader_fd);
        handle_release_resource(&wh);
        return false;
    }
    return true;
}

static bool pipe_id_in_use_locked(u32 id) {
    if (!id) return true;
    for (usize i = 0; i < SYSCALL_PIPE_CAP; ++i) {
        if (pipes[i].used && pipes[i].id == id) return true;
    }
    return false;
}

static u32 alloc_pipe_id_locked(void) {
    for (usize tries = 0; tries < SYSCALL_PIPE_CAP + 1u; ++tries) {
        u32 id = (u32)next_pipe_id++;
        if (id == 0) continue;
        if (!pipe_id_in_use_locked(id)) return id;
    }
    return 0;
}

static sys_pipe_t *alloc_pipe(void) {
    u64 flags = spin_lock_irqsave(&pipe_table_lock);
    for (usize i = 0; i < SYSCALL_PIPE_CAP; ++i) {
        if (!pipes[i].used) {
            memset(&pipes[i], 0, sizeof(pipes[i]));
            pipes[i].data = (u8 *)kmalloc(SYSCALL_PIPE_BUFFER);
            if (!pipes[i].data) { spin_unlock_irqrestore(&pipe_table_lock, flags); return 0; }
            memset(pipes[i].data, 0, SYSCALL_PIPE_BUFFER);
            u32 id = alloc_pipe_id_locked();
            if (!id) { kfree(pipes[i].data); memset(&pipes[i], 0, sizeof(pipes[i])); spin_unlock_irqrestore(&pipe_table_lock, flags); return 0; }
            pipes[i].used = true;
            pipes[i].id = id;
            spinlock_init(&pipes[i].lock);
            spin_unlock_irqrestore(&pipe_table_lock, flags);
            return &pipes[i];
        }
    }
    spin_unlock_irqrestore(&pipe_table_lock, flags);
    return 0;
}

