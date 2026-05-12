static bool find_dir_cb(const ext4_dirent_t *entry, void *ctxp) {
    ext4_find_ctx_t *ctx = (ext4_find_ctx_t *)ctxp;
    if (strcmp(entry->name, ctx->name) == 0) {
        ctx->ino = entry->inode;
        ctx->type = entry->file_type;
        ctx->found = true;
        return false;
    }
    return true;
}

ext4_status_t ext4_find_in_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    if (!mnt || !dir || !name || !ino_out) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    if ((le32(dir->i_flags) & EXT4_INDEX_FL) != 0) {
        ext4_status_t hs = htree_lookup(mnt, dir, name, ino_out, type_out);
        if (hs == EXT4_OK) return EXT4_OK;
        if (hs != EXT4_ERR_NOT_FOUND && hs != EXT4_ERR_CORRUPT) return hs;
    }
    ext4_find_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ext4_status_t st = ext4_list_dir(mnt, dir, find_dir_cb, &ctx);
    if (st != EXT4_OK) return st;
    if (!ctx.found) return EXT4_ERR_NOT_FOUND;
    *ino_out = ctx.ino;
    if (type_out) *type_out = ctx.type;
    return EXT4_OK;
}

static bool ext4_str_append(char *dst, usize cap, const char *src) {
    if (!dst || !src || cap == 0) return false;
    usize dl = strlen(dst);
    usize sl = strlen(src);
    if (dl >= cap || sl >= cap - dl) return false;
    memcpy(dst + dl, src, sl + 1u);
    return true;
}

static ext4_status_t ext4_read_symlink_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!mnt || !inode || (!buffer && size)) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_symlink(inode)) return EXT4_ERR_UNSUPPORTED;
    u64 sz64 = ext4_inode_size(inode);
    if (sz64 > VFS_PATH_MAX - 1u) return EXT4_ERR_RANGE;
    usize len = (usize)sz64;
    usize take = len < size ? len : size;
    bool fast = len <= sizeof(inode->i_block) && le32(inode->i_blocks_lo) == 0 && !ext4_inode_uses_extents(inode);
    if (fast) {
        if (take) memcpy(buffer, inode->i_block, take);
    } else {
        ext4_status_t st = ext4_read_file(mnt, inode, 0, buffer, take, 0);
        if (st != EXT4_OK) return st;
    }
    if (read_out) *read_out = take;
    return EXT4_OK;
}

static bool ext4_compose_symlink_path(const char *base_dir, const char *target, const char *remaining, char *out, usize out_size) {
    if (!target || !target[0] || !out || out_size == 0) return false;
    char tmp[VFS_PATH_MAX];
    memset(tmp, 0, sizeof(tmp));
    if (target[0] == '/') {
        if (!ext4_str_append(tmp, sizeof(tmp), target)) return false;
    } else {
        const char *base = (base_dir && base_dir[0]) ? base_dir : "/";
        if (strcmp(base, "/") == 0) {
            if (!ext4_str_append(tmp, sizeof(tmp), "/")) return false;
        } else if (!ext4_str_append(tmp, sizeof(tmp), base) || !ext4_str_append(tmp, sizeof(tmp), "/")) {
            return false;
        }
        if (!ext4_str_append(tmp, sizeof(tmp), target)) return false;
    }
    if (remaining && remaining[0]) {
        usize len = strlen(tmp);
        if (len == 0 || tmp[len - 1u] != '/') {
            if (!ext4_str_append(tmp, sizeof(tmp), "/")) return false;
        }
        if (!ext4_str_append(tmp, sizeof(tmp), remaining)) return false;
    }
    return path_normalize(tmp, out, out_size);
}

static ext4_status_t ext4_lookup_path_ex(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out, bool follow_final, u32 depth) {
    if (!mnt || !path || !inode_out) return EXT4_ERR_RANGE;
    if (depth >= 16u) return EXT4_ERR_RANGE;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return EXT4_ERR_RANGE;
    ext4_inode_disk_t cur;
    ext4_status_t st = ext4_read_inode(mnt, EXT4_ROOT_INO, &cur);
    if (st != EXT4_OK) return st;
    u32 cur_ino = EXT4_ROOT_INO;
    if (strcmp(norm, "/") == 0) {
        *inode_out = cur;
        if (ino_out) *ino_out = cur_ino;
        return EXT4_OK;
    }
    const char *cursor = norm;
    char comp[EXT4_NAME_LEN + 1];
    char cur_path[VFS_PATH_MAX];
    strcpy(cur_path, "/");
    while (path_next_component(&cursor, comp, sizeof(comp))) {
        if (strcmp(comp, ".") == 0) continue;
        u32 next_ino = 0;
        st = ext4_find_in_dir(mnt, &cur, comp, &next_ino, 0);
        if (st != EXT4_OK) return st;
        ext4_inode_disk_t next;
        st = ext4_read_inode(mnt, next_ino, &next);
        if (st != EXT4_OK) return st;
        bool has_more = *cursor != 0;
        if (ext4_inode_is_symlink(&next) && (follow_final || has_more)) {
            char target[VFS_PATH_MAX];
            usize got = 0;
            st = ext4_read_symlink_inode(mnt, &next, target, sizeof(target) - 1u, &got);
            if (st != EXT4_OK) return st;
            target[got] = 0;
            char combined[VFS_PATH_MAX];
            if (!ext4_compose_symlink_path(cur_path, target, cursor, combined, sizeof(combined))) return EXT4_ERR_RANGE;
            return ext4_lookup_path_ex(mnt, combined, inode_out, ino_out, follow_final, depth + 1u);
        }
        cur = next;
        cur_ino = next_ino;
        if (strcmp(cur_path, "/") != 0 && !ext4_str_append(cur_path, sizeof(cur_path), "/")) return EXT4_ERR_RANGE;
        if (!ext4_str_append(cur_path, sizeof(cur_path), comp)) return EXT4_ERR_RANGE;
    }
    *inode_out = cur;
    if (ino_out) *ino_out = cur_ino;
    return EXT4_OK;
}

ext4_status_t ext4_lookup_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out) {
    return ext4_lookup_path_ex(mnt, path, inode_out, ino_out, true, 0);
}

ext4_status_t ext4_lstat_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out) {
    return ext4_lookup_path_ex(mnt, path, inode_out, ino_out, false, 0);
}

ext4_status_t ext4_readlink(ext4_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_lstat_path(mnt, path, &inode, 0);
    if (st != EXT4_OK) return st;
    return ext4_read_symlink_inode(mnt, &inode, buffer, size, read_out);
}

