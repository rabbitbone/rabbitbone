static ext4_status_t inode_zero_tail(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u64 size) {
    if (size == 0 || (size % mnt->block_size) == 0) return EXT4_OK;
    u64 logical64 = size / mnt->block_size;
    if (logical64 > 0xffffffffull) return EXT4_ERR_RANGE;
    u32 logical = (u32)logical64;
    u32 off = (u32)(size % mnt->block_size);
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, inode, logical, &phys, false, false, false);
    if (st != EXT4_OK) return st == EXT4_ERR_RANGE ? EXT4_OK : st;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    st = read_data_block(mnt, phys, block);
    if (st == EXT4_OK) {
        memset(block + off, 0, (usize)mnt->block_size - off);
        st = write_data_block(mnt, phys, block);
    }
    ext4_tmp_block_free(block);
    return st;
}

ext4_status_t ext4_write_file(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 offset, const void *buffer, usize bytes, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    if (!ext4_inode_is_regular(inode) && !ext4_inode_is_symlink(inode)) return EXT4_ERR_UNSUPPORTED;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    if (bytes == 0) return EXT4_OK;
    u64 old_size = ext4_inode_size(inode);
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    ext4_inode_disk_t persisted_inode = *inode;
    if (offset > old_size) {
        ext4_status_t zs = inode_zero_tail(mnt, inode, old_size);
        if (zs != EXT4_OK) return zs;
    }
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    const u8 *in = (const u8 *)buffer;
    usize done = 0;
    ext4_status_t st = EXT4_OK;
    u32 extent_mutation_saved = ext4_extent_mutation_enter(ino);
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) { st = EXT4_ERR_RANGE; goto out; }
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) { st = EXT4_ERR_RANGE; goto out; }
        u32 logical = (u32)logical64;
        u32 block_off = (u32)(abs % mnt->block_size);
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        bool zero_new = (block_off != 0 || take != (usize)mnt->block_size);
        u64 phys = 0;
        st = inode_block_for_write(mnt, inode, logical, &phys, true, zero_new, false);
        if (st != EXT4_OK) goto out;
        if (memcmp(&persisted_inode, inode, sizeof(persisted_inode)) != 0) {
            st = write_inode(mnt, ino, inode);
            if (st != EXT4_OK) goto out;
            persisted_inode = *inode;
        }
        if (block_off != 0 || take != (usize)mnt->block_size) {
            if (logical64 >= old_blocks64) {
                memset(block, 0, (usize)mnt->block_size);
            } else {
                st = read_data_block(mnt, phys, block);
                if (st != EXT4_OK) goto out;
            }
        } else memset(block, 0, (usize)mnt->block_size);
        memcpy(block + block_off, in + done, take);
        st = write_data_block(mnt, phys, block);
        if (st != EXT4_OK) goto out;
        done += take;
    }
    u64 end = 0;
    if (!checked_add_u64(offset, (u64)done, &end)) { st = EXT4_ERR_RANGE; goto out; }
    if (end > ext4_inode_size(inode)) inode_set_size(inode, end);
    st = write_inode(mnt, ino, inode);
    if (st == EXT4_OK && written_out) *written_out = done;
out:
    ext4_extent_mutation_leave(extent_mutation_saved);
    ext4_tmp_block_free(block);
    return st;
}



static ext4_status_t ext4_preallocate_file_loaded(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 size) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_regular(inode)) return EXT4_ERR_UNSUPPORTED;
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) == 0) return EXT4_ERR_UNSUPPORTED;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u64 old_size = ext4_inode_size(inode);
    if (size <= old_size) return EXT4_OK;
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    u64 new_blocks64 = div_round_up_u64(size, mnt->block_size);
    if (new_blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    ext4_status_t st = EXT4_OK;
    ext4_inode_disk_t persisted_inode = *inode;
    if (old_size && (old_size % mnt->block_size) != 0) {
        st = inode_zero_tail(mnt, inode, old_size);
        if (st != EXT4_OK) return st;
    }
    u32 extent_mutation_saved = ext4_extent_mutation_enter(ino);
    for (u64 l = old_blocks64; l < new_blocks64; ++l) {
        u64 phys = 0;
        st = inode_block_for_write(mnt, inode, (u32)l, &phys, true, false, true);
        if (st != EXT4_OK) { ext4_extent_mutation_leave(extent_mutation_saved); return st; }
        if (memcmp(&persisted_inode, inode, sizeof(persisted_inode)) != 0) {
            st = write_inode(mnt, ino, inode);
            if (st != EXT4_OK) { ext4_extent_mutation_leave(extent_mutation_saved); return st; }
            persisted_inode = *inode;
        }
    }
    ext4_extent_mutation_leave(extent_mutation_saved);
    inode_set_size(inode, size);
    return write_inode(mnt, ino, inode);
}

ext4_status_t ext4_preallocate_file_inode(ext4_mount_t *mnt, u32 ino, u64 size) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    return ext4_preallocate_file_loaded(mnt, ino, &inode, size);
}

ext4_status_t ext4_preallocate_file_path(ext4_mount_t *mnt, const char *path, u64 size) {
    if (!mnt || !path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, path, &inode, &ino);
    if (st != EXT4_OK) return st;
    return ext4_preallocate_file_loaded(mnt, ino, &inode, size);
}

static ext4_status_t ext4_truncate_file_loaded(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 new_size) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    if (ext4_inode_is_dir(inode)) return EXT4_ERR_UNSUPPORTED;
    if (!ext4_inode_is_regular(inode)) return EXT4_ERR_UNSUPPORTED;
    u64 old_size = ext4_inode_size(inode);
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    u64 new_blocks64 = div_round_up_u64(new_size, mnt->block_size);
    if (old_blocks64 > 0xffffffffull || new_blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) == 0 && new_blocks64 > 12ull + (mnt->block_size / 4u)) return EXT4_ERR_UNSUPPORTED;
    ext4_status_t st = EXT4_OK;
    if (new_blocks64 > old_blocks64) {
        st = inode_zero_tail(mnt, inode, old_size);
        if (st != EXT4_OK) return st;
    } else if (new_blocks64 < old_blocks64) {
        for (u64 l = old_blocks64; l > new_blocks64; --l) {
            st = inode_free_logical_block(mnt, ino, inode, (u32)(l - 1u));
            if (st != EXT4_OK) return st;
        }
        if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
            const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
            if (le16(hdr->eh_magic) == EXT4_EXT_MAGIC && le16(hdr->eh_depth) == 1u) {
                ext4_deferred_free_t deferred;
                memset(&deferred, 0, sizeof(deferred));
                st = extent_try_demote_index_to_inline(mnt, inode, &deferred);
                if (st != EXT4_OK) return st;
                st = write_inode(mnt, ino, inode);
                if (st != EXT4_OK) return st;
                st = deferred_free_run(mnt, &deferred);
                if (st != EXT4_OK) return st;
            }
        }
    }
    st = inode_zero_tail(mnt, inode, new_size);
    if (st != EXT4_OK) return st;
    inode_set_size(inode, new_size);
    return write_inode(mnt, ino, inode);
}

ext4_status_t ext4_truncate_file_inode(ext4_mount_t *mnt, u32 ino, u64 new_size) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    return ext4_truncate_file_loaded(mnt, ino, &inode, new_size);
}

ext4_status_t ext4_truncate_file_path(ext4_mount_t *mnt, const char *path, u64 new_size) {
    if (!mnt || !path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, path, &inode, &ino);
    if (st != EXT4_OK) return st;
    return ext4_truncate_file_loaded(mnt, ino, &inode, new_size);
}

