static ext4_status_t inode_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        u16 depth = le16(hdr->eh_depth);
        if (depth == 0) return extent_inline_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        if (depth == 1) return extent_index_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        if (depth == 2) return extent_depth2_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        return EXT4_ERR_UNSUPPORTED;
    }
    if (logical < 12u) {
        u32 b = le32(inode->i_block[logical]);
        if (!b && allocate) {
            u64 nb = 0;
            ext4_status_t st = alloc_data_block(mnt, mnt->allocator_next_block, zero_new, &nb);
            if (st != EXT4_OK) return st;
            inode->i_block[logical] = (u32)nb;
            inode_add_blocks(mnt, inode, 1);
            b = (u32)nb;
        }
        if (!b) return EXT4_ERR_RANGE;
        *phys_out = b;
        return EXT4_OK;
    }
    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical >= 12u + per_block) return EXT4_ERR_UNSUPPORTED;
    u32 indirect = le32(inode->i_block[12]);
    if (!indirect && allocate) {
        u64 nb = 0;
        ext4_status_t st = alloc_block(mnt, &nb);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = (u32)nb;
        inode_add_blocks(mnt, inode, 1);
        indirect = (u32)nb;
    }
    if (!indirect) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b && allocate) {
        u64 nb = 0;
        st = alloc_data_block(mnt, mnt->allocator_next_block, zero_new, &nb);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        put32(&entries[idx], (u32)nb);
        st = write_block(mnt, indirect, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        inode_add_blocks(mnt, inode, 1);
        b = (u32)nb;
    }
    ext4_tmp_block_free(block);
    if (!b) return EXT4_ERR_RANGE;
    *phys_out = b;
    return EXT4_OK;
}


static ext4_status_t inode_free_logical_block(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u32 logical) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t before = *inode;
    ext4_deferred_free_t deferred;
    memset(&deferred, 0, sizeof(deferred));

    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        u16 depth = le16(hdr->eh_depth);
        ext4_status_t st = EXT4_ERR_UNSUPPORTED;
        if (depth == 0) st = extent_inline_free_logical_block(mnt, inode, logical, &deferred);
        else if (depth == 1) st = extent_index_free_logical_block(mnt, inode, logical, &deferred);
        else if (depth == 2) st = extent_depth2_free_logical_block(mnt, inode, logical, &deferred);
        if (st != EXT4_OK) { *inode = before; return st; }
        if (deferred.count == 0 && memcmp(&before, inode, sizeof(before)) == 0) return EXT4_OK;
        st = write_inode(mnt, ino, inode);
        if (st != EXT4_OK) { *inode = before; return st; }
        return deferred_free_run(mnt, &deferred);
    }

    if (logical < 12u) {
        u32 b = le32(inode->i_block[logical]);
        if (!b) return EXT4_OK;
        inode->i_block[logical] = 0;
        inode_add_blocks(mnt, inode, -1);
        ext4_status_t st = write_inode(mnt, ino, inode);
        if (st != EXT4_OK) { *inode = before; return st; }
        st = deferred_free_add(&deferred, b);
        if (st != EXT4_OK) return st;
        return deferred_free_run(mnt, &deferred);
    }

    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical >= 12u + per_block) return EXT4_ERR_UNSUPPORTED;
    u32 indirect = le32(inode->i_block[12]);
    if (!indirect) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    u8 *old_block = ext4_tmp_block(mnt);
    if (!block || !old_block) {
        ext4_tmp_block_free(old_block);
        ext4_tmp_block_free(block);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) goto out;
    memcpy(old_block, block, (usize)mnt->block_size);
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b) goto out;
    put32(&entries[idx], 0);
    bool any = false;
    for (u32 i = 0; i < per_block; ++i) {
        if (le32(entries[i]) != 0) { any = true; break; }
    }
    st = write_block(mnt, indirect, block);
    if (st != EXT4_OK) goto out;
    inode_add_blocks(mnt, inode, -1);
    st = deferred_free_add(&deferred, b);
    if (st != EXT4_OK) { (void)write_block(mnt, indirect, old_block); *inode = before; goto out; }
    if (!any) {
        inode->i_block[12] = 0;
        inode_add_blocks(mnt, inode, -1);
        st = write_inode(mnt, ino, inode);
        if (st != EXT4_OK) {
            *inode = before;
            (void)write_block(mnt, indirect, old_block);
            goto out;
        }
        st = deferred_free_add(&deferred, indirect);
        if (st != EXT4_OK) goto out;
    }
    st = deferred_free_run(mnt, &deferred);
out:
    ext4_tmp_block_free(old_block);
    ext4_tmp_block_free(block);
    return st;
}
