static ext4_status_t dir_add_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 child_ino, u8 type) {
    if (!mnt || !dir || !name) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    if (!ext4_name_cstr_valid(name, false)) return EXT4_ERR_RANGE;
    usize name_len = strlen(name);
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
    u64 block_count64 = div_round_up_u64(size ? size : mnt->block_size, mnt->block_size);
    if (block_count64 > 0xffffffffull) { result = EXT4_ERR_RANGE; goto out; }
    u32 blocks = (u32)block_count64;
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
            usize next = 0;
            if (rec_len < 8 || !checked_add_usize(p, rec_len, &next) || next > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            usize insert_off = 0;
            bool found_slot = false;
            if (de->inode == 0 && rec_len >= need) {
                insert_off = p;
                make_dirent_at(block, insert_off, child_ino, rec_len, type, name);
                found_slot = true;
            } else {
                if (de->inode != 0 && !ext4_name_bytes_valid(de->name, de->name_len, true)) { result = EXT4_ERR_CORRUPT; goto out; }
                u16 used = ext4_rec_len(de->name_len);
                if (de->inode != 0 && rec_len >= used + need) {
                    de->rec_len = used;
                    if (!checked_add_usize(p, used, &insert_off)) { result = EXT4_ERR_CORRUPT; goto out; }
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
            p = next;
        }
    }
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, blocks, &phys, true, false, false);
    if (st != EXT4_OK) { result = st; goto out; }
    memset(block, 0, (usize)mnt->block_size);
    make_dirent_at(block, 0, child_ino, (u16)mnt->block_size, type, name);
    st = write_block(mnt, phys, block);
    if (st != EXT4_OK) { *dir = old_dir; result = st; goto out; }
    u64 new_dir_size = 0;
    if (!checked_add_u64(size, mnt->block_size, &new_dir_size)) {
        (void)free_block(mnt, phys);
        *dir = old_dir;
        result = EXT4_ERR_RANGE;
        goto out;
    }
    inode_set_size(dir, new_dir_size);
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
    if (!ext4_name_cstr_valid(name, false)) return EXT4_ERR_RANGE;
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
        ext4_dir_entry_2_t *prev = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            usize next = 0;
            if (rec_len < 8 || !checked_add_usize(p, rec_len, &next) || next > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && !ext4_name_bytes_valid(de->name, de->name_len, true)) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, name_len) == 0) {
                u32 old_ino = le32(de->inode);
                u8 old_type = de->file_type;
                if (prev) {
                    u16 prev_len = le16(prev->rec_len);
                    usize merged = 0;
                    if (!checked_add_usize(prev_len, rec_len, &merged) || merged > mnt->block_size || merged > 0xffffu) {
                        result = EXT4_ERR_CORRUPT;
                        goto out;
                    }
                    prev->rec_len = (u16)merged;
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
            p = next;
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
