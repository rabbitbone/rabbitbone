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
    u32 incompat = le32(sb.s_feature_incompat);
    u32 supported = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT;
    if ((incompat & ~supported) != 0) return EXT4_ERR_UNSUPPORTED;
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
    out->group_count = (u32)div_round_up_u64(out->blocks_count - first_data, out->blocks_per_group);
    if (out->group_count == 0) return EXT4_ERR_CORRUPT;
    out->sb = sb;
    out->allocator_next_block = first_data;
    st = ext4_flush_data_cache_for_mount(out);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(out);
    if (st != EXT4_OK) return st;
    data_cache_discard_clean_mount(out);
    write_cache_discard_clean_mount(out);

    if (out->blocks_count > first_data + 8u) {
        out->journal_blocks = 2u;
        out->journal_first_block = out->blocks_count - out->journal_blocks;
        out->journal_ready = false;
        st = ext4_recover(out, 0);
        if (st != EXT4_OK) return st;
        for (u32 i = 0; i < out->journal_blocks; ++i) {
            st = reserve_block_if_free(out, out->journal_first_block + i);
            if (st != EXT4_OK) return st;
        }
        out->journal_ready = true;
        st = ext4_recount_free_counters(out);
        if (st != EXT4_OK) return st;
        st = journal_clear(out);
        if (st != EXT4_OK) return st;
    }
    if (out->sb.s_checksum == 0 || out->sb.s_checksum != super_checksum(&out->sb)) {
        st = write_super(out);
        if (st != EXT4_OK) return st;
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
    st = ext4_flush_data_cache_for_mount(mnt);
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

