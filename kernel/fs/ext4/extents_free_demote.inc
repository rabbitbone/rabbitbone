static ext4_status_t extent_array_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *hdr, ext4_extent_t *ext, u32 logical, bool *changed_out, ext4_deferred_free_t *deferred) {
    if (changed_out) *changed_out = false;
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = first + len;
        if (logical < first || logical >= end) continue;
        if (logical != first && logical != end - 1u && entries >= le16(hdr->eh_max)) return EXT4_ERR_UNSUPPORTED;
        bool unwritten = extent_is_unwritten(&ext[i]);
        u64 phys = extent_start_block(&ext[i]) + (u64)(logical - first);
        ext4_status_t st = deferred_free_add(deferred, phys);
        if (st != EXT4_OK) return st;
        if (len == 1u) {
            for (u16 j = i; j + 1u < entries; ++j) ext[j] = ext[j + 1u];
            memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
            hdr->eh_entries = entries - 1u;
        } else if (logical == first) {
            ext[i].ee_block = first + 1u;
            extent_set_start_block(&ext[i], extent_start_block(&ext[i]) + 1u);
            extent_set_len_blocks_state(&ext[i], len - 1u, unwritten);
        } else if (logical == end - 1u) {
            extent_set_len_blocks_state(&ext[i], len - 1u, unwritten);
        } else {
            u32 right_len = end - logical - 1u;
            u64 right_start = phys + 1u;
            extent_set_len_blocks_state(&ext[i], logical - first, unwritten);
            for (u16 j = entries; j > i + 1u; --j) ext[j] = ext[j - 1u];
            memset(&ext[i + 1u], 0, sizeof(ext4_extent_t));
            ext[i + 1u].ee_block = logical + 1u;
            extent_set_len_blocks_state(&ext[i + 1u], right_len, unwritten);
            extent_set_start_block(&ext[i + 1u], right_start);
            hdr->eh_entries = entries + 1u;
        }
        inode_add_blocks(mnt, inode, -1);
        if (changed_out) *changed_out = true;
        return EXT4_OK;
    }
    return EXT4_OK;
}

static ext4_status_t extent_inline_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, ext4_deferred_free_t *deferred) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
    return extent_array_free_logical_block(mnt, inode, hdr, ext, logical, 0, deferred);
}

static ext4_status_t extent_collect_inline_candidate(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 depth, ext4_extent_t *tmp, u16 cap, u16 *count, u32 *prev_end) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize actual_cap = (node_bytes - sizeof(*hdr)) / (depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || max_entries > actual_cap) return EXT4_ERR_CORRUPT;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        for (u16 i = 0; i < entries; ++i) {
            u32 first = le32(ext[i].ee_block);
            u32 len = extent_len_blocks(&ext[i]);
            u32 end = 0;
            if (len == 0 || __builtin_add_overflow(first, len, &end) || first < *prev_end) return EXT4_ERR_CORRUPT;
            if (*count >= cap) return EXT4_ERR_UNSUPPORTED;
            tmp[(*count)++] = ext[i];
            *prev_end = end;
        }
        return EXT4_OK;
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < entries; ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = extent_collect_inline_candidate(mnt, block, (usize)mnt->block_size, depth - 1u, tmp, cap, count, prev_end);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static ext4_status_t extent_defer_metadata_tree(ext4_mount_t *mnt, ext4_inode_disk_t *inode, const void *node, usize node_bytes, u16 depth, ext4_deferred_free_t *deferred) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (depth == 0) return EXT4_OK;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < le16(hdr->eh_entries); ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = extent_defer_metadata_tree(mnt, inode, block, (usize)mnt->block_size, depth - 1u, deferred);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
        st = deferred_free_add(deferred, child);
        if (st != EXT4_OK) return st;
        inode_add_blocks(mnt, inode, -1);
    }
    return EXT4_OK;
}

static ext4_status_t extent_try_demote_index_to_inline(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_deferred_free_t *deferred) {
    if (!mnt || !inode) return EXT4_ERR_RANGE;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    u16 depth = le16(root->eh_depth);
    if (depth == 0) return EXT4_OK;
    if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_UNSUPPORTED;
    u16 inline_cap = extent_inline_capacity();
    ext4_extent_t tmp[EXT4_N_BLOCKS];
    memset(tmp, 0, sizeof(tmp));
    u16 total = 0;
    u32 prev_end = 0;
    ext4_status_t st = extent_collect_inline_candidate(mnt, root, EXT4_N_BLOCKS * sizeof(u32), depth, tmp, inline_cap, &total, &prev_end);
    if (st == EXT4_ERR_UNSUPPORTED) return EXT4_OK;
    if (st != EXT4_OK) return st;
    st = extent_defer_metadata_tree(mnt, inode, root, EXT4_N_BLOCKS * sizeof(u32), depth, deferred);
    if (st != EXT4_OK) return st;
    memset(inode->i_block, 0, sizeof(inode->i_block));
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    hdr->eh_magic = EXT4_EXT_MAGIC;
    hdr->eh_entries = total;
    hdr->eh_max = inline_cap;
    hdr->eh_depth = 0;
    hdr->eh_generation = 0;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    for (u16 i = 0; i < total; ++i) ext[i] = tmp[i];
    return EXT4_OK;
}

static ext4_status_t extent_index_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, ext4_deferred_free_t *deferred) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return EXT4_OK;
    bool found = false;
    u16 chosen = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) { chosen = i; found = true; }
        else break;
    }
    if (!found) return EXT4_OK;
    u64 leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    st = read_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *leaf_hdr = 0;
    ext4_extent_t *leaf_ext = 0;
    st = extent_leaf_validate(mnt, leaf, &leaf_hdr, &leaf_ext);
    if (st != EXT4_OK) { result = st; goto out; }
    bool changed = false;
    st = extent_array_free_logical_block(mnt, inode, leaf_hdr, leaf_ext, logical, &changed, deferred);
    if (st != EXT4_OK || !changed) { result = st; goto out; }
    if (le16(leaf_hdr->eh_entries) == 0) {
        st = deferred_free_add(deferred, leaf_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = chosen; i + 1u < entries; ++i) idx[i] = idx[i + 1u];
        memset(&idx[entries - 1u], 0, sizeof(ext4_extent_idx_t));
        root->eh_entries = entries - 1u;
        result = extent_try_demote_index_to_inline(mnt, inode, deferred);
        goto out;
    }
    idx[chosen].ei_block = le32(leaf_ext[0].ee_block);
    st = write_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    result = extent_try_demote_index_to_inline(mnt, inode, deferred);
out:
    ext4_tmp_block_free(leaf);
    return result;
}

static ext4_status_t extent_depth2_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, ext4_deferred_free_t *deferred) {
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC || le16(root->eh_depth) != 2u) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *root_idx = (ext4_extent_idx_t *)(root + 1);
    u16 root_entries = le16(root->eh_entries);
    if (root_entries == 0) return EXT4_OK;
    bool found_root = false;
    u16 ri = 0;
    for (u16 i = 0; i < root_entries; ++i) {
        if (logical >= le32(root_idx[i].ei_block)) { ri = i; found_root = true; }
        else break;
    }
    if (!found_root) return EXT4_OK;
    u8 *internal = ext4_tmp_block(mnt);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!internal || !leaf) {
        ext4_tmp_block_free(leaf);
        ext4_tmp_block_free(internal);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_OK;
    u64 internal_block = le64_from_lo_hi(root_idx[ri].ei_leaf_lo, root_idx[ri].ei_leaf_hi);
    ext4_status_t st = read_block(mnt, internal_block, internal);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *ih = 0;
    ext4_extent_idx_t *ii = 0;
    st = extent_index_node_validate(mnt, internal, 1u, &ih, &ii);
    if (st != EXT4_OK) { result = st; goto out; }
    u16 internal_entries = le16(ih->eh_entries);
    bool found_leaf = false;
    u16 li = 0;
    for (u16 i = 0; i < internal_entries; ++i) {
        if (logical >= le32(ii[i].ei_block)) { li = i; found_leaf = true; }
        else break;
    }
    if (!found_leaf) goto out;
    u64 leaf_block = le64_from_lo_hi(ii[li].ei_leaf_lo, ii[li].ei_leaf_hi);
    st = read_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *lh = 0;
    ext4_extent_t *le = 0;
    st = extent_leaf_validate(mnt, leaf, &lh, &le);
    if (st != EXT4_OK) { result = st; goto out; }
    bool changed = false;
    st = extent_array_free_logical_block(mnt, inode, lh, le, logical, &changed, deferred);
    if (st != EXT4_OK || !changed) { result = st; goto out; }
    if (le16(lh->eh_entries) == 0) {
        st = deferred_free_add(deferred, leaf_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = li; i + 1u < internal_entries; ++i) ii[i] = ii[i + 1u];
        memset(&ii[internal_entries - 1u], 0, sizeof(ext4_extent_idx_t));
        ih->eh_entries = internal_entries - 1u;
    } else {
        ii[li].ei_block = le32(le[0].ee_block);
        st = write_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
    }
    if (le16(ih->eh_entries) == 0) {
        st = deferred_free_add(deferred, internal_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = ri; i + 1u < root_entries; ++i) root_idx[i] = root_idx[i + 1u];
        memset(&root_idx[root_entries - 1u], 0, sizeof(ext4_extent_idx_t));
        root->eh_entries = root_entries - 1u;
    } else {
        root_idx[ri].ei_block = le32(ii[0].ei_block);
        st = write_block(mnt, internal_block, internal);
        if (st != EXT4_OK) { result = st; goto out; }
    }
    result = extent_try_demote_index_to_inline(mnt, inode, deferred);
out:
    ext4_tmp_block_free(leaf);
    ext4_tmp_block_free(internal);
    return result;
}

