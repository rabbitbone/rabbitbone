#include <aurora/ext4_vfs.h>
#include <aurora/ext4.h>
#include <aurora/block.h>
#include <aurora/mbr.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>
#include <aurora/log.h>

typedef struct ext4_vfs_ctx {
    ext4_mount_t mount;
    char source[32];
} ext4_vfs_ctx_t;

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
    if (!ctx || !should_repair_after(cause)) return false;
    ext4_fsck_report_t report;
    memset(&report, 0, sizeof(report));
    ext4_status_t st = ext4_repair_metadata(&ctx->mount, &report);
    if (st != EXT4_OK) {
        KLOG(LOG_WARN, "ext4vfs", "repair after %s failed cause=%s repair=%s", op ? op : "op", ext4_status_name(cause), ext4_status_name(st));
        return false;
    }
    KLOG(LOG_WARN, "ext4vfs", "repair after %s cause=%s errors=%u htree=%u dirents=%u counters=%u checksums=%u",
         op ? op : "op", ext4_status_name(cause), report.errors, report.repaired_htree,
         report.repaired_dirents, report.repaired_counters, report.repaired_checksums);
    return true;
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
    out->inode = ino;
    out->fs_id = mnt->fs_id;
    out->nlink = le16(inode.i_links_count);
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
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
} list_bridge_ctx_t;

static bool list_bridge(const ext4_dirent_t *e, void *p) {
    if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) return true;
    list_bridge_ctx_t *b = (list_bridge_ctx_t *)p;
    vfs_dirent_t de;
    memset(&de, 0, sizeof(de));
    strncpy(de.name, e->name, sizeof(de.name) - 1u);
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
    list_bridge_ctx_t bridge = { fn, user };
    return ext4_list_dir(&ctx->mount, inode, list_bridge, &bridge);
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *user) {
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

static vfs_status_t op_sync_inode(vfs_mount_t *mnt, u32 ino, bool data_only) {
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
    out->flags = VFS_STATVFS_FLAG_PERSISTENT | VFS_STATVFS_FLAG_JOURNALED | VFS_STATVFS_FLAG_REPAIRABLE;
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
    if (st != EXT4_ERR_CORRUPT && report.errors == 0) {
        KLOG(LOG_WARN, "ext4vfs", "metadata validation on %s returned %s", ctx->source, ext4_status_name(st));
        return;
    }
    ext4_status_t rs = ext4_repair_metadata(&ctx->mount, &report);
    if (rs == EXT4_OK) {
        KLOG(LOG_WARN, "ext4vfs", "repaired metadata on %s errors=%u htree=%u dirents=%u counters=%u checksums=%u",
             ctx->source, report.errors, report.repaired_htree, report.repaired_dirents,
             report.repaired_counters, report.repaired_checksums);
    } else {
        KLOG(LOG_WARN, "ext4vfs", "metadata repair on %s failed: %s", ctx->source, ext4_status_name(rs));
    }
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
        ext4_status_t st = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &ctx->mount);
        if (st != EXT4_OK) {
            KLOG(LOG_WARN, "ext4vfs", "mount %s failed: %s", dev->name, ext4_status_name(st));
            kfree(ctx);
            return map_status(st);
        }
        strncpy(ctx->source, dev->name, sizeof(ctx->source) - 1u);
        ext4_vfs_repair_at_mount(ctx);
        vfs_status_t vs = vfs_mount(path, "ext4", &ops, ctx, true);
        if (vs != VFS_OK) kfree(ctx);
        return vs;
    }
    return VFS_ERR_NOENT;
}
