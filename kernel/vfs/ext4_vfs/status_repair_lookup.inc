static u16 le16(u16 v) { return v; }
static u32 le32(u32 v) { return v; }

static bool ext4_type_checked(const ext4_inode_disk_t *inode, vfs_node_type_t *type_out) {
    if (!inode || !type_out) return false;
    if (ext4_inode_is_dir(inode)) { *type_out = VFS_NODE_DIR; return true; }
    if (ext4_inode_is_regular(inode)) { *type_out = VFS_NODE_FILE; return true; }
    if (ext4_inode_is_symlink(inode)) { *type_out = VFS_NODE_SYMLINK; return true; }
    return false;
}

static vfs_status_t map_status(ext4_status_t st) {
    switch (st) {
        case EXT4_OK: return VFS_OK;
        case EXT4_ERR_NOT_FOUND: return VFS_ERR_NOENT;
        case EXT4_ERR_NOT_DIR: return VFS_ERR_NOTDIR;
        case EXT4_ERR_NO_MEMORY: return VFS_ERR_NOMEM;
        case EXT4_ERR_EXIST: return VFS_ERR_EXIST;
        case EXT4_ERR_NOT_EMPTY: return VFS_ERR_NOTEMPTY;
        case EXT4_ERR_IO: return VFS_ERR_IO;
        case EXT4_ERR_RANGE: return VFS_ERR_INVAL;
        case EXT4_ERR_UNSUPPORTED: return VFS_ERR_UNSUPPORTED;
        default: return VFS_ERR_IO;
    }
}

static bool should_repair_after(ext4_status_t st) {
    return st == EXT4_ERR_CORRUPT;
}

static bool ext4_vfs_repair_once(ext4_vfs_ctx_t *ctx, const char *op, ext4_status_t cause) {
    (void)ctx;
    if (!should_repair_after(cause)) return false;
    KLOG(LOG_WARN, "ext4vfs", "metadata corruption after %s cause=%s; automatic repair is disabled", op ? op : "op", ext4_status_name(cause));
    return false;
}

static ext4_status_t lookup_path_retry(ext4_vfs_ctx_t *ctx, const char *op, const char *path, ext4_inode_disk_t *inode, u32 *ino) {
    ext4_status_t st = ext4_lookup_path(&ctx->mount, path, inode, ino);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, op, st)) st = ext4_lookup_path(&ctx->mount, path, inode, ino);
    return st;
}

static ext4_status_t lstat_path_retry(ext4_vfs_ctx_t *ctx, const char *op, const char *path, ext4_inode_disk_t *inode, u32 *ino) {
    ext4_status_t st = ext4_lstat_path(&ctx->mount, path, inode, ino);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, op, st)) st = ext4_lstat_path(&ctx->mount, path, inode, ino);
    return st;
}

static ext4_status_t read_inode_retry(ext4_vfs_ctx_t *ctx, const char *op, u32 ino, ext4_inode_disk_t *inode) {
    ext4_status_t st = ext4_read_inode(&ctx->mount, ino, inode);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, op, st)) st = ext4_read_inode(&ctx->mount, ino, inode);
    return st;
}

