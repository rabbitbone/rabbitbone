static ext4_status_t inspect_extent_array(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext, ext4_extent_report_t *report) {
    if (!mnt || !hdr || !ext || !report) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    if (entries > max_entries) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        bool unwritten = extent_is_unwritten(&ext[i]);
        if (len == 0 || __builtin_add_overflow(first, len, &end) || (i && first < prev_end)) {
            ++report->errors;
            return EXT4_ERR_CORRUPT;
        }
        u64 start = extent_start_block(&ext[i]);
        u64 end64 = 0;
        if (!checked_add_u64(start, len, &end64) || start >= mnt->blocks_count || len > mnt->blocks_count || end64 > mnt->blocks_count) {
            ++report->errors;
            return EXT4_ERR_RANGE;
        }
        ++report->extent_entries;
        report->data_blocks += len;
        if (unwritten) {
            ++report->unwritten_extents;
            report->unwritten_blocks += len;
        }
        prev_end = end;
    }
    return EXT4_OK;
}

static ext4_status_t inspect_extent_node(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 expected_depth, ext4_extent_report_t *report, u32 *first_out) {
    if (!mnt || !node || !report) return EXT4_ERR_RANGE;
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u16 depth = le16(hdr->eh_depth);
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize cap = (node_bytes - sizeof(*hdr)) / (depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (depth != expected_depth || depth > EXT4_MAX_EXTENT_DEPTH || entries > max_entries || max_entries > cap) { ++report->errors; return EXT4_ERR_CORRUPT; }
    if (depth > report->max_depth_seen) report->max_depth_seen = depth;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        if (entries && first_out) *first_out = le32(ext[0].ee_block);
        return inspect_extent_array(mnt, hdr, ext, report);
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    u32 prev_first = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (i && first <= prev_first) { ++report->errors; return EXT4_ERR_CORRUPT; }
        if (child >= mnt->blocks_count || ext4_block_is_journal(mnt, child)) { ++report->errors; return EXT4_ERR_RANGE; }
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        ++report->metadata_blocks;
        if (depth == 1) ++report->leaf_nodes;
        else ++report->index_nodes;
        u32 child_first = 0;
        st = inspect_extent_node(mnt, block, (usize)mnt->block_size, depth - 1u, report, &child_first);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
        if (entries && first != child_first) { ++report->errors; return EXT4_ERR_CORRUPT; }
        if (i == 0 && first_out) *first_out = first;
        prev_first = first;
    }
    return EXT4_OK;
}

ext4_status_t ext4_inspect_inode_extents(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_extent_report_t *report) {
    if (!mnt || !inode || !report) return EXT4_ERR_RANGE;
    memset(report, 0, sizeof(*report));
    report->depth = 0xffffu;
    if (!ext4_inode_uses_extents(inode)) return EXT4_OK;
    report->uses_extents = true;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC) { ++report->errors; return EXT4_ERR_CORRUPT; }
    report->depth = le16(root->eh_depth);
    report->root_entries = le16(root->eh_entries);
    report->root_capacity = le16(root->eh_max);
    if (report->root_entries > report->root_capacity) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u32 first = 0;
    return inspect_extent_node(mnt, root, EXT4_N_BLOCKS * sizeof(u32), report->depth, report, &first);
}

const char *ext4_status_name(ext4_status_t status) {
    switch (status) {
        case EXT4_OK: return "ok";
        case EXT4_ERR_IO: return "io";
        case EXT4_ERR_BAD_MAGIC: return "bad-magic";
        case EXT4_ERR_UNSUPPORTED: return "unsupported";
        case EXT4_ERR_RANGE: return "range";
        case EXT4_ERR_CORRUPT: return "corrupt";
        case EXT4_ERR_NOT_FOUND: return "not-found";
        case EXT4_ERR_NOT_DIR: return "not-dir";
        case EXT4_ERR_NO_MEMORY: return "no-memory";
        case EXT4_ERR_EXIST: return "exists";
        case EXT4_ERR_NOT_EMPTY: return "not-empty";
        default: return "unknown";
    }
}

static ext4_status_t block_bitmap_is_allocated(ext4_mount_t *mnt, u64 block, bool *allocated_out) {
    if (!mnt || !allocated_out || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    if (group >= mnt->group_count) return EXT4_ERR_RANGE;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st == EXT4_OK) *allocated_out = bitmap_get(bm, bit);
    ext4_tmp_block_free(bm);
    return st;
}

static ext4_status_t validate_extent_array_allocations(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext, ext4_fsck_report_t *report) {
    if (!mnt || !hdr || !ext || !report) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u64 start = extent_start_block(&ext[i]);
        u32 len = extent_len_blocks(&ext[i]);
        u64 end64 = 0;
        if (!checked_add_u64(start, len, &end64) || end64 > mnt->blocks_count) return EXT4_ERR_RANGE;
        for (u32 j = 0; j < len; ++j) {
            u64 data_block = 0;
            if (!checked_add_u64(start, j, &data_block)) return EXT4_ERR_RANGE;
            bool allocated = false;
            ext4_status_t st = block_bitmap_is_allocated(mnt, data_block, &allocated);
            if (st != EXT4_OK) return st;
            if (!allocated) { ++report->errors; return EXT4_ERR_CORRUPT; }
            ++report->extent_data_blocks;
        }
    }
    return EXT4_OK;
}

static ext4_status_t validate_extent_node_allocations(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 expected_depth, ext4_fsck_report_t *report) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != expected_depth) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize cap = (node_bytes - sizeof(*hdr)) / (expected_depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || max_entries > cap) { ++report->errors; return EXT4_ERR_CORRUPT; }
    if (expected_depth == 0) return validate_extent_array_allocations(mnt, hdr, (const ext4_extent_t *)(hdr + 1), report);
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < entries; ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        bool allocated = false;
        ext4_status_t st = block_bitmap_is_allocated(mnt, child, &allocated);
        if (st != EXT4_OK) return st;
        if (!allocated) { ++report->errors; return EXT4_ERR_CORRUPT; }
        ++report->extent_metadata_blocks;
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = validate_extent_node_allocations(mnt, block, (usize)mnt->block_size, expected_depth - 1u, report);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static ext4_status_t validate_inode_extent_allocations(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_fsck_report_t *report) {
    if (!mnt || !inode || !report || !ext4_inode_uses_extents(inode)) return EXT4_OK;
    ext4_extent_report_t er;
    ext4_status_t st = ext4_inspect_inode_extents(mnt, inode, &er);
    if (st != EXT4_OK) { ++report->errors; return st; }
    if (er.errors) { report->errors += er.errors; return EXT4_ERR_CORRUPT; }
    ++report->extent_inodes;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    return validate_extent_node_allocations(mnt, root, EXT4_N_BLOCKS * sizeof(u32), le16(root->eh_depth), report);
}
