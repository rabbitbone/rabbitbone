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

static vfs_mount_t *route_mount(vfs_route_t *route) {
    if (!route || !route->found || route->mount_index >= VFS_MAX_MOUNTS) return 0;
    if (!mounts[route->mount_index].ops) return 0;
    return &mounts[route->mount_index];
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
            mounts[i].fs_id = next_fs_id++;
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
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt || !mnt->ops->stat) return VFS_ERR_NOENT;
    return mnt->ops->stat(mnt, route.relative, out);
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
