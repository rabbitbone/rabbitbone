static bool compose_symlink_path(const char *base_dir, const char *target, const char *remaining, char *out, usize out_size) {
    if (!target || !target[0] || !out || out_size == 0) return false;
    char tmp[VFS_PATH_MAX];
    memset(tmp, 0, sizeof(tmp));
    if (target[0] == '/') {
        strncpy(tmp, target, sizeof(tmp) - 1u);
    } else {
        const char *base = (base_dir && base_dir[0]) ? base_dir : "/";
        if (strcmp(base, "/") == 0) {
            if (!str_set_join(tmp, sizeof(tmp), "/", target, 0)) return false;
        } else {
            if (!str_set_join(tmp, sizeof(tmp), base, "/", target)) return false;
        }
    }
    if (remaining && remaining[0]) {
        usize len = strlen(tmp);
        if (len + 1u + strlen(remaining) >= sizeof(tmp)) return false;
        if (len == 0 || tmp[len - 1u] != '/') { if (!str_append(tmp, sizeof(tmp), "/")) return false; }
        if (!str_append(tmp, sizeof(tmp), remaining)) return false;
    }
    return path_normalize(tmp, out, out_size);
}

static ramfs_node_t *resolve_inner(ramfs_t *fs, const char *path, bool follow_final, u32 depth) {
    if (!fs || !path || depth >= RAMFS_SYMLINK_MAX_DEPTH) return 0;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return 0;
    if (strcmp(norm, "/") == 0) return fs->root;
    const char *cursor = norm;
    char comp[VFS_NAME_MAX];
    ramfs_node_t *cur = fs->root;
    char cur_path[VFS_PATH_MAX];
    strcpy(cur_path, "/");
    while (path_next_component(&cursor, comp, sizeof(comp))) {
        ramfs_node_t *next = find_child(cur, comp);
        if (!next) return 0;
        bool has_more = *cursor != 0;
        if (node_type(next) == VFS_NODE_SYMLINK && (follow_final || has_more)) {
            char target[VFS_PATH_MAX];
            usize tlen = next->obj->size;
            if (tlen >= sizeof(target)) return 0;
            memcpy(target, next->obj->data, tlen);
            target[tlen] = 0;
            char combined[VFS_PATH_MAX];
            if (!compose_symlink_path(cur_path, target, cursor, combined, sizeof(combined))) return 0;
            return resolve_inner(fs, combined, follow_final, depth + 1u);
        }
        cur = next;
        if (strcmp(cur_path, "/") != 0 && !str_append(cur_path, sizeof(cur_path), "/")) return 0;
        if (!str_append(cur_path, sizeof(cur_path), comp)) return 0;
    }
    return cur;
}

static ramfs_node_t *resolve(ramfs_t *fs, const char *path) { return resolve_inner(fs, path, true, 0); }
static ramfs_node_t *resolve_nofollow(ramfs_t *fs, const char *path) { return resolve_inner(fs, path, false, 0); }

static vfs_status_t resolve_parent(ramfs_t *fs, const char *path, ramfs_node_t **parent_out, char *leaf, usize leaf_size) {
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) return VFS_ERR_INVAL;
    const char *base = path_basename(norm);
    if (!*base || strlen(base) >= leaf_size) return VFS_ERR_INVAL;
    strcpy(leaf, base);
    char parent_path[VFS_PATH_MAX];
    usize parent_len = (usize)(base - norm);
    if (parent_len == 0) parent_len = 1;
    if (parent_len >= sizeof(parent_path)) return VFS_ERR_INVAL;
    memcpy(parent_path, norm, parent_len);
    if (parent_len > 1 && parent_path[parent_len - 1u] == '/') --parent_len;
    parent_path[parent_len] = 0;
    ramfs_node_t *parent = resolve(fs, parent_path);
    if (!parent) return VFS_ERR_NOENT;
    if (node_type(parent) != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    *parent_out = parent;
    return VFS_OK;
}

ramfs_t *ramfs_create(const char *label) {
    ramfs_t *fs = (ramfs_t *)kcalloc(1, sizeof(*fs));
    if (!fs) return 0;
    strncpy(fs->label, label ? label : "ramfs", sizeof(fs->label) - 1u);
    spinlock_init(&fs->lock);
    fs->next_inode = 1;
    fs->max_nodes = RAMFS_MAX_NODES;
    fs->max_bytes = RAMFS_MAX_TOTAL_BYTES;
    fs->root = dentry_create(fs, "", VFS_NODE_DIR);
    if (!fs->root) { kfree(fs); return 0; }
    return fs;
}

void ramfs_destroy(ramfs_t *fs) {
    if (!fs) return;
    free_dentry(fs, fs->root);
    kfree(fs);
}

