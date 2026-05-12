static ext4_status_t free_extent_array_blocks(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext) {
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u64 start = extent_start_block(&ext[i]);
        u32 len = extent_len_blocks(&ext[i]);
        for (u32 j = 0; j < len; ++j) {
            ext4_status_t st = free_block(mnt, start + j);
            if (st != EXT4_OK) return st;
        }
    }
    return EXT4_OK;
}

static ext4_status_t free_extent_tree_blocks(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 depth) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    if (depth == 0) return free_extent_array_blocks(mnt, hdr, (const ext4_extent_t *)(hdr + 1));
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u16 i = 0; i < le16(hdr->eh_entries); ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { result = st; goto out; }
        st = free_extent_tree_blocks(mnt, block, (usize)mnt->block_size, depth - 1u);
        if (st != EXT4_OK) { result = st; goto out; }
        st = free_block(mnt, child);
        if (st != EXT4_OK) { result = st; goto out; }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t free_inode_blocks(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    if (ext4_inode_is_symlink(inode) && ext4_inode_size(inode) <= sizeof(inode->i_block) && le32(inode->i_blocks_lo) == 0) {
        memset(inode->i_block, 0, sizeof(inode->i_block));
        inode_set_size(inode, 0);
        inode->i_flags = 0;
        return EXT4_OK;
    }
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *root_hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(root_hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        ext4_status_t st = free_extent_tree_blocks(mnt, root_hdr, EXT4_N_BLOCKS * sizeof(u32), le16(root_hdr->eh_depth));
        if (st != EXT4_OK) return st;
        memset(inode->i_block, 0, sizeof(inode->i_block));
        inode_set_size(inode, 0);
        inode->i_blocks_lo = 0;
        if (ext4_inode_is_dir(inode) && (le32(inode->i_flags) & EXT4_INDEX_FL) != 0 && inode->i_file_acl_lo) { st = free_block(mnt, le32(inode->i_file_acl_lo)); if (st != EXT4_OK) return st; inode->i_file_acl_lo = 0; }
        inode->i_flags &= ~EXT4_INDEX_FL;
        return EXT4_OK;
    }
    for (u32 i = 0; i < 12u; ++i) {
        u32 b = le32(inode->i_block[i]);
        if (b) { ext4_status_t st = free_block(mnt, b); if (st != EXT4_OK) return st; inode->i_block[i] = 0; }
    }
    u32 indirect = le32(inode->i_block[12]);
    if (indirect) {
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, indirect, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        u32 *entries = (u32 *)block;
        u32 per = (u32)(mnt->block_size / 4u);
        for (u32 i = 0; i < per; ++i) {
            u32 b = le32(entries[i]);
            if (b) { st = free_block(mnt, b); if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; } }
        }
        ext4_tmp_block_free(block);
        st = free_block(mnt, indirect);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = 0;
    }
    if (ext4_inode_is_dir(inode) && (le32(inode->i_flags) & EXT4_INDEX_FL) != 0 && inode->i_file_acl_lo) { ext4_status_t st = free_block(mnt, le32(inode->i_file_acl_lo)); if (st != EXT4_OK) return st; inode->i_file_acl_lo = 0; }
    inode->i_flags &= ~EXT4_INDEX_FL;
    inode_set_size(inode, 0);
    inode->i_blocks_lo = 0;
    return EXT4_OK;
}

static ext4_status_t orphan_push(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    inode->i_dtime = mnt->sb.s_last_orphan;
    ext4_status_t st = write_inode(mnt, ino, inode);
    if (st != EXT4_OK) return st;
    mnt->sb.s_last_orphan = ino;
    return write_super(mnt);
}

static ext4_status_t orphan_remove(ext4_mount_t *mnt, u32 ino) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    u32 cur = le32(mnt->sb.s_last_orphan);
    u32 prev = 0;
    u32 guard = 0;
    while (cur) {
        if (++guard > mnt->inodes_count) return EXT4_ERR_CORRUPT;
        ext4_inode_disk_t inode;
        ext4_status_t st = ext4_read_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        u32 next = le32(inode.i_dtime);
        if (cur == ino) {
            if (prev == 0) {
                mnt->sb.s_last_orphan = next;
                st = write_super(mnt);
                if (st != EXT4_OK) return st;
            } else {
                ext4_inode_disk_t pinode;
                st = ext4_read_inode(mnt, prev, &pinode);
                if (st != EXT4_OK) return st;
                pinode.i_dtime = next;
                st = write_inode(mnt, prev, &pinode);
                if (st != EXT4_OK) return st;
            }
            inode.i_dtime = 0;
            return write_inode(mnt, cur, &inode);
        }
        prev = cur;
        cur = next;
    }
    return EXT4_OK;
}

static ext4_status_t ext4_recover_orphans(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    u32 cur = le32(mnt->sb.s_last_orphan);
    u32 guard = 0;
    while (cur) {
        if (++guard > mnt->inodes_count) return EXT4_ERR_CORRUPT;
        ext4_inode_disk_t inode;
        ext4_status_t st = ext4_read_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        u32 next = le32(inode.i_dtime);
        bool is_dir = ext4_inode_is_dir(&inode);
        st = free_inode_blocks(mnt, &inode);
        if (st != EXT4_OK) return st;
        memset(&inode, 0, sizeof(inode));
        st = write_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        st = free_inode(mnt, cur, is_dir);
        if (st != EXT4_OK && st != EXT4_ERR_CORRUPT) return st;
        ++mnt->recovered_orphans;
        if (report) ++report->orphan_recovered;
        cur = next;
    }
    if (mnt->sb.s_last_orphan) {
        mnt->sb.s_last_orphan = 0;
        return write_super(mnt);
    }
    return EXT4_OK;
}

