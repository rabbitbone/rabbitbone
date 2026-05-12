ext4_status_t ext4_read_inode(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *out) {
    if (!mnt || !out || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1;
    u32 group = idx / mnt->inodes_per_group;
    u32 local = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u64 table = group_inode_table(&gd);
    u64 table_off = 0;
    u64 inode_off = 0;
    u64 off = 0;
    if (!checked_mul_u64(table, mnt->block_size, &table_off)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)local, mnt->inode_size, &inode_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table_off, inode_off, &off)) return EXT4_ERR_RANGE;
    memset(out, 0, sizeof(*out));
    usize bytes = mnt->inode_size < sizeof(*out) ? mnt->inode_size : sizeof(*out);
    return read_metadata_bytes(mnt, off, out, bytes);
}

static ext4_status_t map_extent_tree(ext4_mount_t *mnt, const void *node, usize node_bytes, u32 logical, u64 *phys_out, u32 depth) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    u16 node_depth = le16(hdr->eh_depth);
    if (node_bytes < sizeof(ext4_extent_header_t)) return EXT4_ERR_CORRUPT;
    usize capacity = (node_bytes - sizeof(ext4_extent_header_t)) /
                     (node_depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || entries > capacity) return EXT4_ERR_CORRUPT;
    if (node_depth != depth) return EXT4_ERR_CORRUPT;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        for (u16 i = 0; i < entries; ++i) {
            u32 first = le32(ext[i].ee_block);
            u32 len = extent_len_blocks(&ext[i]);
            u32 end = 0;
            if (extent_is_unwritten(&ext[i])) continue;
            if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
            if (logical >= first && logical < end) {
                u64 start = extent_start_block(&ext[i]);
                u64 phys = 0;
                if (!checked_add_u64(start, (u64)(logical - first), &phys)) return EXT4_ERR_RANGE;
                if (phys >= mnt->blocks_count) return EXT4_ERR_RANGE;
                *phys_out = phys;
                return EXT4_OK;
            }
        }
        return EXT4_ERR_RANGE;
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    const ext4_extent_idx_t *best = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) best = &idx[i];
        else break;
    }
    if (!best) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u64 child = le64_from_lo_hi(best->ei_leaf_lo, best->ei_leaf_hi);
    ext4_status_t st = read_block(mnt, child, block);
    if (st == EXT4_OK) st = map_extent_tree(mnt, block, (usize)mnt->block_size, logical, phys_out, depth - 1);
    ext4_tmp_block_free(block);
    return st;
}

static ext4_status_t map_legacy_block(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u32 logical, u64 *phys_out) {
    if (logical < 12) {
        u32 b = le32(inode->i_block[logical]);
        if (!b) return EXT4_ERR_RANGE;
        if ((u64)b >= mnt->blocks_count || ext4_block_is_journal(mnt, b)) return EXT4_ERR_CORRUPT;
        *phys_out = b;
        return EXT4_OK;
    }
    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical < 12 + per_block) {
        u32 indirect_block = le32(inode->i_block[12]);
        if (!indirect_block) return EXT4_ERR_RANGE;
        if ((u64)indirect_block >= mnt->blocks_count || ext4_block_is_journal(mnt, indirect_block)) return EXT4_ERR_CORRUPT;
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, indirect_block, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        u32 *entries = (u32 *)block;
        u32 b = le32(entries[logical - 12]);
        if (!b) { ext4_tmp_block_free(block); return EXT4_ERR_RANGE; }
        if ((u64)b >= mnt->blocks_count || ext4_block_is_journal(mnt, b)) { ext4_tmp_block_free(block); return EXT4_ERR_CORRUPT; }
        *phys_out = b;
        ext4_tmp_block_free(block);
        return EXT4_OK;
    }
    return EXT4_ERR_UNSUPPORTED;
}

static ext4_status_t map_inode_block(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u32 logical, u64 *phys_out) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        u16 depth = le16(hdr->eh_depth);
        if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_CORRUPT;
        return map_extent_tree(mnt, hdr, EXT4_N_BLOCKS * sizeof(u32), logical, phys_out, depth);
    }
    return map_legacy_block(mnt, inode, logical, phys_out);
}

__attribute__((noinline)) ext4_status_t ext4_read_file(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u64 offset, void *buffer, usize bytes, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    u64 size = ext4_inode_size(inode);
    if (offset >= size || bytes == 0) return EXT4_OK;
    if (bytes > size - offset) bytes = (usize)(size - offset);
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *out = (u8 *)buffer;
    usize done = 0;
    ext4_status_t final = EXT4_OK;
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) { final = EXT4_ERR_RANGE; goto out; }
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) { final = EXT4_ERR_RANGE; goto out; }
        u32 logical = (u32)logical64;
        u32 block_off = (u32)(abs % mnt->block_size);
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, inode, logical, &phys);
        if (st == EXT4_ERR_RANGE) {
            memset(out + done, 0, take);
            done += take;
            continue;
        }
        if (st != EXT4_OK) { final = st; goto out; }
        st = read_data_block(mnt, phys, block);
        if (st != EXT4_OK) { final = st; goto out; }
        memcpy(out + done, block + block_off, take);
        done += take;
    }
    if (read_out) *read_out = done;
out:
    ext4_tmp_block_free(block);
    return final;
}


static ext4_status_t write_inode(ext4_mount_t *mnt, u32 ino, const ext4_inode_disk_t *in) {
    if (!mnt || !in || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1;
    u32 group = idx / mnt->inodes_per_group;
    u32 local = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u64 table = group_inode_table(&gd);
    u64 table_off = 0, inode_off = 0, off = 0;
    if (!checked_mul_u64(table, mnt->block_size, &table_off)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)local, mnt->inode_size, &inode_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table_off, inode_off, &off)) return EXT4_ERR_RANGE;
    usize bytes = mnt->inode_size < sizeof(*in) ? mnt->inode_size : sizeof(*in);
    return write_metadata_bytes(mnt, off, in, bytes);
}

static void inode_set_size(ext4_inode_disk_t *inode, u64 size) {
    inode->i_size_lo = (u32)size;
    inode->i_size_high = (u32)(size >> 32);
}

