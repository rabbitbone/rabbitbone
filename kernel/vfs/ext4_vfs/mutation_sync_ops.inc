static vfs_status_t op_list_inode(vfs_mount_t *mnt, u32 ino, vfs_dir_iter_fn fn, void *user) {
    if (ino == 0) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = read_inode_retry(ctx, "list_inode.lookup", ino, &inode);
    if (st != EXT4_OK) return map_status(st);
    st = list_inode_loaded_ext4(ctx, &inode, fn, user);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "list_inode", st)) {
        st = ext4_read_inode(&ctx->mount, ino, &inode);
        if (st == EXT4_OK) st = list_inode_loaded_ext4(ctx, &inode, fn, user);
    }
    return map_status(st);
}

static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = lookup_path_retry(ctx, "write.lookup", path, &inode, &ino);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_write_file(&ctx->mount, ino, &inode, offset, buffer, size, written_out);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "write", st)) {
        st = ext4_lookup_path(&ctx->mount, path, &inode, &ino);
        if (st == EXT4_OK) st = ext4_write_file(&ctx->mount, ino, &inode, offset, buffer, size, written_out);
    }
    return map_status(st);
}

static vfs_status_t op_write_inode(vfs_mount_t *mnt, u32 ino, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (ino == 0) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = read_inode_retry(ctx, "write_inode.lookup", ino, &inode);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_write_file(&ctx->mount, ino, &inode, offset, buffer, size, written_out);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "write_inode", st)) {
        st = ext4_read_inode(&ctx->mount, ino, &inode);
        if (st == EXT4_OK) st = ext4_write_file(&ctx->mount, ino, &inode, offset, buffer, size, written_out);
    }
    return map_status(st);
}

static vfs_status_t op_create(vfs_mount_t *mnt, const char *path, const void *data, usize size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_create_file(&ctx->mount, path, data, size);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "create", st)) st = ext4_create_file(&ctx->mount, path, data, size);
    return map_status(st);
}

static vfs_status_t op_mkdir(vfs_mount_t *mnt, const char *path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_mkdir(&ctx->mount, path);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "mkdir", st)) st = ext4_mkdir(&ctx->mount, path);
    return map_status(st);
}

static vfs_status_t op_unlink(vfs_mount_t *mnt, const char *path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_unlink(&ctx->mount, path);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "unlink", st)) st = ext4_unlink(&ctx->mount, path);
    return map_status(st);
}

static vfs_status_t op_readlink(vfs_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_readlink(&ctx->mount, path, buffer, size, read_out);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "readlink", st)) st = ext4_readlink(&ctx->mount, path, buffer, size, read_out);
    return map_status(st);
}

static vfs_status_t op_symlink(vfs_mount_t *mnt, const char *target, const char *link_path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_symlink(&ctx->mount, target, link_path);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "symlink", st)) st = ext4_symlink(&ctx->mount, target, link_path);
    return map_status(st);
}

static vfs_status_t op_link(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_link(&ctx->mount, old_path, new_path);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "link", st)) st = ext4_link(&ctx->mount, old_path, new_path);
    return map_status(st);
}

static vfs_status_t op_truncate(vfs_mount_t *mnt, const char *path, u64 size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_truncate_file_path(&ctx->mount, path, size);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "truncate", st)) st = ext4_truncate_file_path(&ctx->mount, path, size);
    return map_status(st);
}

static vfs_status_t op_truncate_inode(vfs_mount_t *mnt, u32 ino, u64 size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_truncate_file_inode(&ctx->mount, ino, size);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "truncate_inode", st)) st = ext4_truncate_file_inode(&ctx->mount, ino, size);
    return map_status(st);
}

static vfs_status_t op_rename(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_rename(&ctx->mount, old_path, new_path);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "rename", st)) st = ext4_rename(&ctx->mount, old_path, new_path);
    return map_status(st);
}

static vfs_status_t op_preallocate(vfs_mount_t *mnt, const char *path, u64 size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_preallocate_file_path(&ctx->mount, path, size);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "preallocate", st)) st = ext4_preallocate_file_path(&ctx->mount, path, size);
    return map_status(st);
}

static vfs_status_t op_preallocate_inode(vfs_mount_t *mnt, u32 ino, u64 size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_preallocate_file_inode(&ctx->mount, ino, size);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "preallocate_inode", st)) st = ext4_preallocate_file_inode(&ctx->mount, ino, size);
    return map_status(st);
}

static vfs_status_t op_sync(vfs_mount_t *mnt) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_sync_metadata(&ctx->mount);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "sync", st)) st = ext4_sync_metadata(&ctx->mount);
    return map_status(st);
}

