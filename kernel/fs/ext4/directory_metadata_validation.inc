static ext4_status_t validate_htree_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_fsck_report_t *report) {
    if (!mnt || !inode || !report || !ext4_inode_is_dir(inode) || (le32(inode->i_flags) & EXT4_INDEX_FL) == 0) return EXT4_OK;
    u32 blockno = le32(inode->i_file_acl_lo);
    if (!blockno) { ++report->htree_errors; ++report->errors; return EXT4_ERR_CORRUPT; }
    bool allocated = false;
    ext4_status_t st = block_bitmap_is_allocated(mnt, blockno, &allocated);
    if (st != EXT4_OK) return st;
    if (!allocated) { ++report->htree_errors; ++report->errors; return EXT4_ERR_CORRUPT; }
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *dir_block = ext4_tmp_block(mnt);
    if (!dir_block) { ext4_tmp_block_free(block); return EXT4_ERR_NO_MEMORY; }
    rabbitbone_htree_header_t *hdr = 0;
    rabbitbone_htree_entry_t *entries = 0;
    st = htree_load(mnt, inode, block, &hdr, &entries);
    if (st != EXT4_OK) { ++report->htree_errors; ++report->errors; goto out; }
    ++report->htree_dirs;
    report->htree_entries += le16(hdr->entries);
    u32 prev_hash = 0;
    for (u16 i = 0; i < le16(hdr->entries); ++i) {
        u32 h = le32(entries[i].hash);
        if (i && h < prev_hash) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        if (le16(entries[i].offset) >= mnt->block_size) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        u64 phys = 0;
        st = map_inode_block(mnt, inode, le16(entries[i].logical_block), &phys);
        if (st != EXT4_OK) { ++report->htree_errors; ++report->errors; goto out; }
        st = read_block(mnt, phys, dir_block);
        if (st != EXT4_OK) goto out;
        u16 off = le16(entries[i].offset);
        if ((usize)off + 8u > mnt->block_size) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(dir_block + off);
        u16 rec_len = le16(de->rec_len);
        if (rec_len < 8u || (usize)off + rec_len > mnt->block_size || de->inode == 0 ||
            de->name_len != entries[i].name_len || de->file_type != entries[i].file_type ||
            le32(de->inode) != le32(entries[i].inode) || 8u + de->name_len > rec_len ||
            htree_hash_name(de->name, de->name_len) != h) {
            ++report->htree_errors;
            ++report->errors;
            st = EXT4_ERR_CORRUPT;
            goto out;
        }
        prev_hash = h;
    }
    st = EXT4_OK;
out:
    ext4_tmp_block_free(dir_block);
    ext4_tmp_block_free(block);
    return st;
}


static ext4_status_t validate_dirent_rec_len_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_fsck_report_t *report) {
    if (!mnt || !dir || !report || !ext4_inode_is_dir(dir)) return EXT4_OK;
    u64 size = ext4_inode_size(dir);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 logical64 = off / mnt->block_size;
        if (logical64 > 0xffffffffull) { ++report->errors; result = EXT4_ERR_RANGE; goto out; }
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, dir, (u32)logical64, &phys);
        if (st != EXT4_OK) { ++report->errors; result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || (rec_len & 3u) != 0 || p + rec_len > mnt->block_size) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && (de->name_len == 0 || ext4_rec_len(de->name_len) > rec_len)) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            if (!de->inode && (de->name_len != 0 || de->file_type != 0)) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            p += rec_len;
        }
        if (p != mnt->block_size) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t ext4_recount_free_counters(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u64 total_free_blocks = 0;
    u32 total_free_inodes = 0;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, group_block_bitmap(&gd), bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_blocks = 0;
        u32 valid_blocks = group_valid_blocks(mnt, group);
        for (u32 bit = 0; bit < valid_blocks; ++bit) {
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < le32(mnt->sb.s_first_data_block)) continue;
            if (!bitmap_get(bm, bit)) ++free_blocks;
        }
        st = read_block(mnt, group_inode_bitmap(&gd), bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_inodes = 0;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) ++free_inodes;
        }
        if (free_blocks != gd_free_blocks(&gd) || free_inodes != gd_free_inodes(&gd)) {
            gd_set_free_blocks(&gd, free_blocks);
            gd_set_free_inodes(&gd, free_inodes);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) { result = st; goto out; }
        }
        total_free_blocks += free_blocks;
        total_free_inodes += free_inodes;
    }
    if (total_free_blocks != sb_free_blocks(mnt) || total_free_inodes != sb_free_inodes(mnt)) {
        sb_set_free_blocks(mnt, total_free_blocks);
        sb_set_free_inodes(mnt, total_free_inodes);
        result = write_super(mnt);
        goto out;
    }
out:
    ext4_tmp_block_free(bm);
    return result;
}

ext4_status_t ext4_validate_metadata(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt || !report) return EXT4_ERR_RANGE;
    ext4_status_t flush_st = ext4_flush_and_invalidate_read_caches(mnt);
    if (flush_st != EXT4_OK) return flush_st;
    memset(report, 0, sizeof(*report));
    report->sb_free_blocks = sb_free_blocks(mnt);
    report->sb_free_inodes = sb_free_inodes(mnt);
    report->orphan_recovered = mnt->recovered_orphans;
    report->journal_replays = mnt->recovered_journal_replays;
    if (mnt->sb.s_checksum && mnt->sb.s_checksum != super_checksum(&mnt->sb)) { ++report->checksum_errors; ++report->errors; }
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u32 inode_table_blocks = (u32)div_round_up_u64((u64)mnt->inodes_per_group * mnt->inode_size, mnt->block_size);
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        ++report->checked_groups;
        if (gd.bg_checksum && gd.bg_checksum != group_desc_checksum(mnt, group, &gd)) { ++report->checksum_errors; ++report->errors; }
        u64 block_bm = group_block_bitmap(&gd);
        u64 inode_bm = group_inode_bitmap(&gd);
        u64 inode_table = group_inode_table(&gd);
        if (block_bm >= mnt->blocks_count || inode_bm >= mnt->blocks_count || inode_table >= mnt->blocks_count) {
            ++report->errors;
            result = EXT4_ERR_CORRUPT;
            goto out;
        }
        if (inode_table_blocks && inode_table + inode_table_blocks > mnt->blocks_count) {
            ++report->errors;
            result = EXT4_ERR_CORRUPT;
            goto out;
        }
        st = read_block(mnt, block_bm, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_blocks = 0;
        u32 valid_blocks = group_valid_blocks(mnt, group);
        for (u32 bit = 0; bit < valid_blocks; ++bit) {
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < le32(mnt->sb.s_first_data_block)) continue;
            if (!bitmap_get(bm, bit)) ++free_blocks;
        }
        report->bitmap_free_blocks += free_blocks;
        if (free_blocks != gd_free_blocks(&gd)) ++report->errors;

        st = read_block(mnt, inode_bm, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_inodes = 0;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) ++free_inodes;
        }
        report->bitmap_free_inodes += free_inodes;
        if (free_inodes != gd_free_inodes(&gd)) ++report->errors;

        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) continue;
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino == 0 || ino > mnt->inodes_count) continue;
            ext4_inode_disk_t inode;
            st = ext4_read_inode(mnt, ino, &inode);
            if (st != EXT4_OK) { result = st; goto out; }
            if (inode.i_mode == 0) continue;
            ++report->checked_inodes;
            st = validate_dirent_rec_len_inode(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
            st = validate_inode_extent_allocations(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
            st = validate_htree_inode(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
        }
    }
    if (report->bitmap_free_blocks != report->sb_free_blocks) ++report->errors;
    if (report->bitmap_free_inodes != report->sb_free_inodes) ++report->errors;
    result = report->errors ? EXT4_ERR_CORRUPT : EXT4_OK;
out:
    ext4_tmp_block_free(bm);
    return result;
}

