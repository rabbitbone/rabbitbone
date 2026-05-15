static vfs_status_t op_sync_inode(vfs_mount_t *mnt, u32 ino, bool data_only) {
    if (ino == 0) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_status_t st = ext4_sync_file(&ctx->mount, ino, data_only);
    if (st != EXT4_OK && ext4_vfs_repair_once(ctx, "sync_inode", st)) st = ext4_sync_file(&ctx->mount, ino, data_only);
    return map_status(st);
}

static vfs_status_t op_statvfs(vfs_mount_t *mnt, vfs_statvfs_t *out) {
    if (!mnt || !out) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    memset(out, 0, sizeof(*out));
    out->block_size = ctx->mount.block_size;
    out->total_blocks = ctx->mount.blocks_count;
    out->free_blocks = ((u64)le32(ctx->mount.sb.s_free_blocks_count_hi) << 32) | le32(ctx->mount.sb.s_free_blocks_count_lo);
    out->avail_blocks = out->free_blocks;
    out->total_inodes = ctx->mount.inodes_count;
    out->free_inodes = le32(ctx->mount.sb.s_free_inodes_count);
    out->flags = VFS_STATVFS_FLAG_PERSISTENT | VFS_STATVFS_FLAG_JOURNALED;
    out->max_name_len = EXT4_NAME_LEN;
    return VFS_OK;
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .lstat = op_lstat,
    .readlink = op_readlink,
    .symlink = op_symlink,
    .link = op_link,
    .read = op_read,
    .write = op_write,
    .stat_inode = op_stat_inode,
    .read_inode = op_read_inode,
    .write_inode = op_write_inode,
    .truncate_inode = op_truncate_inode,
    .list = op_list,
    .list_inode = op_list_inode,
    .mkdir = op_mkdir,
    .create = op_create,
    .unlink = op_unlink,
    .truncate = op_truncate,
    .rename = op_rename,
    .preallocate = op_preallocate,
    .preallocate_inode = op_preallocate_inode,
    .sync = op_sync,
    .sync_inode = op_sync_inode,
    .statvfs = op_statvfs,
};

static void ext4_vfs_repair_at_mount(ext4_vfs_ctx_t *ctx) {
    if (!ctx) return;
    ext4_fsck_report_t report;
    memset(&report, 0, sizeof(report));
    ext4_status_t st = ext4_validate_metadata(&ctx->mount, &report);
    if (st == EXT4_OK && report.errors == 0) return;
    KLOG(LOG_WARN, "ext4vfs", "metadata validation on %s reported %s errors=%u; mounted without automatic repair",
         ctx->source, ext4_status_name(st), report.errors);
}

vfs_status_t ext4_vfs_mount_first_linux_partition(const char *path) {
    for (usize i = 0; i < block_count(); ++i) {
        block_device_t *dev = block_get(i);
        mbr_table_t mbr;
        if (!mbr_read(dev, &mbr)) continue;
        const mbr_partition_t *part = mbr_find_linux_on_device(dev, &mbr);
        if (!part) continue;
        ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)kcalloc(1, sizeof(*ctx));
        if (!ctx) return VFS_ERR_NOMEM;
        KLOG(LOG_INFO, "ext4vfs", "probing %s driver=%s partition_lba=%u sectors=%u", dev->name, block_driver_name(dev), part->lba_first, part->sector_count);
        ext4_status_t st = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &ctx->mount);
        if (st != EXT4_OK) {
            KLOG(LOG_WARN, "ext4vfs", "mount %s partition_lba=%u failed: %s", dev->name, part->lba_first, ext4_status_name(st));
            kfree(ctx);
            continue;
        }
        strlcpy(ctx->source, dev->name, sizeof(ctx->source));
        ext4_vfs_repair_at_mount(ctx);
        vfs_status_t vs = vfs_mount(path, "ext4", &ops, ctx, true);
        if (vs != VFS_OK) {
            KLOG(LOG_WARN, "ext4vfs", "vfs mount %s at %s failed: %s", dev->name, path, vfs_status_name(vs));
            kfree(ctx);
            return vs;
        }
        KLOG(LOG_INFO, "ext4vfs", "mounted %s driver=%s partition_lba=%u sectors=%u at %s", dev->name, block_driver_name(dev), part->lba_first, part->sector_count, path);
        return VFS_OK;
    }
    return VFS_ERR_NOENT;
}
