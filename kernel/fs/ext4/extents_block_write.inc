static ext4_status_t extent_index_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;

    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_UNSUPPORTED;
    for (u8 attempt = 0; attempt < 3u; ++attempt) {
        u16 chosen = 0;
        st = extent_index_choose_leaf(root, idx, logical, allocate, &chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
        st = read_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *leaf_hdr = 0;
        ext4_extent_t *leaf_ext = 0;
        st = extent_leaf_validate(mnt, leaf, &leaf_hdr, &leaf_ext);
        if (st != EXT4_OK) { result = st; goto out; }
        st = extent_array_block_for_write(mnt, inode, leaf_hdr, leaf_ext, logical, phys_out, allocate, zero_new, create_unwritten);
        if (st == EXT4_OK) {
            if (le16(leaf_hdr->eh_entries) > 0) idx[chosen].ei_block = le32(leaf_ext[0].ee_block);
            result = write_block(mnt, leaf_block, leaf);
            goto out;
        }
        if (st != EXT4_ERR_UNSUPPORTED || !allocate) { result = st; goto out; }
        if (le16(leaf_hdr->eh_entries) < le16(leaf_hdr->eh_max)) { result = st; goto out; }
        st = extent_index_split_leaf(mnt, inode, root, idx, chosen, leaf);
        if (st == EXT4_ERR_UNSUPPORTED) {
            st = extent_promote_index_to_depth2(mnt, inode);
            if (st != EXT4_OK) { result = st; goto out; }
            ext4_tmp_block_free(leaf);
            return extent_depth2_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        }
        if (st != EXT4_OK) { result = st; goto out; }
    }
out:
    ext4_tmp_block_free(leaf);
    return result;
}

static ext4_status_t extent_inline_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
    st = extent_array_block_for_write(mnt, inode, hdr, ext, logical, phys_out, allocate, zero_new, create_unwritten);
    if (st != EXT4_ERR_UNSUPPORTED || !allocate) return st;
    st = extent_promote_inline_to_index(mnt, inode);
    if (st != EXT4_OK) return st;
    return extent_index_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
}

