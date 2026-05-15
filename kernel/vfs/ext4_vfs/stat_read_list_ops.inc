static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = lookup_path_retry(ctx, "stat", path, &inode, &ino);
    if (st != EXT4_OK) return map_status(st);
    vfs_node_type_t type;
    if (!ext4_type_checked(&inode, &type)) return VFS_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->size = ext4_inode_size(&inode);
    out->mode = le16(inode.i_mode);
    out->uid = le16(inode.i_uid);
    out->gid = le16(inode.i_gid);
    out->inode = ino;
    out->fs_id = mnt->fs_id;
    out->nlink = le16(inode.i_links_count);
    return VFS_OK;
}

static vfs_status_t op_lstat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = lstat_path_retry(ctx, "lstat", path, &inode, &ino);
    if (st != EXT4_OK) return map_status(st);
    vfs_node_type_t type;
    if (!ext4_type_checked(&inode, &type)) return VFS_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->size = ext4_inode_size(&inode);
    out->mode = le16(inode.i_mode);
    out->uid = le16(inode.i_uid);
    out->gid = le16(inode.i_gid);
    out->inode = ino;
    out->fs_id = mnt->fs_id;
    out->nlink = le16(inode.i_links_count);
    return VFS_OK;
}

static vfs_status_t op_stat_inode(vfs_mount_t *mnt, u32 ino, vfs_stat_t *out) {
    if (!out || ino == 0) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = read_inode_retry(ctx, "stat_inode", ino, &inode);
    if (st != EXT4_OK) return map_status(st);
    vfs_node_type_t type;
    if (!ext4_type_checked(&inode, &type)) return VFS_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->size = ext4_inode_size(&inode);
    out->mode = le16(inode.i_mode);
    out->uid = le16(inode.i_uid);
    out->gid = le16(inode.i_gid);
    out->inode = ino;
    out->fs_id = mnt->fs_id;
    out->nlink = le16(inode.i_links_count);
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = lookup_path_retry(ctx, "read.lookup", path, &inode, 0);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_read_file(&ctx->mount, &inode, offset, buffer, size, read_out);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "read", st)) {
        st = ext4_lookup_path(&ctx->mount, path, &inode, 0);
        if (st == EXT4_OK) st = ext4_read_file(&ctx->mount, &inode, offset, buffer, size, read_out);
    }
    return map_status(st);
}

static vfs_status_t op_read_inode(vfs_mount_t *mnt, u32 ino, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (ino == 0) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = read_inode_retry(ctx, "read_inode.lookup", ino, &inode);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_read_file(&ctx->mount, &inode, offset, buffer, size, read_out);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "read_inode", st)) {
        st = ext4_read_inode(&ctx->mount, ino, &inode);
        if (st == EXT4_OK) st = ext4_read_file(&ctx->mount, &inode, offset, buffer, size, read_out);
    }
    return map_status(st);
}

typedef struct list_bridge_ctx {
    vfs_dir_iter_fn fn;
    void *ctx;
    ext4_status_t status;
} list_bridge_ctx_t;

static bool list_bridge(const ext4_dirent_t *e, void *p) {
    if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) return true;
    if (e->file_type != 1u && e->file_type != 2u && e->file_type != 7u) return false;
    list_bridge_ctx_t *b = (list_bridge_ctx_t *)p;
    usize n = strlen(e->name);
    if (n >= sizeof(((vfs_dirent_t *)0)->name)) {
        b->status = EXT4_ERR_RANGE;
        return false;
    }
    vfs_dirent_t de;
    memset(&de, 0, sizeof(de));
    memcpy(de.name, e->name, n + 1u);
    de.type = e->file_type == 2u ? VFS_NODE_DIR : (e->file_type == 7u ? VFS_NODE_SYMLINK : VFS_NODE_FILE);
    de.inode = e->inode;
    return b->fn(&de, b->ctx);
}

static ext4_status_t list_inode_loaded_ext4(ext4_vfs_ctx_t *ctx, ext4_inode_disk_t *inode, vfs_dir_iter_fn fn, void *user) {
    if (!ctx || !inode || !fn) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_dir(inode)) {
        if (ext4_inode_is_regular(inode)) return EXT4_ERR_NOT_DIR;
        return EXT4_ERR_UNSUPPORTED;
    }
    list_bridge_ctx_t bridge = { fn, user, EXT4_OK };
    ext4_status_t st = ext4_list_dir(&ctx->mount, inode, list_bridge, &bridge);
    if (st == EXT4_OK && bridge.status != EXT4_OK) st = bridge.status;
    return st;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *user) {
    if (!fn) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = lookup_path_retry(ctx, "list.lookup", path, &inode, 0);
    if (st != EXT4_OK) return map_status(st);
    st = list_inode_loaded_ext4(ctx, &inode, fn, user);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "list", st)) {
        st = ext4_lookup_path(&ctx->mount, path, &inode, 0);
        if (st == EXT4_OK) st = list_inode_loaded_ext4(ctx, &inode, fn, user);
    }
    return map_status(st);
}

