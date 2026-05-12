static u16 extent_inline_capacity(void) {
    return (u16)((EXT4_N_BLOCKS * sizeof(u32) - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_t));
}

static void ext4_init_inline_extent_tree(ext4_inode_disk_t *inode) {
    memset(inode->i_block, 0, sizeof(inode->i_block));
    inode->i_flags |= EXT4_EXTENTS_FL;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    hdr->eh_magic = EXT4_EXT_MAGIC;
    hdr->eh_entries = 0;
    hdr->eh_max = extent_inline_capacity();
    hdr->eh_depth = 0;
    hdr->eh_generation = 0;
}

static ext4_status_t extent_inline_validate(ext4_inode_disk_t *inode, ext4_extent_header_t **hdr_out, ext4_extent_t **ext_out) {
    if (!inode || !hdr_out || !ext_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 0) return EXT4_ERR_UNSUPPORTED;
    u16 cap = extent_inline_capacity();
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
        if (i && first < prev_end) return EXT4_ERR_CORRUPT;
        if (extent_start_block(&ext[i]) >= ((u64)1 << 48)) return EXT4_ERR_RANGE;
        prev_end = end;
    }
    *hdr_out = hdr;
    *ext_out = ext;
    return EXT4_OK;
}

static u16 extent_root_index_capacity(void) {
    return (u16)((EXT4_N_BLOCKS * sizeof(u32) - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_idx_t));
}

static u16 extent_leaf_capacity(ext4_mount_t *mnt) {
    return (u16)((mnt->block_size - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_t));
}

static u16 extent_index_node_capacity(ext4_mount_t *mnt) {
    return (u16)((mnt->block_size - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_idx_t));
}

static ext4_status_t extent_leaf_validate(ext4_mount_t *mnt, void *block, ext4_extent_header_t **hdr_out, ext4_extent_t **ext_out) {
    if (!mnt || !block || !hdr_out || !ext_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 0) return EXT4_ERR_CORRUPT;
    u16 cap = extent_leaf_capacity(mnt);
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
        if (i && first < prev_end) return EXT4_ERR_CORRUPT;
        if (extent_start_block(&ext[i]) >= ((u64)1 << 48)) return EXT4_ERR_RANGE;
        prev_end = end;
    }
    *hdr_out = hdr;
    *ext_out = ext;
    return EXT4_OK;
}

static ext4_status_t extent_index_validate(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t **hdr_out, ext4_extent_idx_t **idx_out) {
    if (!mnt || !inode || !hdr_out || !idx_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 1u) return EXT4_ERR_UNSUPPORTED;
    u16 cap = extent_root_index_capacity();
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(hdr + 1);
    u32 prev = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 leaf = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (leaf >= mnt->blocks_count) return EXT4_ERR_RANGE;
        if (i && first <= prev) return EXT4_ERR_CORRUPT;
        prev = first;
    }
    *hdr_out = hdr;
    *idx_out = idx;
    return EXT4_OK;
}

static ext4_status_t extent_index_node_validate(ext4_mount_t *mnt, void *block, u16 depth, ext4_extent_header_t **hdr_out, ext4_extent_idx_t **idx_out) {
    if (!mnt || !block || !hdr_out || !idx_out || depth == 0) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    u16 cap = extent_index_node_capacity(mnt);
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(hdr + 1);
    u32 prev = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (child >= mnt->blocks_count) return EXT4_ERR_RANGE;
        if (i && first <= prev) return EXT4_ERR_CORRUPT;
        prev = first;
    }
    *hdr_out = hdr;
    *idx_out = idx;
    return EXT4_OK;
}

