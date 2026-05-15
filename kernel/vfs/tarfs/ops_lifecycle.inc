static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    tarfs_t *fs = (tarfs_t *)mnt->ctx;
    if (strcmp(path, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->fs_id = mnt->fs_id;
        return VFS_OK;
    }
    tarfs_entry_t *e = find_exact(fs, path);
    if (!e) {
        char norm[VFS_PATH_MAX];
        if (path_normalize(path, norm, sizeof(norm)) && has_children(fs, norm)) {
            memset(out, 0, sizeof(*out));
            out->type = VFS_NODE_DIR;
            out->fs_id = mnt->fs_id;
            return VFS_OK;
        }
        return VFS_ERR_NOENT;
    }
    memset(out, 0, sizeof(*out));
    out->type = e->type;
    out->size = e->size;
    out->inode = e->inode;
    out->mode = e->type == VFS_NODE_DIR ? 0555u : 0444u; out->uid = RABBITBONE_UID_ROOT; out->gid = RABBITBONE_GID_ROOT;
    out->fs_id = mnt->fs_id;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    tarfs_entry_t *e = find_exact((tarfs_t *)mnt->ctx, path);
    if (!e) return VFS_ERR_NOENT;
    if (e->type != VFS_NODE_FILE) return VFS_ERR_ISDIR;
    if (offset >= e->size) return VFS_OK;
    usize take = e->size - (usize)offset;
    if (take > size) take = size;
    memcpy(buffer, e->data + offset, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    if (!fn) return VFS_ERR_INVAL;
    tarfs_t *fs = (tarfs_t *)mnt->ctx;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    vfs_stat_t st;
    vfs_status_t s = op_stat(mnt, norm, &st);
    if (s != VFS_OK) return s;
    if (st.type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    for (usize i = 0; i < fs->count; ++i) {
        char name[VFS_NAME_MAX];
        bool implicit_dir = false;
        if (!immediate_child_component(norm, fs->entries[i].path, name, sizeof(name), &implicit_dir)) continue;
        bool duplicate = false;
        for (usize j = 0; j < i; ++j) {
            char prev[VFS_NAME_MAX];
            bool prev_implicit = false;
            if (immediate_child_component(norm, fs->entries[j].path, prev, sizeof(prev), &prev_implicit) && strcmp(prev, name) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strcpy(de.name, name);
        de.type = implicit_dir ? VFS_NODE_DIR : fs->entries[i].type;
        de.size = implicit_dir ? 0u : fs->entries[i].size;
        de.inode = implicit_dir ? 0u : fs->entries[i].inode;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .read = op_read,
    .list = op_list,
};

void tarfs_destroy(tarfs_t *fs) {
    if (!fs) return;
    kfree(fs->entries);
    kfree(fs);
}

const vfs_ops_t *tarfs_ops(void) { return &ops; }
