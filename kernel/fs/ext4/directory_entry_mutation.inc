static ext4_status_t dir_add_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 child_ino, u8 type) {
    if (!mnt || !dir || !name) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    usize name_len = strlen(name);
    if (name_len == 0 || name_len > EXT4_NAME_LEN) return EXT4_ERR_RANGE;
    u16 need = ext4_rec_len(name_len);
    u8 *block = ext4_tmp_block(mnt);
    u8 *original = ext4_tmp_block(mnt);
    if (!block || !original) {
        ext4_tmp_block_free(original);
        ext4_tmp_block_free(block);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_inode_disk_t old_dir = *dir;
    u32 extent_mutation_saved = ext4_extent_mutation_enter(dir_ino);
    ext4_status_t result = EXT4_OK;
    u64 size = ext4_inode_size(dir);
    u32 blocks = (u32)div_round_up_u64(size ? size : mnt->block_size, mnt->block_size);
    for (u32 logical = 0; logical < blocks; ++logical) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(original, block, (usize)mnt->block_size);
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            usize insert_off = 0;
            bool found_slot = false;
            if (de->inode == 0 && rec_len >= need) {
                insert_off = p;
                make_dirent_at(block, insert_off, child_ino, rec_len, type, name);
                found_slot = true;
            } else {
                u16 used = ext4_rec_len(de->name_len);
                if (de->inode != 0 && rec_len >= used + need) {
                    de->rec_len = used;
                    insert_off = p + used;
                    make_dirent_at(block, insert_off, child_ino, rec_len - used, type, name);
                    found_slot = true;
                }
            }
            if (found_slot) {
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_note_add(mnt, dir_ino, dir, name, child_ino, type, logical, insert_off);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    *dir = old_dir;
                    result = st;
                    goto out;
                }
                st = write_inode(mnt, dir_ino, dir);
                if (st != EXT4_OK) {
                    (void)htree_remove_name(mnt, dir, name, logical, insert_off);
                    (void)write_block(mnt, phys, original);
                    *dir = old_dir;
                    result = st;
                    goto out;
                }
                result = EXT4_OK;
                goto out;
            }
            p += rec_len;
        }
    }
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, blocks, &phys, true, false, false);
    if (st != EXT4_OK) { result = st; goto out; }
    memset(block, 0, (usize)mnt->block_size);
    make_dirent_at(block, 0, child_ino, (u16)mnt->block_size, type, name);
    st = write_block(mnt, phys, block);
    if (st != EXT4_OK) { *dir = old_dir; result = st; goto out; }
    inode_set_size(dir, size + mnt->block_size);
    st = htree_note_add(mnt, dir_ino, dir, name, child_ino, type, blocks, 0);
    if (st != EXT4_OK) {
        (void)free_block(mnt, phys);
        *dir = old_dir;
        (void)write_inode(mnt, dir_ino, &old_dir);
        result = st;
        goto out;
    }
    st = write_inode(mnt, dir_ino, dir);
    if (st != EXT4_OK) {
        (void)htree_remove_name(mnt, dir, name, blocks, 0);
        (void)free_block(mnt, phys);
        *dir = old_dir;
        (void)write_inode(mnt, dir_ino, &old_dir);
        result = st;
        goto out;
    }
    result = EXT4_OK;
out:
    ext4_extent_mutation_leave(extent_mutation_saved);
    ext4_tmp_block_free(original);
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t dir_remove_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    if (!mnt || !dir || !name) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    u8 *original = ext4_tmp_block(mnt);
    if (!block || !original) {
        ext4_tmp_block_free(original);
        ext4_tmp_block_free(block);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_ERR_NOT_FOUND;
    usize name_len = strlen(name);
    u64 size = ext4_inode_size(dir);
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 logical64 = off / mnt->block_size;
        if (logical64 > 0xffffffffull) { result = EXT4_ERR_RANGE; goto out; }
        u32 logical = (u32)logical64;
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(original, block, (usize)mnt->block_size);
        usize p = 0;
        ext4_dir_entry_2_t *prev = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, de->name_len) == 0) {
                u32 old_ino = le32(de->inode);
                u8 old_type = de->file_type;
                if (prev) {
                    u16 prev_len = le16(prev->rec_len);
                    prev->rec_len = (u16)(prev_len + rec_len);
                } else {
                    dirent_make_free((u8 *)de, rec_len);
                }
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_remove_name(mnt, dir, name, logical, p);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    result = st;
                    goto out;
                }
                st = write_inode(mnt, dir_ino, dir);
                if (st != EXT4_OK) {
                    (void)write_block(mnt, phys, original);
                    (void)htree_note_add(mnt, dir_ino, dir, name, old_ino, old_type, logical, p);
                    result = st;
                    goto out;
                }
                if (ino_out) *ino_out = old_ino;
                if (type_out) *type_out = old_type;
                result = EXT4_OK;
                goto out;
            }
            prev = de;
            p += rec_len;
        }
    }
out:
    ext4_tmp_block_free(original);
    ext4_tmp_block_free(block);
    return result;
}

static bool dir_is_empty_cb(const ext4_dirent_t *entry, void *ctx) {
    bool *empty = (bool *)ctx;
    if (strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0) {
        *empty = false;
        return false;
    }
    return true;
}
