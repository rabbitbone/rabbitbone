static ext4_status_t dir_replace_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 new_ino, u8 new_type, u32 *old_ino_out, u8 *old_type_out) {
    if (!mnt || !dir || !name || !ext4_name_cstr_valid(name, false)) return EXT4_ERR_RANGE;
    usize name_len = strlen(name);
    u8 *block = ext4_tmp_block(mnt);
    u8 *original = ext4_tmp_block(mnt);
    if (!block || !original) {
        ext4_tmp_block_free(original);
        ext4_tmp_block_free(block);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_ERR_NOT_FOUND;
    u64 size = ext4_inode_size(dir);
    u32 block_count = 0;
    if (!ext4_block_count_u32_checked(mnt, size, &block_count)) { result = EXT4_ERR_RANGE; goto out; }
    for (u32 logical = 0; logical < block_count; ++logical) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(original, block, (usize)mnt->block_size);
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            usize next = 0;
            if (rec_len < 8u || (rec_len & 3u) != 0 || !checked_add_usize(p, rec_len, &next) || next > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && !ext4_name_bytes_valid(de->name, de->name_len, true)) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, name_len) == 0) {
                u32 old_ino = le32(de->inode);
                u8 old_type = de->file_type;
                de->inode = new_ino;
                de->file_type = new_type;
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_remove_name(mnt, dir, name, logical, p);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    result = st;
                    goto out;
                }
                st = htree_note_add(mnt, dir_ino, dir, name, new_ino, new_type, logical, p);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    (void)htree_note_add(mnt, dir_ino, dir, name, old_ino, old_type, logical, p);
                    result = st;
                    goto out;
                }
                st = write_inode(mnt, dir_ino, dir);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    (void)htree_remove_name(mnt, dir, name, logical, p);
                    (void)htree_note_add(mnt, dir_ino, dir, name, old_ino, old_type, logical, p);
                    result = st;
                    goto out;
                }
                if (old_ino_out) *old_ino_out = old_ino;
                if (old_type_out) *old_type_out = old_type;
                result = EXT4_OK;
                goto out;
            }
            p = next;
        }
    }
out:
    ext4_tmp_block_free(original);
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t dir_update_dotdot(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u32 new_parent_ino) {
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, 0, &phys, false, false, false);
    if (st != EXT4_OK) return st;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, phys, block);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    usize p = 0;
    while (p + 8u <= mnt->block_size) {
        ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
        u16 rec_len = le16(de->rec_len);
        usize next = 0;
        if (rec_len < 8u || (rec_len & 3u) != 0 || !checked_add_usize(p, rec_len, &next) || next > mnt->block_size) { ext4_tmp_block_free(block); return EXT4_ERR_CORRUPT; }
        if (de->inode && !ext4_name_bytes_valid(de->name, de->name_len, true)) { ext4_tmp_block_free(block); return EXT4_ERR_CORRUPT; }
        if (de->inode && de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.') {
            de->inode = new_parent_ino;
            st = write_block(mnt, phys, block);
            ext4_tmp_block_free(block);
            return st;
        }
        p = next;
    }
    ext4_tmp_block_free(block);
    return EXT4_ERR_CORRUPT;
}

ext4_status_t ext4_rename(ext4_mount_t *mnt, const char *old_path, const char *new_path) {
    if (!mnt || !old_path || !new_path || strcmp(old_path, "/") == 0 || strcmp(new_path, "/") == 0) return EXT4_ERR_RANGE;
    char old_parent_path[VFS_PATH_MAX];
    char new_parent_path[VFS_PATH_MAX];
    char old_name[EXT4_NAME_LEN + 1];
    char new_name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(old_path, old_parent_path, sizeof(old_parent_path), old_name, sizeof(old_name))) return EXT4_ERR_RANGE;
    if (!split_parent_path(new_path, new_parent_path, sizeof(new_parent_path), new_name, sizeof(new_name))) return EXT4_ERR_RANGE;
    if (!ext4_name_cstr_valid(old_name, false) || !ext4_name_cstr_valid(new_name, false)) return EXT4_ERR_RANGE;
    if (strcmp(old_parent_path, new_parent_path) == 0 && strcmp(old_name, new_name) == 0) return EXT4_OK;

    ext4_inode_disk_t old_parent, new_parent, inode;
    u32 old_parent_ino = 0, new_parent_ino = 0, src_ino = 0, target_ino = 0;
    u8 src_type = 0, target_type = 0;
    ext4_status_t st = ext4_lookup_path(mnt, old_parent_path, &old_parent, &old_parent_ino);
    if (st != EXT4_OK) return st;
    st = ext4_lookup_path(mnt, new_parent_path, &new_parent, &new_parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&old_parent) || !ext4_inode_is_dir(&new_parent)) return EXT4_ERR_NOT_DIR;
    st = ext4_find_in_dir(mnt, &old_parent, old_name, &src_ino, &src_type);
    if (st != EXT4_OK) return st;
    st = ext4_read_inode(mnt, src_ino, &inode);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t old_parent_before = old_parent;
    ext4_inode_disk_t new_parent_before = new_parent;
    ext4_inode_disk_t inode_before = inode;
    bool is_dir = ext4_inode_is_dir(&inode);
    if (!is_dir && !ext4_inode_is_regular(&inode) && !ext4_inode_is_symlink(&inode)) return EXT4_ERR_UNSUPPORTED;
    if (is_dir) {
        usize old_len = strlen(old_path);
        if (strncmp(new_path, old_path, old_len) == 0 && (new_path[old_len] == '/' || new_path[old_len] == 0)) return EXT4_ERR_RANGE;
    }

    ext4_status_t target_st = ext4_find_in_dir(mnt, &new_parent, new_name, &target_ino, &target_type);
    if (target_st == EXT4_OK) {
        if (target_ino == src_ino) return EXT4_OK;
        ext4_inode_disk_t target_inode;
        st = ext4_read_inode(mnt, target_ino, &target_inode);
        if (st != EXT4_OK) return st;
        bool target_is_dir = ext4_inode_is_dir(&target_inode);
        if (target_is_dir != is_dir) return EXT4_ERR_EXIST;
        if (target_is_dir) {
            bool empty = true;
            st = ext4_list_dir(mnt, &target_inode, dir_is_empty_cb, &empty);
            if (st != EXT4_OK) return st;
            if (!empty) return EXT4_ERR_NOT_EMPTY;
        }
        u32 replaced_ino = 0;
        u8 replaced_type = 0;
        st = dir_replace_entry(mnt, new_parent_ino, &new_parent, new_name, src_ino, src_type, &replaced_ino, &replaced_type);
        if (st != EXT4_OK) return st;
        if (old_parent_ino == new_parent_ino) old_parent = new_parent;
        st = dir_remove_entry(mnt, old_parent_ino, &old_parent, old_name, 0, 0);
        if (st != EXT4_OK) {
            (void)dir_replace_entry(mnt, new_parent_ino, &new_parent, new_name, replaced_ino, replaced_type, 0, 0);
            return st;
        }
        if (is_dir && old_parent_ino != new_parent_ino) {
            if (!ext4_links_dec_checked(&old_parent)) {
                (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
                (void)dir_replace_entry(mnt, new_parent_ino, &new_parent_before, new_name, replaced_ino, replaced_type, 0, 0);
                return EXT4_ERR_CORRUPT;
            }
            st = write_inode(mnt, old_parent_ino, &old_parent);
            if (st != EXT4_OK) {
                (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
                (void)dir_replace_entry(mnt, new_parent_ino, &new_parent_before, new_name, replaced_ino, replaced_type, 0, 0);
                return st;
            }
            st = dir_update_dotdot(mnt, &inode, new_parent_ino);
            if (st != EXT4_OK) {
                (void)write_inode(mnt, old_parent_ino, &old_parent_before);
                (void)write_inode(mnt, src_ino, &inode_before);
                (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
                (void)dir_replace_entry(mnt, new_parent_ino, &new_parent_before, new_name, replaced_ino, replaced_type, 0, 0);
                return st;
            }
        }
        st = drop_removed_link(mnt, target_ino, &target_inode, target_is_dir);
        if (st != EXT4_OK) {
            if (is_dir && old_parent_ino != new_parent_ino) (void)write_inode(mnt, old_parent_ino, &old_parent_before);
            (void)write_inode(mnt, target_ino, &target_inode);
            (void)write_inode(mnt, src_ino, &inode_before);
            (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
            (void)dir_replace_entry(mnt, new_parent_ino, &new_parent_before, new_name, replaced_ino, replaced_type, 0, 0);
            return st;
        }
        return EXT4_OK;
    }
    if (target_st != EXT4_ERR_NOT_FOUND) return target_st;

    st = dir_add_entry(mnt, new_parent_ino, &new_parent, new_name, src_ino, src_type);
    if (st != EXT4_OK) return st;
    if (old_parent_ino == new_parent_ino) old_parent = new_parent;
    st = dir_remove_entry(mnt, old_parent_ino, &old_parent, old_name, 0, 0);
    if (st != EXT4_OK) {
        (void)dir_remove_entry(mnt, new_parent_ino, &new_parent, new_name, 0, 0);
        return st;
    }
    if (is_dir && old_parent_ino != new_parent_ino) {
        if (!ext4_links_dec_checked(&old_parent) || !ext4_links_inc_checked(&new_parent)) {
            (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
            (void)dir_remove_entry(mnt, new_parent_ino, &new_parent, new_name, 0, 0);
            return EXT4_ERR_CORRUPT;
        }
        st = write_inode(mnt, old_parent_ino, &old_parent);
        if (st != EXT4_OK) {
            (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
            (void)dir_remove_entry(mnt, new_parent_ino, &new_parent, new_name, 0, 0);
            return st;
        }
        st = write_inode(mnt, new_parent_ino, &new_parent);
        if (st != EXT4_OK) {
            (void)write_inode(mnt, old_parent_ino, &old_parent_before);
            (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
            (void)dir_remove_entry(mnt, new_parent_ino, &new_parent_before, new_name, 0, 0);
            return st;
        }
        st = dir_update_dotdot(mnt, &inode, new_parent_ino);
        if (st != EXT4_OK) {
            (void)write_inode(mnt, old_parent_ino, &old_parent_before);
            (void)write_inode(mnt, new_parent_ino, &new_parent_before);
            (void)write_inode(mnt, src_ino, &inode_before);
            (void)dir_add_entry(mnt, old_parent_ino, &old_parent_before, old_name, src_ino, src_type);
            (void)dir_remove_entry(mnt, new_parent_ino, &new_parent_before, new_name, 0, 0);
            return st;
        }
    }
    return EXT4_OK;
}

__attribute__((noinline)) ext4_status_t ext4_list_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_dir_iter_fn fn, void *ctx) {
    if (!mnt || !dir || !fn) return EXT4_ERR_IO;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 size = ext4_inode_size(dir);
    u8 *block = (u8 *)kmalloc((usize)mnt->block_size);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u32 block_count = 0;
    if (!ext4_block_count_u32_checked(mnt, size, &block_count)) { kfree(block); return EXT4_ERR_RANGE; }
    for (u32 logical = 0; logical < block_count; ++logical) {
        u64 off = 0;
        if (!ext4_logical_offset_checked(mnt, logical, &off)) { result = EXT4_ERR_RANGE; goto done; }
        usize got = 0;
        ext4_status_t st = ext4_read_file(mnt, dir, off, block, (usize)mnt->block_size, &got);
        if (st != EXT4_OK) { result = st; break; }
        usize p = 0;
        while (p + 8 <= got) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            usize next = 0;
            if (rec_len < 8 || !checked_add_usize(p, rec_len, &next) || next > got) { result = EXT4_ERR_CORRUPT; goto done; }
            if (de->inode != 0 && !ext4_name_bytes_valid(de->name, de->name_len, true)) { result = EXT4_ERR_CORRUPT; goto done; }
            if (de->inode != 0 && 8u + de->name_len <= rec_len) {
                ext4_dirent_t e;
                memset(&e, 0, sizeof(e));
                e.inode = le32(de->inode);
                e.file_type = de->file_type;
                memcpy(e.name, de->name, de->name_len);
                e.name[de->name_len] = 0;
                if (!fn(&e, ctx)) goto done;
            }
            p = next;
        }
    }
done:
    kfree(block);
    return result;
}

typedef struct ext4_find_ctx {
    const char *name;
    u32 ino;
    u8 type;
    bool found;
} ext4_find_ctx_t;

