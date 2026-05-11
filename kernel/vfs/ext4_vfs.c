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

static bool ext4_type_checked(const ext4_inode_disk_t *inode, vfs_node_type_t *type_out) {
    if (!inode || !type_out) return false;
    if (ext4_inode_is_dir(inode)) { *type_out = VFS_NODE_DIR; return true; }
    if (ext4_inode_is_regular(inode)) { *type_out = VFS_NODE_FILE; return true; }
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

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(&ctx->mount, path, &inode, &ino);
    if (st != EXT4_OK) return map_status(st);
    vfs_node_type_t type;
    if (!ext4_type_checked(&inode, &type)) return VFS_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    out->type = type;
    out->size = ext4_inode_size(&inode);
    out->mode = le16(inode.i_mode);
    out->inode = ino;
    out->fs_id = mnt->fs_id;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_lookup_path(&ctx->mount, path, &inode, 0);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_read_file(&ctx->mount, &inode, offset, buffer, size, read_out);
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
    de.type = e->file_type == 2u ? VFS_NODE_DIR : VFS_NODE_FILE;
    de.inode = e->inode;
    return b->fn(&de, b->ctx);
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *user) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_lookup_path(&ctx->mount, path, &inode, 0);
    if (st != EXT4_OK) return map_status(st);
    if (!ext4_inode_is_dir(&inode)) {
        if (ext4_inode_is_regular(&inode)) return VFS_ERR_NOTDIR;
        return VFS_ERR_UNSUPPORTED;
    }
    list_bridge_ctx_t bridge = { fn, user };
    st = ext4_list_dir(&ctx->mount, &inode, list_bridge, &bridge);
    return map_status(st);
}


static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(&ctx->mount, path, &inode, &ino);
    if (st != EXT4_OK) return map_status(st);
    if (ext4_inode_is_dir(&inode)) return VFS_ERR_ISDIR;
    if (!ext4_inode_is_regular(&inode)) return VFS_ERR_UNSUPPORTED;
    st = ext4_write_file(&ctx->mount, ino, &inode, offset, buffer, size, written_out);
    return map_status(st);
}

static vfs_status_t op_create(vfs_mount_t *mnt, const char *path, const void *data, usize size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    return map_status(ext4_create_file(&ctx->mount, path, data, size));
}

static vfs_status_t op_mkdir(vfs_mount_t *mnt, const char *path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    return map_status(ext4_mkdir(&ctx->mount, path));
}

static vfs_status_t op_unlink(vfs_mount_t *mnt, const char *path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    return map_status(ext4_unlink(&ctx->mount, path));
}


static vfs_status_t op_truncate(vfs_mount_t *mnt, const char *path, u64 size) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    return map_status(ext4_truncate_file_path(&ctx->mount, path, size));
}

static vfs_status_t op_rename(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ext4_vfs_ctx_t *ctx = (ext4_vfs_ctx_t *)mnt->ctx;
    return map_status(ext4_rename(&ctx->mount, old_path, new_path));
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .read = op_read,
    .write = op_write,
    .list = op_list,
    .mkdir = op_mkdir,
    .create = op_create,
    .unlink = op_unlink,
    .truncate = op_truncate,
    .rename = op_rename,
};

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
        vfs_status_t vs = vfs_mount(path, "ext4", &ops, ctx, true);
        if (vs != VFS_OK) kfree(ctx);
        return vs;
    }
    return VFS_ERR_NOENT;
}
