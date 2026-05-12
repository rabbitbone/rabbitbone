static u32 g_ext4_extent_mutation_ino;

static u32 ext4_extent_mutation_enter(u32 ino) {
    u32 old = g_ext4_extent_mutation_ino;
    if (ino) g_ext4_extent_mutation_ino = ino;
    return old;
}

static void ext4_extent_mutation_leave(u32 old) {
    g_ext4_extent_mutation_ino = old;
}

static ext4_status_t extent_promote_inline_to_index(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_t *old_ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &old_hdr, &old_ext);
    if (st != EXT4_OK) return st;
    u16 entries = le16(old_hdr->eh_entries);
    u64 leaf_block = 0;
    st = alloc_block(mnt, &leaf_block);
    if (st != EXT4_OK) return st;

    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) { (void)free_block(mnt, leaf_block); return EXT4_ERR_NO_MEMORY; }
    memset(leaf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *leaf_hdr = (ext4_extent_header_t *)leaf;
    leaf_hdr->eh_magic = EXT4_EXT_MAGIC;
    leaf_hdr->eh_entries = entries;
    leaf_hdr->eh_max = extent_leaf_capacity(mnt);
    leaf_hdr->eh_depth = 0;
    leaf_hdr->eh_generation = 0;
    if (entries > le16(leaf_hdr->eh_max)) {
        ext4_tmp_block_free(leaf);
        (void)free_block(mnt, leaf_block);
        return EXT4_ERR_CORRUPT;
    }
    ext4_extent_t *leaf_ext = (ext4_extent_t *)(leaf_hdr + 1);
    for (u16 i = 0; i < entries; ++i) leaf_ext[i] = old_ext[i];
    st = write_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) {
        ext4_tmp_block_free(leaf);
        (void)free_block(mnt, leaf_block);
        return st;
    }
    u32 first_logical = entries ? le32(leaf_ext[0].ee_block) : 0u;
    ext4_tmp_block_free(leaf);

    memset(inode->i_block, 0, sizeof(inode->i_block));
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    root->eh_magic = EXT4_EXT_MAGIC;
    root->eh_entries = 1u;
    root->eh_max = extent_root_index_capacity();
    root->eh_depth = 1u;
    root->eh_generation = 0;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(root + 1);
    idx[0].ei_block = first_logical;
    idx[0].ei_leaf_lo = (u32)leaf_block;
    idx[0].ei_leaf_hi = (u16)(leaf_block >> 32);
    idx[0].ei_unused = 0;
    inode_add_blocks(mnt, inode, 1);
    return EXT4_OK;
}

static void extent_idx_set_leaf(ext4_extent_idx_t *idx, u32 first_logical, u64 leaf_block) {
    idx->ei_block = first_logical;
    idx->ei_leaf_lo = (u32)leaf_block;
    idx->ei_leaf_hi = (u16)(leaf_block >> 32);
    idx->ei_unused = 0;
}

static ext4_status_t extent_index_choose_leaf(ext4_extent_header_t *root, ext4_extent_idx_t *idx, u32 logical, bool allocate, u16 *chosen_out) {
    if (!root || !idx || !chosen_out) return EXT4_ERR_RANGE;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return allocate ? EXT4_ERR_UNSUPPORTED : EXT4_ERR_RANGE;
    bool found = false;
    u16 chosen = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) { chosen = i; found = true; }
        else break;
    }
    if (!found) {
        if (!allocate) return EXT4_ERR_RANGE;
        chosen = 0;
    }
    *chosen_out = chosen;
    return EXT4_OK;
}

static ext4_status_t extent_index_split_leaf(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *root, ext4_extent_idx_t *idx, u16 chosen, u8 *leaf) {
    if (!mnt || !root || !idx || !leaf) return EXT4_ERR_RANGE;
    u16 root_entries = le16(root->eh_entries);
    u16 root_max = le16(root->eh_max);
    if (chosen >= root_entries) return EXT4_ERR_RANGE;
    if (root_entries >= root_max) return EXT4_ERR_UNSUPPORTED;

    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_t *old_ext = 0;
    ext4_status_t st = extent_leaf_validate(mnt, leaf, &old_hdr, &old_ext);
    if (st != EXT4_OK) return st;
    u16 old_entries = le16(old_hdr->eh_entries);
    if (old_entries < 2u) return EXT4_ERR_CORRUPT;

    u16 split = (u16)(old_entries / 2u);
    if (split == 0 || split >= old_entries) return EXT4_ERR_CORRUPT;

    u8 *old_copy = ext4_tmp_block(mnt);
    if (!old_copy) return EXT4_ERR_NO_MEMORY;
    memcpy(old_copy, leaf, (usize)mnt->block_size);
    u32 old_i_block[EXT4_N_BLOCKS];
    memcpy(old_i_block, inode->i_block, sizeof(old_i_block));
    u32 old_i_blocks_lo = inode->i_blocks_lo;

    u64 new_leaf_block = 0;
    st = alloc_block(mnt, &new_leaf_block);
    if (st != EXT4_OK) { ext4_tmp_block_free(old_copy); return st; }

    u8 *new_leaf = ext4_tmp_block(mnt);
    if (!new_leaf) { ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_leaf_block); return EXT4_ERR_NO_MEMORY; }
    memset(new_leaf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *new_hdr = (ext4_extent_header_t *)new_leaf;
    ext4_extent_t *new_ext = (ext4_extent_t *)(new_hdr + 1);
    new_hdr->eh_magic = EXT4_EXT_MAGIC;
    new_hdr->eh_entries = (u16)(old_entries - split);
    new_hdr->eh_max = extent_leaf_capacity(mnt);
    new_hdr->eh_depth = 0;
    new_hdr->eh_generation = 0;
    for (u16 i = 0; i < new_hdr->eh_entries; ++i) new_ext[i] = old_ext[split + i];
    memset(&old_ext[split], 0, (usize)(old_entries - split) * sizeof(ext4_extent_t));
    old_hdr->eh_entries = split;

    st = extent_leaf_validate(mnt, leaf, &old_hdr, &old_ext);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_leaf_block); return st; }
    st = extent_leaf_validate(mnt, new_leaf, &new_hdr, &new_ext);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_leaf_block); return st; }

    u64 old_leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
    st = write_block(mnt, new_leaf_block, new_leaf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_leaf_block); return st; }
    st = write_block(mnt, old_leaf_block, leaf);
    if (st != EXT4_OK) {
        (void)write_block(mnt, old_leaf_block, old_copy);
        ext4_tmp_block_free(new_leaf);
        ext4_tmp_block_free(old_copy);
        (void)free_block(mnt, new_leaf_block);
        return st;
    }

    for (u16 i = root_entries; i > chosen + 1u; --i) idx[i] = idx[i - 1u];
    extent_idx_set_leaf(&idx[chosen], le32(old_ext[0].ee_block), old_leaf_block);
    extent_idx_set_leaf(&idx[chosen + 1u], le32(new_ext[0].ee_block), new_leaf_block);
    root->eh_entries = root_entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    if (g_ext4_extent_mutation_ino != 0) {
        st = write_inode(mnt, g_ext4_extent_mutation_ino, inode);
        if (st != EXT4_OK) {
            memcpy(inode->i_block, old_i_block, sizeof(old_i_block));
            inode->i_blocks_lo = old_i_blocks_lo;
            (void)write_block(mnt, old_leaf_block, old_copy);
            (void)free_block(mnt, new_leaf_block);
            ext4_tmp_block_free(new_leaf);
            ext4_tmp_block_free(old_copy);
            return st;
        }
    }
    ext4_tmp_block_free(new_leaf);
    ext4_tmp_block_free(old_copy);
    return EXT4_OK;
}

static ext4_status_t extent_promote_index_to_depth2(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return EXT4_ERR_CORRUPT;
    u64 internal_block = 0;
    st = alloc_block(mnt, &internal_block);
    if (st != EXT4_OK) return st;

    u8 *internal = ext4_tmp_block(mnt);
    if (!internal) { (void)free_block(mnt, internal_block); return EXT4_ERR_NO_MEMORY; }
    memset(internal, 0, (usize)mnt->block_size);
    ext4_extent_header_t *ih = (ext4_extent_header_t *)internal;
    ext4_extent_idx_t *ii = (ext4_extent_idx_t *)(ih + 1);
    ih->eh_magic = EXT4_EXT_MAGIC;
    ih->eh_entries = entries;
    ih->eh_max = extent_index_node_capacity(mnt);
    ih->eh_depth = 1u;
    ih->eh_generation = 0;
    if (entries > le16(ih->eh_max)) { ext4_tmp_block_free(internal); (void)free_block(mnt, internal_block); return EXT4_ERR_CORRUPT; }
    for (u16 i = 0; i < entries; ++i) ii[i] = idx[i];
    st = write_block(mnt, internal_block, internal);
    if (st != EXT4_OK) { ext4_tmp_block_free(internal); (void)free_block(mnt, internal_block); return st; }
    u32 first = le32(ii[0].ei_block);
    ext4_tmp_block_free(internal);

    u32 old_i_block[EXT4_N_BLOCKS];
    memcpy(old_i_block, inode->i_block, sizeof(old_i_block));
    u32 old_i_blocks_lo = inode->i_blocks_lo;
    memset(inode->i_block, 0, sizeof(inode->i_block));
    root = (ext4_extent_header_t *)inode->i_block;
    root->eh_magic = EXT4_EXT_MAGIC;
    root->eh_entries = 1u;
    root->eh_max = extent_root_index_capacity();
    root->eh_depth = 2u;
    root->eh_generation = 0;
    idx = (ext4_extent_idx_t *)(root + 1);
    extent_idx_set_leaf(&idx[0], first, internal_block);
    inode_add_blocks(mnt, inode, 1);
    if (g_ext4_extent_mutation_ino != 0) {
        st = write_inode(mnt, g_ext4_extent_mutation_ino, inode);
        if (st != EXT4_OK) {
            memcpy(inode->i_block, old_i_block, sizeof(old_i_block));
            inode->i_blocks_lo = old_i_blocks_lo;
            (void)free_block(mnt, internal_block);
            return st;
        }
    }
    return EXT4_OK;
}

static ext4_status_t extent_depth2_split_internal(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *root, ext4_extent_idx_t *root_idx, u16 chosen, u64 old_block, u8 *old_buf) {
    u16 root_entries = le16(root->eh_entries);
    if (chosen >= root_entries) return EXT4_ERR_RANGE;
    if (root_entries >= le16(root->eh_max)) return EXT4_ERR_UNSUPPORTED;
    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_idx_t *old_idx = 0;
    ext4_status_t st = extent_index_node_validate(mnt, old_buf, 1u, &old_hdr, &old_idx);
    if (st != EXT4_OK) return st;
    u16 old_entries = le16(old_hdr->eh_entries);
    if (old_entries < 2u) return EXT4_ERR_CORRUPT;
    u8 *old_copy = ext4_tmp_block(mnt);
    if (!old_copy) return EXT4_ERR_NO_MEMORY;
    memcpy(old_copy, old_buf, (usize)mnt->block_size);
    u32 old_i_block[EXT4_N_BLOCKS];
    memcpy(old_i_block, inode->i_block, sizeof(old_i_block));
    u32 old_i_blocks_lo = inode->i_blocks_lo;
    u16 split = (u16)(old_entries / 2u);
    u64 new_block = 0;
    st = alloc_block(mnt, &new_block);
    if (st != EXT4_OK) { ext4_tmp_block_free(old_copy); return st; }

    u8 *new_buf = ext4_tmp_block(mnt);
    if (!new_buf) { ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_block); return EXT4_ERR_NO_MEMORY; }
    memset(new_buf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *nh = (ext4_extent_header_t *)new_buf;
    ext4_extent_idx_t *ni = (ext4_extent_idx_t *)(nh + 1);
    nh->eh_magic = EXT4_EXT_MAGIC;
    nh->eh_entries = (u16)(old_entries - split);
    nh->eh_max = extent_index_node_capacity(mnt);
    nh->eh_depth = 1u;
    nh->eh_generation = 0;
    for (u16 i = 0; i < nh->eh_entries; ++i) ni[i] = old_idx[split + i];
    memset(&old_idx[split], 0, (usize)(old_entries - split) * sizeof(ext4_extent_idx_t));
    old_hdr->eh_entries = split;
    st = write_block(mnt, new_block, new_buf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_buf); ext4_tmp_block_free(old_copy); (void)free_block(mnt, new_block); return st; }
    st = write_block(mnt, old_block, old_buf);
    if (st != EXT4_OK) {
        (void)write_block(mnt, old_block, old_copy);
        ext4_tmp_block_free(new_buf);
        ext4_tmp_block_free(old_copy);
        (void)free_block(mnt, new_block);
        return st;
    }
    for (u16 i = root_entries; i > chosen + 1u; --i) root_idx[i] = root_idx[i - 1u];
    extent_idx_set_leaf(&root_idx[chosen], le32(old_idx[0].ei_block), old_block);
    extent_idx_set_leaf(&root_idx[chosen + 1u], le32(ni[0].ei_block), new_block);
    root->eh_entries = root_entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    if (g_ext4_extent_mutation_ino != 0) {
        st = write_inode(mnt, g_ext4_extent_mutation_ino, inode);
        if (st != EXT4_OK) {
            memcpy(inode->i_block, old_i_block, sizeof(old_i_block));
            inode->i_blocks_lo = old_i_blocks_lo;
            (void)write_block(mnt, old_block, old_copy);
            (void)free_block(mnt, new_block);
            ext4_tmp_block_free(new_buf);
            ext4_tmp_block_free(old_copy);
            return st;
        }
    }
    ext4_tmp_block_free(new_buf);
    ext4_tmp_block_free(old_copy);
    return EXT4_OK;
}

static ext4_status_t extent_depth2_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC || le16(root->eh_depth) != 2u) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *root_idx = (ext4_extent_idx_t *)(root + 1);
    u8 *internal = ext4_tmp_block(mnt);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!internal || !leaf) {
        ext4_tmp_block_free(leaf);
        ext4_tmp_block_free(internal);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_ERR_UNSUPPORTED;
    for (u8 pass = 0; pass < 6u; ++pass) {
        u16 internal_chosen = 0;
        ext4_status_t st = extent_index_choose_leaf(root, root_idx, logical, allocate, &internal_chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 internal_block = le64_from_lo_hi(root_idx[internal_chosen].ei_leaf_lo, root_idx[internal_chosen].ei_leaf_hi);
        st = read_block(mnt, internal_block, internal);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *ih = 0;
        ext4_extent_idx_t *ii = 0;
        st = extent_index_node_validate(mnt, internal, 1u, &ih, &ii);
        if (st != EXT4_OK) { result = st; goto out; }
        u16 leaf_chosen = 0;
        st = extent_index_choose_leaf(ih, ii, logical, allocate, &leaf_chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 leaf_block = le64_from_lo_hi(ii[leaf_chosen].ei_leaf_lo, ii[leaf_chosen].ei_leaf_hi);
        st = read_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *lh = 0;
        ext4_extent_t *le = 0;
        st = extent_leaf_validate(mnt, leaf, &lh, &le);
        if (st != EXT4_OK) { result = st; goto out; }
        st = extent_array_block_for_write(mnt, inode, lh, le, logical, phys_out, allocate, zero_new, create_unwritten);
        if (st == EXT4_OK) {
            if (le16(lh->eh_entries)) ii[leaf_chosen].ei_block = le32(le[0].ee_block);
            if (le16(ih->eh_entries)) root_idx[internal_chosen].ei_block = le32(ii[0].ei_block);
            st = write_block(mnt, leaf_block, leaf);
            if (st != EXT4_OK) { result = st; goto out; }
            result = write_block(mnt, internal_block, internal);
            goto out;
        }
        if (st != EXT4_ERR_UNSUPPORTED || !allocate) { result = st; goto out; }
        if (le16(lh->eh_entries) >= le16(lh->eh_max)) {
            st = extent_index_split_leaf(mnt, inode, ih, ii, leaf_chosen, leaf);
            if (st == EXT4_OK) {
                if (le16(ih->eh_entries)) root_idx[internal_chosen].ei_block = le32(ii[0].ei_block);
                st = write_block(mnt, internal_block, internal);
                if (st != EXT4_OK) { result = st; goto out; }
                continue;
            }
            if (st != EXT4_ERR_UNSUPPORTED) { result = st; goto out; }
            st = extent_depth2_split_internal(mnt, inode, root, root_idx, internal_chosen, internal_block, internal);
            if (st != EXT4_OK) { result = st; goto out; }
            continue;
        }
        result = st;
        goto out;
    }
out:
    ext4_tmp_block_free(leaf);
    ext4_tmp_block_free(internal);
    return result;
}

