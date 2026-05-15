static ext4_status_t ext4_block_bitmap_is_free(ext4_mount_t *mnt, u64 block, bool *free_out) {
    if (!mnt || !free_out || block >= mnt->blocks_count || mnt->blocks_per_group == 0) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    if (group >= mnt->group_count) return EXT4_ERR_RANGE;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st == EXT4_OK) *free_out = !bitmap_get(bm, bit);
    ext4_tmp_block_free(bm);
    return st;
}

static ext4_status_t ext4_journal_area_is_free(ext4_mount_t *mnt, u64 first, u32 count, bool *free_out) {
    if (!mnt || !free_out || count == 0) return EXT4_ERR_RANGE;
    *free_out = false;
    for (u32 i = 0; i < count; ++i) {
        u64 block = first + i;
        if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
        bool one_free = false;
        ext4_status_t st = ext4_block_bitmap_is_free(mnt, block, &one_free);
        if (st != EXT4_OK) return st;
        if (!one_free) return EXT4_OK;
    }
    *free_out = true;
    return EXT4_OK;
}

static ext4_status_t ext4_journal_area_has_rabbitbone_marker(ext4_mount_t *mnt, u64 first, bool *owned_out) {
    if (!mnt || !owned_out || first + 1u >= mnt->blocks_count) return EXT4_ERR_RANGE;
    *owned_out = false;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t st = read_block(mnt, first, block);
    if (st == EXT4_OK) {
        rabbitbone_ext4_journal_record_t jr;
        memcpy(&jr, block, sizeof(jr));
        u64 old_first = mnt->journal_first_block;
        u32 old_blocks = mnt->journal_blocks;
        mnt->journal_first_block = first;
        mnt->journal_blocks = 2u;
        *owned_out = journal_record_header_valid(mnt, &jr);
        mnt->journal_first_block = old_first;
        mnt->journal_blocks = old_blocks;
    }
    ext4_tmp_block_free(block);
    return st;
}

static u64 default_partition_sectors(block_device_t *dev, u64 partition_lba) {
    if (!dev || dev->sector_count == 0) return 0;
    if (partition_lba >= dev->sector_count) return 0;
    return dev->sector_count - partition_lba;
}

ext4_status_t ext4_mount_bounded(block_device_t *dev, u64 partition_lba, u64 partition_sectors, ext4_mount_t *out) {
    if (!dev || !out) return EXT4_ERR_IO;
    if (dev->sector_size && dev->sector_size != 512u) return EXT4_ERR_UNSUPPORTED;
    if (dev->sector_count) {
        if (partition_lba >= dev->sector_count) return EXT4_ERR_RANGE;
        if (partition_sectors == 0 || partition_sectors > dev->sector_count - partition_lba) return EXT4_ERR_RANGE;
    }
    memset(out, 0, sizeof(*out));
    ext4_superblock_disk_t sb;
    ext4_status_t st = read_bytes(dev, partition_lba, partition_sectors, 1024, &sb, sizeof(sb));
    if (st != EXT4_OK) return st;
    if (le16(sb.s_magic) != EXT4_SUPER_MAGIC) return EXT4_ERR_BAD_MAGIC;
    u32 log_block = le32(sb.s_log_block_size);
    if (log_block > 2u) return EXT4_ERR_UNSUPPORTED;
    u64 block_size = 1024ull << log_block;
    if (block_size < 1024 || block_size > MAX_BLOCK_SIZE) return EXT4_ERR_UNSUPPORTED;
    u64 blocks_count = le64_from_lo_hi(sb.s_blocks_count_lo, sb.s_blocks_count_hi);
    if (partition_sectors) {
        u64 fs_bytes = 0;
        u64 part_bytes = 0;
        if (!checked_mul_u64(blocks_count, block_size, &fs_bytes)) return EXT4_ERR_RANGE;
        if (!checked_mul_u64(partition_sectors, 512u, &part_bytes)) return EXT4_ERR_RANGE;
        if (fs_bytes > part_bytes) return EXT4_ERR_RANGE;
    }
    u32 compat = le32(sb.s_feature_compat);
    u32 incompat = le32(sb.s_feature_incompat);
    u32 ro_compat = le32(sb.s_feature_ro_compat);
    if ((compat & ~EXT4_FEATURE_COMPAT_SUPPORTED) != 0) return EXT4_ERR_UNSUPPORTED;
    if ((incompat & ~EXT4_FEATURE_INCOMPAT_SUPPORTED) != 0) return EXT4_ERR_UNSUPPORTED;
    if ((ro_compat & ~EXT4_FEATURE_RO_COMPAT_SUPPORTED) != 0) return EXT4_ERR_UNSUPPORTED;
    out->dev = dev;
    out->partition_lba = partition_lba;
    out->partition_sectors = partition_sectors;
    out->block_size = block_size;
    out->blocks_count = blocks_count;
    out->inodes_count = le32(sb.s_inodes_count);
    out->inodes_per_group = le32(sb.s_inodes_per_group);
    out->blocks_per_group = le32(sb.s_blocks_per_group);
    out->inode_size = le16(sb.s_inode_size) ? le16(sb.s_inode_size) : 128;
    if (out->blocks_per_group > block_size * 8u || out->inodes_per_group > block_size * 8u) return EXT4_ERR_CORRUPT;
    out->group_desc_size = (incompat & EXT4_FEATURE_INCOMPAT_64BIT) ? le16(sb.s_desc_size) : 32;
    u64 first_data = le32(sb.s_first_data_block);
    if (!out->blocks_count || !out->inodes_count || !out->blocks_per_group || !out->inodes_per_group) return EXT4_ERR_CORRUPT;
    if (first_data > out->blocks_count) return EXT4_ERR_CORRUPT;
    if (out->inode_size < 128u || out->inode_size > block_size) return EXT4_ERR_UNSUPPORTED;
    if (out->group_desc_size < 32 || out->group_desc_size > sizeof(ext4_group_desc_disk_t) || (out->group_desc_size % 8u) != 0) return EXT4_ERR_UNSUPPORTED;
    u64 group_count64 = div_round_up_u64(out->blocks_count - first_data, out->blocks_per_group);
    if (group_count64 == 0 || group_count64 > 0xffffffffull) return EXT4_ERR_CORRUPT;
    out->group_count = (u32)group_count64;
    out->sb = sb;
    out->allocator_next_block = first_data;
    st = ext4_flush_data_cache_for_mount(out);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(out);
    if (st != EXT4_OK) return st;
    data_cache_discard_clean_mount(out);
    write_cache_discard_clean_mount(out);

    if (out->blocks_count > first_data + 8u) {
        u64 journal_first = out->blocks_count - 2u;
        bool journal_area_free = false;
        st = ext4_journal_area_is_free(out, journal_first, 2u, &journal_area_free);
        if (st != EXT4_OK) return st;
        bool journal_owned = false;
        if (!journal_area_free) {
            st = ext4_journal_area_has_rabbitbone_marker(out, journal_first, &journal_owned);
            if (st != EXT4_OK) return st;
        }
        if (journal_area_free || journal_owned) {
            out->journal_blocks = 2u;
            out->journal_first_block = journal_first;
            out->journal_ready = false;
            if (journal_area_free) {
                for (u32 i = 0; i < out->journal_blocks; ++i) {
                    st = reserve_block_if_free(out, out->journal_first_block + i);
                    if (st != EXT4_OK) return st;
                }
            }
            st = ext4_recover(out, 0);
            if (st != EXT4_OK) return st;
            out->journal_ready = true;
            st = ext4_recount_free_counters(out);
            if (st != EXT4_OK) return st;
            st = journal_clear(out);
            if (st != EXT4_OK) return st;
        }
    }
    u32 expected_super_checksum = super_checksum(&out->sb);
    if (out->sb.s_checksum == 0) {
        st = write_super(out);
        if (st != EXT4_OK) return st;
    } else if (out->sb.s_checksum != expected_super_checksum) {
        return EXT4_ERR_CORRUPT;
    }
    return EXT4_OK;
}


ext4_status_t ext4_sync_metadata(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    ext4_status_t st = ext4_flush_data_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    st = write_super(mnt);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    return journal_clear(mnt);
}

ext4_status_t ext4_sync_file(ext4_mount_t *mnt, u32 ino, bool data_only) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_regular(&inode) && !ext4_inode_is_dir(&inode)) return EXT4_ERR_UNSUPPORTED;
    st = ext4_flush_data_cache_for_inode(mnt, &inode);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    if (!data_only) {
        st = write_super(mnt);
        if (st != EXT4_OK) return st;
    }
    return journal_clear(mnt);
}

ext4_status_t ext4_get_perf_stats(ext4_mount_t *mnt, ext4_perf_stats_t *out) {
    if (!mnt || !out) return EXT4_ERR_RANGE;
    *out = g_ext4_perf_stats;
    out->cache_slots = g_ext4_write_cache_slots ? g_ext4_write_cache_slots : EXT4_WRITE_CACHE_SLOTS;
    out->dirty_slots = 0;
    if (g_ext4_write_cache && g_ext4_write_cache_slots) {
        for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
            ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
            if (slot->valid && slot->dirty && slot->dev == mnt->dev &&
                slot->partition_lba == mnt->partition_lba &&
                slot->partition_sectors == mnt->partition_sectors &&
                slot->block_size == (u32)mnt->block_size) ++out->dirty_slots;
        }
    }
    if (g_ext4_data_cache && g_ext4_data_cache_slots) {
        for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
            ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
            if (slot->valid && slot->dirty && slot->dev == mnt->dev &&
                slot->partition_lba == mnt->partition_lba &&
                slot->partition_sectors == mnt->partition_sectors &&
                slot->block_size == (u32)mnt->block_size) ++out->dirty_slots;
        }
    }
    return EXT4_OK;
}

ext4_status_t ext4_mount(block_device_t *dev, u64 partition_lba, ext4_mount_t *out) {
    return ext4_mount_bounded(dev, partition_lba, default_partition_sectors(dev, partition_lba), out);
}

