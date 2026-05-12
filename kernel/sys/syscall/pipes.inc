static void release_pipe(sys_pipe_t *p) {
    if (!p) return;
    u64 flags = spin_lock_irqsave(&pipe_table_lock);
    release_pipe_locked(p);
    spin_unlock_irqrestore(&pipe_table_lock, flags);
}

static i64 alloc_pipe_handle(sys_handle_t *handles, sys_handle_kind_t kind, u32 pipe_id) {
    if (kind != SYS_HANDLE_PIPE_READ && kind != SYS_HANDLE_PIPE_WRITE) return -1;
    sys_handle_t h;
    memset(&h, 0, sizeof(h));
    h.used = true;
    h.kind = (u32)kind;
    h.type = VFS_NODE_DEV;
    h.pipe_id = pipe_id;
    h.fs_id = 0x50495045u;
    h.inode = pipe_id;
    const char *suffix = kind == SYS_HANDLE_PIPE_READ ? ":read" : ":write";
    ksnprintf(h.path, sizeof(h.path), "pipe:[%u]%s", pipe_id, suffix);
    if (!handle_retain_resource(&h)) return -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) {
            handles[i] = h;
            return (i64)i;
        }
    }
    handle_release_resource(&h);
    return -1;
}

typedef enum pipe_write_status {
    PIPE_WRITE_OK = 0,
    PIPE_WRITE_FULL = 1,
    PIPE_WRITE_CLOSED = 2,
    PIPE_WRITE_INVAL = 3,
} pipe_write_status_t;

static pipe_write_status_t pipe_write_bytes(sys_pipe_t *p, const u8 *src, usize len, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!p || !p->data || !src || !len) return PIPE_WRITE_INVAL;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (p->read_refs == 0) { spin_unlock_irqrestore(&p->lock, flags); return PIPE_WRITE_CLOSED; }
    usize room = SYSCALL_PIPE_BUFFER - p->len;
    if (room == 0) { spin_unlock_irqrestore(&p->lock, flags); return PIPE_WRITE_FULL; }
    if (len > room) len = room;
    for (usize i = 0; i < len; ++i) {
        usize pos = (p->read_pos + p->len) % SYSCALL_PIPE_BUFFER;
        p->data[pos] = src[i];
        ++p->len;
    }
    p->total_written += len;
    if (written_out) *written_out = len;
    spin_unlock_irqrestore(&p->lock, flags);
    return PIPE_WRITE_OK;
}

static usize pipe_read_bytes(sys_pipe_t *p, u8 *dst, usize len) {
    if (!p || !p->data || !dst || !len) return 0;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (len > p->len) len = p->len;
    for (usize i = 0; i < len; ++i) {
        dst[i] = p->data[p->read_pos];
        p->read_pos = (p->read_pos + 1u) % SYSCALL_PIPE_BUFFER;
        --p->len;
    }
    p->total_read += len;
    spin_unlock_irqrestore(&p->lock, flags);
    return len;
}

static void pipe_snapshot(sys_pipe_t *p, aurora_pipeinfo_t *info) {
    if (!p || !info) return;
    u64 flags = spin_lock_irqsave(&p->lock);
    info->pipe_id = p->id;
    info->capacity = SYSCALL_PIPE_BUFFER;
    info->bytes_available = p->len;
    info->total_read = p->total_read;
    info->total_written = p->total_written;
    info->read_refs = p->read_refs;
    info->write_refs = p->write_refs;
    spin_unlock_irqrestore(&p->lock, flags);
}

