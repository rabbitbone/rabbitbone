static bool dirent_live_entry_is_repairable(ext4_mount_t *mnt, const ext4_dir_entry_2_t *de, u16 rec_len) {
    if (!mnt || !de || de->inode == 0) return false;
    if (de->name_len == 0) return false;
    if (le32(de->inode) == 0 || le32(de->inode) > mnt->inodes_count) return false;
    if (de->file_type > 7u) return false;
    if (!ext4_name_bytes_valid(de->name, de->name_len, true)) return false;
    u16 need = ext4_rec_len(de->name_len);
    return need >= 8u && need <= rec_len;
}

static void dirent_copy_normalized(u8 *dst, u32 ino, u16 rec_len, u8 type, const char *name, u8 name_len) {
    ext4_dir_entry_2_t *out = (ext4_dir_entry_2_t *)dst;
    memset(out, 0, rec_len);
    out->inode = ino;
    out->rec_len = rec_len;
    out->name_len = name_len;
    out->file_type = type;
    if (name_len) memcpy(out->name, name, name_len);
}

static void dirent_make_free(u8 *dst, u16 rec_len) {
    ext4_dir_entry_2_t *out = (ext4_dir_entry_2_t *)dst;
    memset(out, 0, rec_len);
    out->rec_len = rec_len;
}

static ext4_status_t dirent_repair_dir(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, ext4_fsck_report_t *report) {
    if (!mnt || !dir || !ext4_inode_is_dir(dir)) return EXT4_OK;
    if (mnt->block_size == 0 || mnt->block_size > MAX_BLOCK_SIZE || mnt->block_size > 0xffffu) return EXT4_ERR_RANGE;
    u64 size = ext4_inode_size(dir);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *fixed = (u8 *)kmalloc((usize)mnt->block_size);
    if (!fixed) { ext4_tmp_block_free(block); return EXT4_ERR_NO_MEMORY; }
    bool changed_any = false;
    ext4_status_t final_st = EXT4_OK;
    u32 block_count = 0;
    if (!ext4_block_count_u32_checked(mnt, size, &block_count)) { final_st = EXT4_ERR_RANGE; goto done; }
    for (u32 logical = 0; logical < block_count; ++logical) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { final_st = st; break; }

        memset(fixed, 0, (usize)mnt->block_size);
        usize in = 0;
        usize out = 0;
        bool parse_ok = true;
        bool changed = false;
        u32 live_entries = 0;

        while (in + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + in);
            u16 rec_len = le16(de->rec_len);
            usize next_in = 0;
            if (rec_len < 8u || (rec_len & 3u) != 0 || !checked_add_usize(in, rec_len, &next_in) || next_in > mnt->block_size) {
                parse_ok = false;
                changed = true;
                break;
            }

            if (dirent_live_entry_is_repairable(mnt, de, rec_len)) {
                u16 need = ext4_rec_len(de->name_len);
                usize next_out = 0;
                if (!checked_add_usize(out, need, &next_out) || next_out > mnt->block_size) {
                    parse_ok = false;
                    changed = true;
                    break;
                }
                dirent_copy_normalized(fixed + out, le32(de->inode), need, de->file_type, de->name, de->name_len);
                out = next_out;
                ++live_entries;
            } else {
                if (de->inode != 0 || de->name_len != 0 || de->file_type != 0) changed = true;
            }
            in = next_in;
        }
        if (in != mnt->block_size) changed = true;

        if (out == 0) {
            dirent_make_free(fixed, (u16)mnt->block_size);
        } else {
            ext4_dir_entry_2_t *last = 0;
            usize scan = 0;
            for (u32 i = 0; i < live_entries; ++i) {
                last = (ext4_dir_entry_2_t *)(fixed + scan);
                usize next_scan = 0;
                if (!checked_add_usize(scan, le16(last->rec_len), &next_scan) || next_scan > mnt->block_size) {
                    final_st = EXT4_ERR_CORRUPT;
                    break;
                }
                scan = next_scan;
            }
            if (!last || scan != out) {
                final_st = EXT4_ERR_CORRUPT;
                break;
            }
            last->rec_len = (u16)(le16(last->rec_len) + (u16)(mnt->block_size - out));
        }

        if (!parse_ok || memcmp(block, fixed, (usize)mnt->block_size) != 0) changed = true;
        if (changed) {
            st = write_block(mnt, phys, fixed);
            if (st != EXT4_OK) { final_st = st; break; }
            changed_any = true;
        }
    }
done:
    kfree(fixed);
    ext4_tmp_block_free(block);
    if (final_st != EXT4_OK) return final_st;
    if (changed_any) {
        if ((le32(dir->i_flags) & EXT4_INDEX_FL) != 0) {
            ext4_status_t st = htree_repair_dir(mnt, dir_ino, dir, report);
            if (st != EXT4_OK) return st;
        } else {
            ext4_status_t st = write_inode(mnt, dir_ino, dir);
            if (st != EXT4_OK) return st;
        }
        if (report) ++report->repaired_dirents;
        ++g_ext4_perf_stats.repair_dirent_fixes;
    }
    return EXT4_OK;
}

ext4_status_t ext4_repair_metadata(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (report) memset(report, 0, sizeof(*report));
    ++g_ext4_perf_stats.repair_runs;
    ext4_status_t st = ext4_flush_and_invalidate_read_caches(mnt);
    if (st != EXT4_OK) return st;

    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) goto out_bm;
        st = read_block(mnt, group_inode_bitmap(&gd), bm);
        if (st != EXT4_OK) goto out_bm;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) continue;
            u64 ino64 = (u64)group * (u64)mnt->inodes_per_group + (u64)bit + 1u;
            if (ino64 == 0 || ino64 > mnt->inodes_count || ino64 > 0xffffffffull) continue;
            u32 ino = (u32)ino64;
            ext4_inode_disk_t inode;
            st = ext4_read_inode(mnt, ino, &inode);
            if (st != EXT4_OK) goto out_bm;
            if (inode.i_mode == 0 || !ext4_inode_is_dir(&inode)) continue;
            st = dirent_repair_dir(mnt, ino, &inode, report);
            if (st != EXT4_OK) goto out_bm;
            st = htree_repair_dir(mnt, ino, &inode, report);
            if (st != EXT4_OK) goto out_bm;
        }
    }
    ext4_tmp_block_free(bm);
    bm = 0;

    u64 old_free_blocks = sb_free_blocks(mnt);
    u32 old_free_inodes = sb_free_inodes(mnt);
    st = ext4_recount_free_counters(mnt);
    if (st != EXT4_OK) return st;
    if (old_free_blocks != sb_free_blocks(mnt) || old_free_inodes != sb_free_inodes(mnt)) {
        if (report) ++report->repaired_counters;
        ++g_ext4_perf_stats.repair_counter_fixes;
    }

    bool checksum_fixed = false;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
        u16 want = group_desc_checksum(mnt, group, &gd);
        if (gd.bg_checksum != want) checksum_fixed = true;
        st = write_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
    }
    u32 old_super_sum = mnt->sb.s_checksum;
    st = write_super(mnt);
    if (st != EXT4_OK) return st;
    if (checksum_fixed || old_super_sum != mnt->sb.s_checksum) {
        if (report) ++report->repaired_checksums;
        ++g_ext4_perf_stats.repair_checksum_fixes;
    }
    st = ext4_sync_metadata(mnt);
    if (st != EXT4_OK) return st;
    if (!report) return EXT4_OK;
    u32 repaired_counters = report->repaired_counters;
    u32 repaired_checksums = report->repaired_checksums;
    u32 repaired_htree = report->repaired_htree;
    u32 repaired_dirents = report->repaired_dirents;
    ext4_fsck_report_t verify;
    st = ext4_validate_metadata(mnt, &verify);
    *report = verify;
    report->repaired_counters = repaired_counters;
    report->repaired_checksums = repaired_checksums;
    report->repaired_htree = repaired_htree;
    report->repaired_dirents = repaired_dirents;
    return st;
out_bm:
    ext4_tmp_block_free(bm);
    return st;
}

