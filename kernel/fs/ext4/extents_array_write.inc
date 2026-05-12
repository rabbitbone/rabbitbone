static bool extent_pair_can_merge(const ext4_extent_t *a, const ext4_extent_t *b) {
    if (!a || !b) return false;
    u32 a_first = le32(a->ee_block);
    u32 a_len = extent_len_blocks(a);
    u32 b_first = le32(b->ee_block);
    u32 b_len = extent_len_blocks(b);
    u64 a_start = extent_start_block(a);
    u64 b_start = extent_start_block(b);
    return a_len != 0 && b_len != 0 && a_len + b_len <= 32768u &&
           extent_is_unwritten(a) == extent_is_unwritten(b) &&
           a_first + a_len == b_first && a_start + a_len == b_start;
}

static void extent_array_merge_neighbors(ext4_extent_header_t *hdr, ext4_extent_t *ext, u16 pos) {
    u16 entries = le16(hdr->eh_entries);
    if (entries == 0) return;
    if (pos >= entries) pos = entries - 1u;
    if (pos > 0 && extent_pair_can_merge(&ext[pos - 1u], &ext[pos])) {
        u32 left_len = extent_len_blocks(&ext[pos - 1u]);
        u32 right_len = extent_len_blocks(&ext[pos]);
        bool unwritten = extent_is_unwritten(&ext[pos - 1u]);
        extent_set_len_blocks_state(&ext[pos - 1u], left_len + right_len, unwritten);
        for (u16 i = pos; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
        memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
        hdr->eh_entries = --entries;
        --pos;
    }
    if (pos + 1u < entries && extent_pair_can_merge(&ext[pos], &ext[pos + 1u])) {
        u32 left_len = extent_len_blocks(&ext[pos]);
        u32 right_len = extent_len_blocks(&ext[pos + 1u]);
        bool unwritten = extent_is_unwritten(&ext[pos]);
        extent_set_len_blocks_state(&ext[pos], left_len + right_len, unwritten);
        for (u16 i = pos + 1u; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
        memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
        hdr->eh_entries = entries - 1u;
    }
}

static ext4_status_t extent_array_convert_unwritten_block(ext4_mount_t *mnt, ext4_extent_header_t *hdr, ext4_extent_t *ext, u16 pos, u32 logical, u64 *phys_out) {
    if (!mnt || !hdr || !ext || !phys_out) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    if (pos >= entries || !extent_is_unwritten(&ext[pos])) return EXT4_ERR_CORRUPT;
    u32 first = le32(ext[pos].ee_block);
    u32 len = extent_len_blocks(&ext[pos]);
    u32 end = first + len;
    if (len == 0 || logical < first || logical >= end) return EXT4_ERR_CORRUPT;
    u64 start = extent_start_block(&ext[pos]);
    u64 phys = start + (u64)(logical - first);
    if (phys >= mnt->blocks_count) return EXT4_ERR_RANGE;

    ext4_extent_t pieces[3];
    memset(pieces, 0, sizeof(pieces));
    u16 n = 0;
    if (logical > first) {
        pieces[n].ee_block = first;
        extent_set_start_block(&pieces[n], start);
        extent_set_len_blocks_state(&pieces[n], logical - first, true);
        ++n;
    }
    pieces[n].ee_block = logical;
    extent_set_start_block(&pieces[n], phys);
    extent_set_len_blocks_state(&pieces[n], 1u, false);
    ++n;
    if (logical + 1u < end) {
        pieces[n].ee_block = logical + 1u;
        extent_set_start_block(&pieces[n], phys + 1u);
        extent_set_len_blocks_state(&pieces[n], end - logical - 1u, true);
        ++n;
    }
    u16 add = (u16)(n - 1u);
    if ((u32)entries + add > max_entries) return EXT4_ERR_UNSUPPORTED;
    for (u16 i = entries + add; i > pos + n - 1u; --i) ext[i] = ext[i - add];
    for (u16 i = 0; i < n; ++i) ext[pos + i] = pieces[i];
    hdr->eh_entries = entries + add;

    u8 *zero = ext4_tmp_block(mnt);
    if (!zero) return EXT4_ERR_NO_MEMORY;
    memset(zero, 0, (usize)mnt->block_size);
    ext4_status_t st = write_data_block(mnt, phys, zero);
    ext4_tmp_block_free(zero);
    if (st != EXT4_OK) return st;
    ++g_ext4_perf_stats.unwritten_conversions;
    ++g_ext4_perf_stats.unwritten_zero_fills;
    extent_array_merge_neighbors(hdr, ext, pos);
    *phys_out = phys;
    return EXT4_OK;
}

static ext4_status_t extent_array_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *hdr, ext4_extent_t *ext, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    u16 pos = 0;
    for (; pos < entries; ++pos) {
        u32 first = le32(ext[pos].ee_block);
        u32 len = extent_len_blocks(&ext[pos]);
        u32 end = first + len;
        if (logical >= first && logical < end) {
            u64 start = extent_start_block(&ext[pos]);
            if (!checked_add_u64(start, (u64)(logical - first), phys_out)) return EXT4_ERR_RANGE;
            if (*phys_out >= mnt->blocks_count) return EXT4_ERR_RANGE;
            if (extent_is_unwritten(&ext[pos])) {
                if (create_unwritten) return EXT4_OK;
                if (!allocate) return EXT4_ERR_RANGE;
                return extent_array_convert_unwritten_block(mnt, hdr, ext, pos, logical, phys_out);
            }
            return EXT4_OK;
        }
        if (logical < first) break;
    }
    if (!allocate) return EXT4_ERR_RANGE;
    u64 goal = mnt->allocator_next_block;
    if (pos > 0) {
        const ext4_extent_t *prev = &ext[pos - 1u];
        u32 prev_first = le32(prev->ee_block);
        u32 prev_len = extent_len_blocks(prev);
        u64 prev_start = extent_start_block(prev);
        if (prev_first + prev_len == logical) goal = prev_start + prev_len;
    } else if (pos < entries) {
        const ext4_extent_t *next = &ext[pos];
        u64 next_start = extent_start_block(next);
        if (le32(next->ee_block) == logical + 1u && next_start > allocator_first_data_block(mnt)) goal = next_start - 1u;
    }
    u64 nb = 0;
    ext4_status_t st = alloc_data_block(mnt, goal, zero_new && !create_unwritten, &nb);
    if (st != EXT4_OK) return st;
    if (create_unwritten) ++g_ext4_perf_stats.unwritten_allocations;

    bool merged_prev = false;
    if (pos > 0) {
        ext4_extent_t *prev = &ext[pos - 1u];
        u32 prev_first = le32(prev->ee_block);
        u32 prev_len = extent_len_blocks(prev);
        u64 prev_start = extent_start_block(prev);
        if (extent_is_unwritten(prev) == create_unwritten && prev_len < 32768u && prev_first + prev_len == logical && prev_start + prev_len == nb) {
            extent_set_len_blocks_state(prev, prev_len + 1u, create_unwritten);
            merged_prev = true;
        }
    }
    if (merged_prev) {
        if (pos < entries) {
            ext4_extent_t *prev = &ext[pos - 1u];
            ext4_extent_t *next = &ext[pos];
            if (extent_pair_can_merge(prev, next)) {
                u32 prev_len = extent_len_blocks(prev);
                u32 next_len = extent_len_blocks(next);
                extent_set_len_blocks_state(prev, prev_len + next_len, create_unwritten);
                for (u16 i = pos; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
                memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
                hdr->eh_entries = entries - 1u;
            }
        }
        inode_add_blocks(mnt, inode, 1);
        *phys_out = nb;
        return EXT4_OK;
    }
    if (pos < entries) {
        ext4_extent_t *next = &ext[pos];
        u32 next_first = le32(next->ee_block);
        u32 next_len = extent_len_blocks(next);
        u64 next_start = extent_start_block(next);
        if (extent_is_unwritten(next) == create_unwritten && logical + 1u == next_first && nb + 1u == next_start && next_len < 32768u) {
            next->ee_block = logical;
            extent_set_start_block(next, nb);
            extent_set_len_blocks_state(next, next_len + 1u, create_unwritten);
            inode_add_blocks(mnt, inode, 1);
            *phys_out = nb;
            return EXT4_OK;
        }
    }
    if (entries >= max_entries) {
        (void)free_block(mnt, nb);
        return EXT4_ERR_UNSUPPORTED;
    }
    for (u16 i = entries; i > pos; --i) ext[i] = ext[i - 1u];
    memset(&ext[pos], 0, sizeof(ext[pos]));
    ext[pos].ee_block = logical;
    extent_set_len_blocks_state(&ext[pos], 1u, create_unwritten);
    extent_set_start_block(&ext[pos], nb);
    hdr->eh_entries = entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    *phys_out = nb;
    return EXT4_OK;
}

