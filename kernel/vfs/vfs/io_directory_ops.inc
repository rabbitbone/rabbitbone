vfs_status_t vfs_read(const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!path || (size && !buffer)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->ops->read) { mount_release(mnt); return VFS_ERR_NOENT; }
    rs = mnt->ops->read(mnt, route.relative, offset, buffer, size, read_out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_read_ref(const vfs_node_ref_t *ref, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!ref || ref->fs_id == 0 || ref->inode == 0 || ref->mount_generation == 0 || (size && !buffer)) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->read_inode) rs = mnt->ops->read_inode(mnt, ref->inode, offset, buffer, size, read_out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_write(const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!path || (size && !buffer)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->write) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->write(mnt, route.relative, offset, buffer, size, written_out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_write_ref(const vfs_node_ref_t *ref, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!ref || ref->fs_id == 0 || ref->inode == 0 || ref->mount_generation == 0 || (size && !buffer)) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->write_inode) rs = mnt->ops->write_inode(mnt, ref->inode, offset, buffer, size, written_out);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_list(const char *path, vfs_dir_iter_fn fn, void *ctx) {
    if (!path || !fn) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    if (strcmp(route.normalized, "/") == 0) return root_list(fn, ctx);
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->ops->list) { mount_release(mnt); return VFS_ERR_NOENT; }
    rs = mnt->ops->list(mnt, route.relative, fn, ctx);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_list_ref(const vfs_node_ref_t *ref, vfs_dir_iter_fn fn, void *ctx) {
    if (!ref || !fn || ref->inode == 0) return VFS_ERR_INVAL;
    if (ref->fs_id == 0 && strcmp(ref->path, "/") == 0) return root_list(fn, ctx);
    if (ref->fs_id == 0 || ref->mount_generation == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->list_inode) rs = mnt->ops->list_inode(mnt, ref->inode, fn, ctx);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_mkdir(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->mkdir) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->mkdir(mnt, route.relative);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_create(const char *path, const void *data, usize size) {
    if (!path || (size && !data)) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->create) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->create(mnt, route.relative, data, size);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_unlink(const char *path) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->unlink) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->unlink(mnt, route.relative);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_truncate(const char *path, u64 size) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->truncate) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->truncate(mnt, route.relative, size);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_truncate_ref(const vfs_node_ref_t *ref, u64 size) {
    if (!ref || ref->fs_id == 0 || ref->inode == 0 || ref->mount_generation == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->truncate_inode) rs = mnt->ops->truncate_inode(mnt, ref->inode, size);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_rename(const char *old_path, const char *new_path) {
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
    if (!mnt->ops->rename) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->rename(mnt, old_route.relative, new_route.relative);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_preallocate(const char *path, u64 size) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->preallocate) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->preallocate(mnt, route.relative, size);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_preallocate_ref(const vfs_node_ref_t *ref, u64 size) {
    if (!ref || ref->fs_id == 0 || ref->inode == 0 || ref->mount_generation == 0) return VFS_ERR_INVAL;
    vfs_mount_t *mnt = mount_by_fs_id(ref->fs_id, ref->mount_generation);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    vfs_status_t rs = VFS_ERR_UNSUPPORTED;
    if (mnt->ops->preallocate_inode) rs = mnt->ops->preallocate_inode(mnt, ref->inode, size);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_chmod(const char *path, u32 mode) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->chmod) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->chmod(mnt, route.relative, mode);
    mount_release(mnt);
    return rs;
}

vfs_status_t vfs_chown(const char *path, u32 uid, u32 gid) {
    if (!path) return VFS_ERR_INVAL;
    vfs_route_t route;
    vfs_status_t rs = resolve_route(path, &route);
    if (rs != VFS_OK) return rs;
    vfs_mount_t *mnt = route_mount(&route);
    if (!mnt) return VFS_ERR_NOENT;
    if (!mnt->writable) { mount_release(mnt); return VFS_ERR_PERM; }
    if (!mnt->ops->chown) { mount_release(mnt); return VFS_ERR_UNSUPPORTED; }
    rs = mnt->ops->chown(mnt, route.relative, uid, gid);
    mount_release(mnt);
    return rs;
}
