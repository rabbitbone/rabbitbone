vfs_status_t vfs_sync_all(void) {
    typedef struct sync_target { usize idx; u32 gen; } sync_target_t;
    sync_target_t targets[VFS_MAX_MOUNTS];
    usize count = 0;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || mounts[i].unmounting || !mounts[i].writable || !mounts[i].ops->sync) continue;
        targets[count++] = (sync_target_t){ i, mounts[i].generation };
    }
    spin_unlock_irqrestore(&mounts_lock, flags);

    vfs_status_t first = VFS_OK;
    for (usize i = 0; i < count; ++i) {
        vfs_mount_t *mnt = mount_acquire_index(targets[i].idx, targets[i].gen);
        if (!mnt) continue;
        vfs_status_t st = mnt->ops->sync ? mnt->ops->sync(mnt) : VFS_OK;
        mount_release(mnt);
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
    if (!ref || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return vfs_sync_all();
    if (ref->fs_id == 0 || ref->mount_generation == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    vfs_status_t st = VFS_OK;
    if (mnt->writable) {
        if (mnt->ops->sync_inode) st = mnt->ops->sync_inode(mnt, ref->inode, data_only);
        else if (!data_only && mnt->ops->sync) st = mnt->ops->sync(mnt);
    }
    mount_release(mnt);
    return st;
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
        rs = mnt->ops->statvfs(mnt, out);
        if (rs != VFS_OK) { mount_release(mnt); return rs; }
    } else {
        fill_default_statvfs(mnt, out);
    }
    out->fs_id = mnt->fs_id;
    out->flags = (out->flags & ~VFS_STATVFS_FLAG_READONLY) | (mnt->writable ? 0u : VFS_STATVFS_FLAG_READONLY);
    strncpy(out->mount_path, mnt->path, sizeof(out->mount_path) - 1u);
    out->mount_path[sizeof(out->mount_path) - 1u] = 0;
    strncpy(out->fs_name, mnt->name, sizeof(out->fs_name) - 1u);
    out->fs_name[sizeof(out->fs_name) - 1u] = 0;
    mount_release(mnt);
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
    if (staged_route.mount_index != final_route.mount_index || staged_route.mount_generation != final_route.mount_generation) return VFS_ERR_UNSUPPORTED;
    vfs_mount_t *mnt = route_mount(&staged_route);
    if (!mnt) return VFS_ERR_NOENT;
    bool writable = mnt->writable;
    bool has_rename = mnt->ops->rename != 0;
    mount_release(mnt);
    if (!writable) return VFS_ERR_PERM;
    if (!has_rename) return VFS_ERR_UNSUPPORTED;

    rs = vfs_sync_path(staged_path);
    if (rs != VFS_OK) return rs;
    rs = vfs_rename(staged_path, final_path);
    if (rs != VFS_OK) return rs;
    rs = vfs_sync_parent_dir(final_path);
    if (rs != VFS_OK) {
        KLOG(LOG_WARN, "vfs", "install commit applied but parent sync failed for %s: %s", final_path, vfs_status_name(rs));
        return rs;
    }
    return VFS_OK;
}

void vfs_dump_mounts(void) {
    kprintf("mounts:\n");
    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || mounts[i].unmounting) continue;
        kprintf("  fs%u gen%u %-10s at %-16s %s refs=%u\n", mounts[i].fs_id, mounts[i].generation,
                mounts[i].name, mounts[i].path, mounts[i].writable ? "rw" : "ro", mounts[i].active_refs);
    }
    spin_unlock_irqrestore(&mounts_lock, flags);
}

bool vfs_rust_route_selftest(void) {
    return rabbitbone_rust_vfs_route_selftest();
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
