#include <aurora/vfs.h>
#include <aurora/path.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/rust.h>

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static u32 next_fs_id;
static bool ready;

static bool fs_id_in_use(u32 id) {
    if (id == 0) return true;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) if (mounts[i].ops && mounts[i].fs_id == id) return true;
    return false;
}

static u32 alloc_fs_id(void) {
    for (u32 tries = 0; tries < 0xffffffffu; ++tries) {
        u32 id = next_fs_id++;
        if (next_fs_id == 0) next_fs_id = 1;
        if (!fs_id_in_use(id)) return id;
    }
    return 0;
}

typedef struct vfs_route {
    bool found;
    usize mount_index;
    char normalized[VFS_PATH_MAX];
    char relative[VFS_PATH_MAX];
} vfs_route_t;

static vfs_status_t resolve_route(const char *path, vfs_route_t *route) {
    if (!path || !route) return VFS_ERR_INVAL;
    memset(route, 0, sizeof(*route));

    if (!aurora_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    usize len = strnlen(path, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX) return VFS_ERR_INVAL;

    aurora_rust_mount_view_t view[VFS_MAX_MOUNTS];
    memset(view, 0, sizeof(view));
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops) continue;
        view[i].active = 1;
        strncpy(view[i].path, mounts[i].path, sizeof(view[i].path) - 1u);
    }

    aurora_rust_route_out_t out;
    memset(&out, 0, sizeof(out));
    i32 rc = aurora_rust_vfs_route(view, (const u8 *)path, len + 1u, &out);
    if (rc == VFS_ERR_NOENT) return VFS_ERR_NOENT;
    if (rc != VFS_OK) return VFS_ERR_INVAL;
    if (!out.found || out.mount_index >= VFS_MAX_MOUNTS || !mounts[out.mount_index].ops) return VFS_ERR_NOENT;

    route->found = true;
    route->mount_index = out.mount_index;
    memcpy(route->normalized, out.normalized, sizeof(route->normalized));
    memcpy(route->relative, out.relative, sizeof(route->relative));
    route->normalized[sizeof(route->normalized) - 1u] = 0;
    route->relative[sizeof(route->relative) - 1u] = 0;
    return VFS_OK;
}


static void fill_default_statvfs(const vfs_mount_t *mnt, vfs_statvfs_t *out) {
    if (!mnt || !out) return;
    memset(out, 0, sizeof(*out));
    out->block_size = 4096u;
    out->fs_id = mnt->fs_id;
    out->flags = mnt->writable ? 0u : VFS_STATVFS_FLAG_READONLY;
    out->max_name_len = VFS_NAME_MAX - 1u;
    strncpy(out->mount_path, mnt->path, sizeof(out->mount_path) - 1u);
    strncpy(out->fs_name, mnt->name, sizeof(out->fs_name) - 1u);
}

static vfs_mount_t *route_mount(vfs_route_t *route) {
    if (!route || !route->found || route->mount_index >= VFS_MAX_MOUNTS) return 0;
    if (!mounts[route->mount_index].ops) return 0;
    return &mounts[route->mount_index];
}

static vfs_mount_t *mount_by_fs_id(u32 fs_id) {
    if (fs_id == 0) return 0;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && mounts[i].fs_id == fs_id) return &mounts[i];
    }
    return 0;
}

static void fill_ref_from_stat(vfs_node_ref_t *out, const char *path, const vfs_stat_t *st) {
    if (!out || !st) return;
    memset(out, 0, sizeof(*out));
    out->fs_id = st->fs_id;
    out->inode = st->inode;
    out->type = st->type;
    out->size = st->size;
    if (path) strncpy(out->path, path, sizeof(out->path) - 1u);
}

static vfs_status_t root_list(vfs_dir_iter_fn fn, void *ctx) {
    if (!fn) return VFS_ERR_INVAL;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && strcmp(mounts[i].path, "/") == 0 && mounts[i].ops->list) {
            vfs_status_t st = mounts[i].ops->list(&mounts[i], "/", fn, ctx);
            if (st != VFS_OK) return st;
            break;
        }
    }
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || strcmp(mounts[i].path, "/") == 0) continue;
        const char *name = path_basename(mounts[i].path);
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, name, sizeof(de.name) - 1u);
        de.type = VFS_NODE_DIR;
        de.inode = mounts[i].fs_id;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

void vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    next_fs_id = 1;
    ready = true;
    KLOG(LOG_INFO, "vfs", "initialized mount table entries=%u", VFS_MAX_MOUNTS);
}

vfs_status_t vfs_mount(const char *path, const char *name, const vfs_ops_t *ops, void *ctx, bool writable) {
    if (!ready) vfs_init();
    if (!path || !name || !ops) return VFS_ERR_INVAL;
    if (strnlen(name, VFS_NAME_MAX) >= VFS_NAME_MAX) return VFS_ERR_INVAL;
    if (!aurora_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && strcmp(mounts[i].path, norm) == 0) return VFS_ERR_EXIST;
    }
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops) {
            memset(&mounts[i], 0, sizeof(mounts[i]));
            strncpy(mounts[i].path, norm, sizeof(mounts[i].path) - 1u);
            strncpy(mounts[i].name, name, sizeof(mounts[i].name) - 1u);
            mounts[i].ops = ops;
            mounts[i].ctx = ctx;
            mounts[i].fs_id = alloc_fs_id();
            if (mounts[i].fs_id == 0) { memset(&mounts[i], 0, sizeof(mounts[i])); return VFS_ERR_NOSPC; }
            mounts[i].writable = writable;
            KLOG(LOG_INFO, "vfs", "mounted %s at %s writable=%u", mounts[i].name, mounts[i].path, writable ? 1u : 0u);
            return VFS_OK;
        }
    }
    return VFS_ERR_NOSPC;
}

vfs_status_t vfs_unmount(const char *path) {
    if (!aurora_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) return VFS_ERR_PERM;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && strcmp(mounts[i].path, norm) == 0) {
            memset(&mounts[i], 0, sizeof(mounts[i]));
            return VFS_OK;
        }
    }
    return VFS_ERR_NOENT;
}

vfs_status_t vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->inode = 1u;
        out->nlink = 1u;
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt || !mnt->ops->stat) return VFS_ERR_NOENT;
    return mnt->ops->stat(mnt, route.relative, out);
}

vfs_status_t vfs_lstat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->inode = 1u;
        out->nlink = 1u;
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->lstat) return mnt->ops->lstat(mnt, route.relative, out);
    if (mnt->ops->stat) return mnt->ops->stat(mnt, route.relative, out);
    return VFS_ERR_NOENT;
}

vfs_status_t vfs_readlink(const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!path || (!buffer && size)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt || !mnt->ops->readlink) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->readlink(mnt, route.relative, buffer, size, read_out);
}

vfs_status_t vfs_symlink(const char *target, const char *link_path) {
    if (!target || !link_path || target[0] == 0) return VFS_ERR_INVAL;
    if (strnlen(target, VFS_PATH_MAX) >= VFS_PATH_MAX) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(link_path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->symlink) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->symlink(mnt, target, route.relative);
}

vfs_status_t vfs_link(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return VFS_ERR_INVAL;
    vfs_route_t old_route;
    vfs_status_t rs = resolve_route(old_path, &old_route);
    if (rs != VFS_OK) return rs;
    vfs_route_t new_route;
    rs = resolve_route(new_path, &new_route);
    if (rs != VFS_OK) return rs;
    if (old_route.mount_index != new_route.mount_index) return VFS_ERR_UNSUPPORTED;
    vfs_mount_t *mnt = route_mount(&old_route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->link) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->link(mnt, old_route.relative, new_route.relative);
}

vfs_status_t vfs_get_ref(const char *path, vfs_node_ref_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->inode = 1u;
        out->type = VFS_NODE_DIR;
        strncpy(out->path, "/", sizeof(out->path) - 1u);
        return VFS_OK;
    }
    vfs_stat_t st;
    vfs_status_t rs = vfs_stat(path, &st);
    if (rs != VFS_OK) return rs;
    fill_ref_from_stat(out, path, &st);
    return VFS_OK;
}

vfs_status_t vfs_stat_ref(const vfs_node_ref_t *ref, vfs_stat_t *out) {
    if (!ref || !out || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->inode = ref->inode;
        out->nlink = 1u;
        return VFS_OK;
    }
    if (ref->fs_id == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->stat_inode) return mnt->ops->stat_inode(mnt, ref->inode, out);
    if (ref->path[0]) return vfs_stat(ref->path, out);
    return VFS_ERR_UNSUPPORTED;
}

vfs_status_t vfs_read(const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt || !mnt->ops->read) return VFS_ERR_NOENT;
    return mnt->ops->read(mnt, route.relative, offset, buffer, size, read_out);
}

vfs_status_t vfs_read_ref(const vfs_node_ref_t *ref, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!ref || ref->fs_id == 0 || ref->inode == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->read_inode) return mnt->ops->read_inode(mnt, ref->inode, offset, buffer, size, read_out);
    if (ref->path[0]) return vfs_read(ref->path, offset, buffer, size, read_out);
    return VFS_ERR_UNSUPPORTED;
}

vfs_status_t vfs_write(const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->write) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->write(mnt, route.relative, offset, buffer, size, written_out);
}

vfs_status_t vfs_write_ref(const vfs_node_ref_t *ref, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!ref || ref->fs_id == 0 || ref->inode == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (mnt->ops->write_inode) return mnt->ops->write_inode(mnt, ref->inode, offset, buffer, size, written_out);
    if (ref->path[0]) return vfs_write(ref->path, offset, buffer, size, written_out);
    return VFS_ERR_UNSUPPORTED;
}

vfs_status_t vfs_list(const char *path, vfs_dir_iter_fn fn, void *ctx) {
    if (!path || !fn) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) return root_list(fn, ctx);
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt || !mnt->ops->list) return VFS_ERR_NOENT;
    return mnt->ops->list(mnt, route.relative, fn, ctx);
}

vfs_status_t vfs_list_ref(const vfs_node_ref_t *ref, vfs_dir_iter_fn fn, void *ctx) {
    if (!ref || !fn || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return root_list(fn, ctx);
    if (ref->fs_id == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->list_inode) return mnt->ops->list_inode(mnt, ref->inode, fn, ctx);
    if (ref->path[0]) return vfs_list(ref->path, fn, ctx);
    return VFS_ERR_UNSUPPORTED;
}

vfs_status_t vfs_mkdir(const char *path) {
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->mkdir) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->mkdir(mnt, route.relative);
}

vfs_status_t vfs_create(const char *path, const void *data, usize size) {
    if (size && !data) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->create) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->create(mnt, route.relative, data, size);
}


vfs_status_t vfs_unlink(const char *path) {
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->unlink) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->unlink(mnt, route.relative);
}


vfs_status_t vfs_truncate(const char *path, u64 size) {
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->truncate) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->truncate(mnt, route.relative, size);
}

vfs_status_t vfs_truncate_ref(const vfs_node_ref_t *ref, u64 size) {
    if (!ref || ref->fs_id == 0 || ref->inode == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (mnt->ops->truncate_inode) return mnt->ops->truncate_inode(mnt, ref->inode, size);
    if (ref->path[0]) return vfs_truncate(ref->path, size);
    return VFS_ERR_UNSUPPORTED;
}

vfs_status_t vfs_rename(const char *old_path, const char *new_path) {
    vfs_route_t old_route;
    vfs_status_t rs = resolve_route(old_path, &old_route);
    if (rs != VFS_OK) return rs;
    vfs_route_t new_route;
    rs = resolve_route(new_path, &new_route);
    if (rs != VFS_OK) return rs;
    if (old_route.mount_index != new_route.mount_index) return VFS_ERR_UNSUPPORTED;
    vfs_mount_t *mnt = route_mount(&old_route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->rename) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->rename(mnt, old_route.relative, new_route.relative);
}


vfs_status_t vfs_preallocate(const char *path, u64 size) {
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (!mnt->ops->preallocate) return VFS_ERR_UNSUPPORTED;
    return mnt->ops->preallocate(mnt, route.relative, size);
}

vfs_status_t vfs_preallocate_ref(const vfs_node_ref_t *ref, u64 size) {
    if (!ref || ref->fs_id == 0 || ref->inode == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    if (mnt->ops->preallocate_inode) return mnt->ops->preallocate_inode(mnt, ref->inode, size);
    if (ref->path[0]) return vfs_preallocate(ref->path, size);
    return VFS_ERR_UNSUPPORTED;
}


vfs_status_t vfs_sync_all(void) {
    vfs_status_t first = VFS_OK;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || !mounts[i].writable || !mounts[i].ops->sync) continue;
        vfs_status_t st = mounts[i].ops->sync(&mounts[i]);
        if (st != VFS_OK && first == VFS_OK) first = st;
    }
    return first;
}

vfs_status_t vfs_sync_path(const char *path) {
    vfs_node_ref_t ref;
    vfs_status_t rs = vfs_get_ref(path, &ref);
    if (rs != VFS_OK) return rs;
    return vfs_sync_ref(&ref, false);
}

vfs_status_t vfs_sync_ref(const vfs_node_ref_t *ref, bool data_only) {
    (void)data_only;
    if (!ref || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return vfs_sync_all();
    if (ref->fs_id == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_OK;
    if (mnt->ops->sync_inode) return mnt->ops->sync_inode(mnt, ref->inode, data_only);
    if (data_only) return VFS_OK;
    if (mnt->ops->sync) return mnt->ops->sync(mnt);
    return VFS_OK;
}

vfs_status_t vfs_sync_parent_dir(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) return VFS_OK;
    const char *base = path_basename(norm);
    usize parent_len = (usize)(base - norm);
    if (parent_len == 0) parent_len = 1u;
    if (parent_len >= sizeof(norm)) return VFS_ERR_INVAL;
    if (parent_len > 1u && norm[parent_len - 1u] == '/') --parent_len;
    norm[parent_len] = 0;
    if (norm[0] == 0) strcpy(norm, "/");
    return vfs_sync_path(norm);
}

vfs_status_t vfs_statvfs(const char *path, vfs_statvfs_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->statvfs) {
        vfs_status_t st = mnt->ops->statvfs(mnt, out);
        if (st != VFS_OK) return st;
    } else {
        fill_default_statvfs(mnt, out);
    }
    out->fs_id = mnt->fs_id;
    out->flags = (out->flags & ~VFS_STATVFS_FLAG_READONLY) | (mnt->writable ? 0u : VFS_STATVFS_FLAG_READONLY);
    strncpy(out->mount_path, mnt->path, sizeof(out->mount_path) - 1u);
    strncpy(out->fs_name, mnt->name, sizeof(out->fs_name) - 1u);
    return VFS_OK;
}



vfs_status_t vfs_install_commit(const char *staged_path, const char *final_path) {
    if (!staged_path || !final_path) return VFS_ERR_INVAL;
    vfs_route_t staged_route;
    vfs_status_t rs = resolve_route(staged_path, &staged_route);
    if (rs != VFS_OK) return rs;
    vfs_route_t final_route;
    rs = resolve_route(final_path, &final_route);
    if (rs != VFS_OK) return rs;
    if (staged_route.mount_index != final_route.mount_index) return VFS_ERR_UNSUPPORTED;
    vfs_mount_t *mnt = route_mount(&staged_route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) return VFS_ERR_PERM;
    rs = vfs_sync_path(staged_path);
    if (rs != VFS_OK) return rs;
    rs = vfs_rename(staged_path, final_path);
    if (rs != VFS_OK) return rs;
    rs = vfs_sync_parent_dir(final_path);
    if (rs != VFS_OK) return rs;
    return VFS_OK;
}

void vfs_dump_mounts(void) {
    kprintf("mounts:\n");
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops) continue;
        kprintf("  fs%u %-10s at %-16s %s\n", mounts[i].fs_id, mounts[i].name, mounts[i].path,
                mounts[i].writable ? "rw" : "ro");
    }
}

bool vfs_rust_route_selftest(void) {
    return aurora_rust_vfs_route_selftest();
}

const char *vfs_status_name(vfs_status_t st) {
    switch (st) {
        case VFS_OK: return "ok";
        case VFS_ERR_NOENT: return "no-entry";
        case VFS_ERR_NOMEM: return "no-memory";
        case VFS_ERR_INVAL: return "invalid";
        case VFS_ERR_IO: return "io";
        case VFS_ERR_NOTDIR: return "not-dir";
        case VFS_ERR_ISDIR: return "is-dir";
        case VFS_ERR_EXIST: return "exists";
        case VFS_ERR_PERM: return "permission";
        case VFS_ERR_NOSPC: return "no-space";
        case VFS_ERR_NOTEMPTY: return "not-empty";
        case VFS_ERR_UNSUPPORTED: return "unsupported";
        default: return "unknown";
    }
}
