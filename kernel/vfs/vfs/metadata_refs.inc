void vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    memset(ref_pins, 0, sizeof(ref_pins));
    spinlock_init(&mounts_lock);
    spinlock_init(&ref_pins_lock);
    next_fs_id = 1;
    next_mount_generation = 1;
    ready = true;
    KLOG(LOG_INFO, "vfs", "initialized mount table entries=%u", VFS_MAX_MOUNTS);
}

vfs_status_t vfs_mount(const char *path, const char *name, const vfs_ops_t *ops, void *ctx, bool writable) {
    if (!ready) vfs_init();
    if (!path || !name || !ops || !ops->stat) return VFS_ERR_INVAL;
    usize name_len = strnlen(name, VFS_NAME_MAX);
    if (name_len == 0 || name_len >= VFS_NAME_MAX) return VFS_ERR_INVAL;
    if (!rabbitbone_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;

    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && !mounts[i].unmounting && strcmp(mounts[i].path, norm) == 0) {
            spin_unlock_irqrestore(&mounts_lock, flags);
            return VFS_ERR_EXIST;
        }
    }
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops && mounts[i].active_refs == 0) {
            memset(&mounts[i], 0, sizeof(mounts[i]));
            strncpy(mounts[i].path, norm, sizeof(mounts[i].path) - 1u);
            strncpy(mounts[i].name, name, sizeof(mounts[i].name) - 1u);
            mounts[i].ops = ops;
            mounts[i].ctx = ctx;
            mounts[i].fs_id = alloc_fs_id();
            mounts[i].generation = alloc_mount_generation();
            mounts[i].writable = writable;
            if (mounts[i].fs_id == 0 || mounts[i].generation == 0) {
                memset(&mounts[i], 0, sizeof(mounts[i]));
                spin_unlock_irqrestore(&mounts_lock, flags);
                return VFS_ERR_NOSPC;
            }
            KLOG(LOG_INFO, "vfs", "mounted %s at %s writable=%u", mounts[i].name, mounts[i].path, writable ? 1u : 0u);
            spin_unlock_irqrestore(&mounts_lock, flags);
            return VFS_OK;
        }
    }
    spin_unlock_irqrestore(&mounts_lock, flags);
    return VFS_ERR_NOSPC;
}

vfs_status_t vfs_unmount(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    if (!rabbitbone_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) return VFS_ERR_PERM;

    usize idx = VFS_MAX_MOUNTS;
    u32 generation = 0;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && !mounts[i].unmounting && strcmp(mounts[i].path, norm) == 0) {
            idx = i;
            generation = mounts[i].generation;
            break;
        }
    }
    if (idx >= VFS_MAX_MOUNTS) {
        spin_unlock_irqrestore(&mounts_lock, flags);
        return VFS_ERR_NOENT;
    }
    usize norm_len = strlen(norm);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (i == idx || !mounts[i].ops || mounts[i].unmounting) continue;
        if (strncmp(mounts[i].path, norm, norm_len) == 0 && mounts[i].path[norm_len] == '/') {
            spin_unlock_irqrestore(&mounts_lock, flags);
            return VFS_ERR_NOTEMPTY;
        }
    }
    if (mounts[idx].active_refs != 0) {
        spin_unlock_irqrestore(&mounts_lock, flags);
        return VFS_ERR_BUSY;
    }
    mounts[idx].unmounting = true;
    if (mounts[idx].generation == generation) memset(&mounts[idx], 0, sizeof(mounts[idx]));
    spin_unlock_irqrestore(&mounts_lock, flags);
    return VFS_OK;
}

vfs_status_t vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->mode = 0755u;
        out->inode = 1u;
        out->nlink = 1u;
        out->uid = 0u;
        out->gid = 0u;
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->ops->stat) { mount_release(mnt); return VFS_ERR_NOENT; }
    rs = mnt->ops->stat(mnt, route.relative, out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_lstat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->mode = 0755u;
        out->inode = 1u;
        out->nlink = 1u;
        out->uid = 0u;
        out->gid = 0u;
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (mnt->ops->lstat) rs = mnt->ops->lstat(mnt, route.relative, out);
    else if (mnt->ops->stat) rs = mnt->ops->stat(mnt, route.relative, out);
    else rs = VFS_ERR_NOENT;
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_readlink(const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!path || (!buffer && size)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->ops->readlink) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->readlink(mnt, route.relative, buffer, size, read_out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_symlink(const char *target, const char *link_path) {
    if (!target || !link_path || target[0] == 0) return VFS_ERR_INVAL;
    if (strnlen(target, VFS_PATH_MAX) >= VFS_PATH_MAX) return VFS_ERR_INVAL;
    if (!rabbitbone_rust_path_no_traversal_check((const u8 *)target, VFS_PATH_MAX, true)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(link_path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->symlink) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->symlink(mnt, target, route.relative);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_link(const char *old_path, const char *new_path) {
    if (!old_path || !new_path) return VFS_ERR_INVAL;
    vfs_route_t old_route;
    vfs_status_t rs = resolve_route(old_path, &old_route);
    if (rs != VFS_OK) return rs;
    vfs_route_t new_route;
    rs = resolve_route(new_path, &new_route);
    if (rs != VFS_OK) return rs;
    if (old_route.mount_index != new_route.mount_index || old_route.mount_generation != new_route.mount_generation) return VFS_ERR_UNSUPPORTED;
    vfs_mount_t *mnt = route_mount(&old_route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->link) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->link(mnt, old_route.relative, new_route.relative);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_get_ref(const char *path, vfs_node_ref_t *out) {
    if (!path || !out) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->inode = 1u;
        out->type = VFS_NODE_DIR;
        strncpy(out->path, "/", sizeof(out->path) - 1u);
        return VFS_OK;
    }
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->ops->stat) { mount_release(mnt); return VFS_ERR_NOENT; }
    vfs_stat_t st;
    rs = mnt->ops->stat(mnt, route.relative, &st);
    mount_release(mnt);
    if (rs != VFS_OK) return rs;
    fill_ref_from_stat(out, route.normalized, &st, &route);
    return VFS_OK;
}

bool vfs_retain_ref(const vfs_node_ref_t *ref) {
    if (!ref || ref->inode == 0) return false;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return true;
    if (ref->fs_id == 0 || ref->mount_generation == 0) return false;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return false;
    u64 flags = spin_lock_irqsave(&ref_pins_lock);
    usize free_idx = VFS_MAX_PINNED_REFS;
    for (usize i = 0; i < VFS_MAX_PINNED_REFS; ++i) {
        if (ref_pins[i].used && ref_pins[i].fs_id == ref->fs_id &&
            ref_pins[i].mount_generation == ref->mount_generation && ref_pins[i].inode == ref->inode) {
            if (ref_pins[i].refs == 0xffffffffu) {
                spin_unlock_irqrestore(&ref_pins_lock, flags);
                mount_release(mnt);
                return false;
            }
            ++ref_pins[i].refs;
            spin_unlock_irqrestore(&ref_pins_lock, flags);
            return true;
        }
        if (!ref_pins[i].used && free_idx == VFS_MAX_PINNED_REFS) free_idx = i;
    }
    if (free_idx == VFS_MAX_PINNED_REFS) {
        spin_unlock_irqrestore(&ref_pins_lock, flags);
        mount_release(mnt);
        return false;
    }
    memset(&ref_pins[free_idx], 0, sizeof(ref_pins[free_idx]));
    ref_pins[free_idx].used = true;
    ref_pins[free_idx].fs_id = ref->fs_id;
    ref_pins[free_idx].mount_generation = ref->mount_generation;
    ref_pins[free_idx].inode = ref->inode;
    ref_pins[free_idx].refs = 1u;
    spin_unlock_irqrestore(&ref_pins_lock, flags);
    return true;
}

void vfs_release_ref(const vfs_node_ref_t *ref) {
    if (!ref || ref->inode == 0) return;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return;
    if (ref->fs_id == 0 || ref->mount_generation == 0) return;
    bool release_mount = false;
    u64 flags = spin_lock_irqsave(&ref_pins_lock);
    for (usize i = 0; i < VFS_MAX_PINNED_REFS; ++i) {
        if (ref_pins[i].used && ref_pins[i].fs_id == ref->fs_id &&
            ref_pins[i].mount_generation == ref->mount_generation && ref_pins[i].inode == ref->inode) {
            if (ref_pins[i].refs > 0) --ref_pins[i].refs;
            release_mount = true;
            if (ref_pins[i].refs == 0) memset(&ref_pins[i], 0, sizeof(ref_pins[i]));
            break;
        }
    }
    spin_unlock_irqrestore(&ref_pins_lock, flags);
    if (release_mount) (void)mount_release_by_fs_id(ref->fs_id, ref->mount_generation);
}

bool vfs_ref_is_busy(const vfs_node_ref_t *ref) {
    if (!ref || ref->inode == 0) return false;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return false;
    if (ref->fs_id == 0 || ref->mount_generation == 0) return false;
    bool busy = false;
    u64 flags = spin_lock_irqsave(&ref_pins_lock);
    for (usize i = 0; i < VFS_MAX_PINNED_REFS; ++i) {
        if (ref_pins[i].used && ref_pins[i].fs_id == ref->fs_id &&
            ref_pins[i].mount_generation == ref->mount_generation && ref_pins[i].inode == ref->inode &&
            ref_pins[i].refs != 0) {
            busy = true;
            break;
        }
    }
    spin_unlock_irqrestore(&ref_pins_lock, flags);
    return busy;
}

vfs_status_t vfs_stat_ref(const vfs_node_ref_t *ref, vfs_stat_t *out) {
    if (!ref || !out || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->mode = 0755u;
        out->inode = ref->inode;
        out->nlink = 1u;
        out->uid = 0u;
        out->gid = 0u;
        return VFS_OK;
    }
    if (ref->fs_id == 0 || ref->mount_generation == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->stat_inode) rs = mnt->ops->stat_inode(mnt, ref->inode, out);
    mount_release(mnt);
    return rs;
}
