static ext4_status_t read_data_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (slot) {
        memcpy(buffer, slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.data_cache_hits;
        return EXT4_OK;
    }
    ext4_status_t st = read_block(mnt, block, buffer);
    if (st != EXT4_OK) return st;
    st = data_cache_store_clean_block(mnt, block, buffer, false);
    if (st != EXT4_OK) return st;
    data_cache_readahead(mnt, block + 1u);
    return EXT4_OK;
}

static ext4_status_t write_cache_flush_slot_with_mount(ext4_mount_t *mnt, ext4_write_cache_slot_t *slot) {
    if (!slot || !slot->valid || !slot->dirty) return EXT4_OK;
    ++g_ext4_perf_stats.cache_flushes;
    ext4_status_t st;
    if (mnt && slot->dev == mnt->dev && slot->partition_lba == mnt->partition_lba &&
        slot->partition_sectors == mnt->partition_sectors && slot->block_size == (u32)mnt->block_size &&
        slot->block < mnt->blocks_count) {
        st = journal_commit_ordered_block(mnt, slot->block, slot->data);
    } else {
        ext4_mount_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.dev = slot->dev;
        tmp.partition_lba = slot->partition_lba;
        tmp.partition_sectors = slot->partition_sectors;
        tmp.block_size = slot->block_size;
        if (!ext4_partition_blocks_from_sectors(slot->partition_sectors, slot->block_size, &tmp.blocks_count)) return EXT4_ERR_RANGE;
        tmp.journal_ready = false;
        st = raw_write_block(&tmp, slot->block, slot->data);
    }
    if (st != EXT4_OK) return st;
    slot->dirty = false;
    slot->valid = false;
    return EXT4_OK;
}

static ext4_status_t ext4_flush_write_cache_for_mount(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (!g_ext4_write_cache || !g_ext4_write_cache_slots) return EXT4_OK;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (!slot->valid || !slot->dirty || slot->dev != mnt->dev || slot->partition_lba != mnt->partition_lba ||
            slot->partition_sectors != mnt->partition_sectors || slot->block_size != (u32)mnt->block_size) continue;
        ++g_ext4_perf_stats.cache_flushes;
        ext4_status_t st = journal_commit_ordered_block(mnt, slot->block, slot->data);
        if (st != EXT4_OK) return st;
        slot->dirty = false;
        slot->valid = false;
    }
    return EXT4_OK;
}

static ext4_status_t write_cache_store_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    data_cache_discard_block(mnt, block);
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return journal_commit_ordered_block(mnt, block, buffer);
    ext4_write_cache_slot_t *slot = write_cache_find_slot(mnt, block);
    if (!slot) {
        if (!write_cache_ensure()) return journal_commit_ordered_block(mnt, block, buffer);
        for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
            if (!g_ext4_write_cache[i].valid) { slot = &g_ext4_write_cache[i]; break; }
        }
    }
    if (!slot) {
        if (!write_cache_ensure()) return journal_commit_ordered_block(mnt, block, buffer);
        slot = &g_ext4_write_cache[0];
        ++g_ext4_perf_stats.cache_evictions;
        ext4_status_t st = write_cache_flush_slot_with_mount(mnt, slot);
        if (st != EXT4_OK) return st;
    }
    slot->valid = true;
    slot->dirty = true;
    slot->dev = mnt->dev;
    slot->partition_lba = mnt->partition_lba;
    slot->partition_sectors = mnt->partition_sectors;
    slot->block = block;
    slot->block_size = (u32)mnt->block_size;
    memcpy(slot->data, buffer, (usize)mnt->block_size);
    ++g_ext4_perf_stats.cache_stores;
    return EXT4_OK;
}

static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    ext4_write_cache_slot_t *slot = write_cache_find_slot(mnt, block);
    if (slot) {
        memcpy(buffer, slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.cache_hits;
        return EXT4_OK;
    }
    ext4_data_cache_slot_t *data_slot = data_cache_find_slot(mnt, block);
    if (data_slot) {
        memcpy(buffer, data_slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.data_cache_hits;
        return EXT4_OK;
    }
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    ++g_ext4_perf_stats.raw_reads;
    return read_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, buffer, (usize)mnt->block_size);
}


static ext4_status_t raw_write_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    ++g_ext4_perf_stats.raw_writes;
    return write_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, buffer, (usize)mnt->block_size);
}

