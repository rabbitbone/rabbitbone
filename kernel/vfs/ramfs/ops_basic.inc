vfs_status_t ramfs_add_dir(ramfs_t *fs, const char *path) {
    if (!fs || !path) return VFS_ERR_INVAL;
    if (strcmp(path, "/") == 0) return VFS_OK;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) { spin_unlock_irqrestore(&fs->lock, flags); return st; }
    if (find_child(parent, leaf)) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_EXIST; }
    ramfs_node_t *n = dentry_create(fs, leaf, VFS_NODE_DIR);
    if (!n) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOSPC; }
    attach_child(parent, n);
    ++parent->obj->nlink;
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

vfs_status_t ramfs_add_file(ramfs_t *fs, const char *path, const void *data, usize size) {
    if (!fs || !path || (size && !data)) return VFS_ERR_INVAL;
    if (size > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) { spin_unlock_irqrestore(&fs->lock, flags); return st; }
    ramfs_node_t *n = find_child(parent, leaf);
    bool is_new = !n;
    if (n && node_type(n) != VFS_NODE_FILE) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_ISDIR; }
    usize old_cap = n ? n->obj->capacity : 0u;
    usize delta = size > old_cap ? size - old_cap : 0u;
    if ((is_new && !ramfs_can_add_node(fs)) || !ramfs_can_add_bytes(fs, delta)) {
        spin_unlock_irqrestore(&fs->lock, flags);
        return VFS_ERR_NOSPC;
    }
    u8 *copy = 0;
    if (size) {
        copy = (u8 *)kmalloc(size);
        if (!copy) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOMEM; }
        memcpy(copy, data, size);
    }
    if (!n) {
        n = dentry_create(fs, leaf, VFS_NODE_FILE);
        if (!n) { kfree(copy); spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOSPC; }
        attach_child(parent, n);
    }
    if (fs->bytes_used >= old_cap) fs->bytes_used -= old_cap;
    else fs->bytes_used = 0;
    fs->bytes_used += size;
    kfree(n->obj->data);
    n->obj->data = copy;
    n->obj->size = size;
    n->obj->capacity = size;
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t stat_node(vfs_mount_t *mnt, ramfs_node_t *n, vfs_stat_t *out) {
    if (!mnt || !n || !n->obj || !out) return VFS_ERR_INVAL;
    memset(out, 0, sizeof(*out));
    out->type = n->obj->type;
    out->size = n->obj->size;
    out->mode = n->obj->mode;
    out->inode = n->obj->inode;
    out->fs_id = mnt->fs_id;
    out->nlink = n->obj->nlink;
    out->uid = n->obj->uid;
    out->gid = n->obj->gid;
    return VFS_OK;
}

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = resolve(fs, path);
    vfs_status_t st = n ? stat_node(mnt, n, out) : VFS_ERR_NOENT;
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_lstat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = resolve_nofollow(fs, path);
    vfs_status_t st = n ? stat_node(mnt, n, out) : VFS_ERR_NOENT;
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_stat_inode(vfs_mount_t *mnt, u32 ino, vfs_stat_t *out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = node_find_inode(fs->root, ino);
    vfs_status_t st = n ? stat_node(mnt, n, out) : VFS_ERR_NOENT;
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t read_node(ramfs_node_t *n, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    if (offset >= (u64)n->obj->size) return VFS_OK;
    usize off = (usize)offset;
    usize take = n->obj->size - off;
    if (take > size) take = size;
    if (take) memcpy(buffer, n->obj->data + off, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = read_node(resolve(fs, path), offset, buffer, size, read_out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_read_inode(vfs_mount_t *mnt, u32 ino, u64 offset, void *buffer, usize size, usize *read_out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = read_node(node_find_inode(fs->root, ino), offset, buffer, size, read_out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t write_node(ramfs_t *fs, ramfs_node_t *n, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (offset > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (size > RAMFS_MAX_FILE_SIZE - (usize)offset) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    usize off = (usize)offset;
    usize end = off + size;
    if (end < off || end > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (end > n->obj->capacity) {
        usize cap = n->obj->capacity ? n->obj->capacity : 64u;
        while (cap < end) {
            if (cap >= RAMFS_MAX_FILE_SIZE) return VFS_ERR_NOSPC;
            if (cap > ((usize)-1) / 2u) return VFS_ERR_NOSPC;
            cap *= 2u;
            if (cap > RAMFS_MAX_FILE_SIZE) cap = RAMFS_MAX_FILE_SIZE;
        }
        usize delta = cap - n->obj->capacity;
        if (!ramfs_can_add_bytes(fs, delta)) return VFS_ERR_NOSPC;
        u8 *new_data = (u8 *)kmalloc(cap);
        if (!new_data) return VFS_ERR_NOMEM;
        if (n->obj->data) memcpy(new_data, n->obj->data, n->obj->size);
        if (cap > n->obj->size) memset(new_data + n->obj->size, 0, cap - n->obj->size);
        kfree(n->obj->data);
        n->obj->data = new_data;
        fs->bytes_used += delta;
        n->obj->capacity = cap;
    }
    if (size) memcpy(n->obj->data + off, buffer, size);
    if (end > n->obj->size) n->obj->size = end;
    if (written_out) *written_out = size;
    return VFS_OK;
}

static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = write_node(fs, resolve(fs, path), offset, buffer, size, written_out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_write_inode(vfs_mount_t *mnt, u32 ino, u64 offset, const void *buffer, usize size, usize *written_out) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = write_node(fs, node_find_inode(fs->root, ino), offset, buffer, size, written_out);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t list_node(ramfs_node_t *n, vfs_dir_iter_fn fn, void *ctx) {
    if (!fn) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    for (ramfs_node_t *c = n->obj->children; c; c = c->next) {
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, c->name, sizeof(de.name) - 1u);
        de.type = c->obj->type;
        de.size = c->obj->size;
        de.inode = c->obj->inode;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = list_node(resolve(fs, path), fn, ctx);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_list_inode(vfs_mount_t *mnt, u32 ino, vfs_dir_iter_fn fn, void *ctx) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = list_node(node_find_inode(fs->root, ino), fn, ctx);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_mkdir(vfs_mount_t *mnt, const char *path) { return ramfs_add_dir((ramfs_t *)mnt->ctx, path); }
static vfs_status_t op_create(vfs_mount_t *mnt, const char *path, const void *data, usize size) { return ramfs_add_file((ramfs_t *)mnt->ctx, path, data, size); }

static vfs_status_t op_readlink(vfs_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!buffer && size) return VFS_ERR_INVAL;
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = resolve_nofollow(fs, path);
    if (!n) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOENT; }
    if (node_type(n) != VFS_NODE_SYMLINK) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_INVAL; }
    usize take = n->obj->size;
    if (take > size) take = size;
    if (take) memcpy(buffer, n->obj->data, take);
    if (read_out) *read_out = take;
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t op_symlink(vfs_mount_t *mnt, const char *target, const char *link_path) {
    if (!target || !target[0]) return VFS_ERR_INVAL;
    usize len = strnlen(target, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX) return VFS_ERR_INVAL;
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, link_path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) { spin_unlock_irqrestore(&fs->lock, flags); return st; }
    if (find_child(parent, leaf)) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_EXIST; }
    if (!ramfs_can_add_node(fs) || !ramfs_can_add_bytes(fs, len)) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOSPC; }
    ramfs_node_t *n = dentry_create(fs, leaf, VFS_NODE_SYMLINK);
    if (!n) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOSPC; }
    n->obj->data = (u8 *)kmalloc(len);
    if (!n->obj->data) { free_dentry(fs, n); spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOMEM; }
    memcpy(n->obj->data, target, len);
    n->obj->size = len;
    n->obj->capacity = len;
    fs->bytes_used += len;
    attach_child(parent, n);
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t op_link(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *src = resolve_nofollow(fs, old_path);
    if (!src) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOENT; }
    if (node_type(src) == VFS_NODE_DIR) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_PERM; }
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, new_path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) { spin_unlock_irqrestore(&fs->lock, flags); return st; }
    if (find_child(parent, leaf)) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_EXIST; }
    ramfs_node_t *alias = dentry_alias_create(fs, leaf, src->obj);
    if (!alias) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOSPC; }
    attach_child(parent, alias);
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t op_unlink(vfs_mount_t *mnt, const char *path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) { spin_unlock_irqrestore(&fs->lock, flags); return st; }
    ramfs_node_t *prev = 0;
    for (ramfs_node_t *c = parent->obj->children; c; c = c->next) {
        if (strcmp(c->name, leaf) == 0) {
            if (node_type(c) == VFS_NODE_DIR && c->obj->children) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOTEMPTY; }
            if (prev) prev->next = c->next;
            else parent->obj->children = c->next;
            if (node_type(c) == VFS_NODE_DIR && parent->obj->nlink > 0) --parent->obj->nlink;
            c->next = 0;
            free_dentry(fs, c);
            spin_unlock_irqrestore(&fs->lock, flags);
            return VFS_OK;
        }
        prev = c;
    }
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_ERR_NOENT;
}

static vfs_status_t truncate_node(ramfs_t *fs, ramfs_node_t *n, u64 size64) {
    if (size64 > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    usize size = (usize)size64;
    if (size > n->obj->capacity) {
        usize cap = n->obj->capacity ? n->obj->capacity : 64u;
        while (cap < size) {
            if (cap >= RAMFS_MAX_FILE_SIZE) return VFS_ERR_NOSPC;
            if (cap > ((usize)-1) / 2u) return VFS_ERR_NOSPC;
            cap *= 2u;
            if (cap > RAMFS_MAX_FILE_SIZE) cap = RAMFS_MAX_FILE_SIZE;
        }
        usize delta = cap - n->obj->capacity;
        if (!ramfs_can_add_bytes(fs, delta)) return VFS_ERR_NOSPC;
        u8 *new_data = (u8 *)kmalloc(cap);
        if (!new_data) return VFS_ERR_NOMEM;
        if (n->obj->data) memcpy(new_data, n->obj->data, n->obj->size);
        if (cap > n->obj->size) memset(new_data + n->obj->size, 0, cap - n->obj->size);
        kfree(n->obj->data);
        n->obj->data = new_data;
        n->obj->capacity = cap;
        fs->bytes_used += delta;
    } else if (size < n->obj->size && n->obj->data) {
        memset(n->obj->data + size, 0, n->obj->size - size);
    }
    n->obj->size = size;
    return VFS_OK;
}

static vfs_status_t op_truncate(vfs_mount_t *mnt, const char *path, u64 size64) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = truncate_node(fs, resolve(fs, path), size64);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_truncate_inode(vfs_mount_t *mnt, u32 ino, u64 size64) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = truncate_node(fs, node_find_inode(fs->root, ino), size64);
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_chmod(vfs_mount_t *mnt, const char *path, u32 mode) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = resolve_nofollow(fs, path);
    if (!n || !n->obj) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOENT; }
    n->obj->mode = mode & 07777u;
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t op_chown(vfs_mount_t *mnt, const char *path, u32 uid, u32 gid) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *n = resolve_nofollow(fs, path);
    if (!n || !n->obj) { spin_unlock_irqrestore(&fs->lock, flags); return VFS_ERR_NOENT; }
    n->obj->uid = uid;
    n->obj->gid = gid;
    spin_unlock_irqrestore(&fs->lock, flags);
    return VFS_OK;
}

static vfs_status_t op_sync_inode(vfs_mount_t *mnt, u32 ino, bool data_only) {
    (void)data_only;
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    vfs_status_t st = node_find_inode(fs->root, ino) ? VFS_OK : VFS_ERR_NOENT;
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static vfs_status_t op_sync(vfs_mount_t *mnt) {
    (void)mnt;
    return VFS_OK;
}
