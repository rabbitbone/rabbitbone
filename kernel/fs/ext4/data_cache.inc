static u64 data_cache_tick(void) {
    ++g_ext4_cache_clock;
    if (g_ext4_cache_clock == 0) ++g_ext4_cache_clock;
    return g_ext4_cache_clock;
}

static bool data_cache_ensure(void) {
    if (g_ext4_data_cache && g_ext4_data_cache_slots) return true;
    g_ext4_data_cache = (ext4_data_cache_slot_t *)kcalloc(EXT4_DATA_CACHE_SLOTS, sizeof(ext4_data_cache_slot_t));
    if (!g_ext4_data_cache) {
        g_ext4_data_cache_slots = 0;
        return false;
    }
    g_ext4_data_cache_slots = EXT4_DATA_CACHE_SLOTS;
    return true;
}

static void data_cache_touch(ext4_data_cache_slot_t *slot) {
    if (slot) slot->last_used = data_cache_tick();
}

static bool data_cache_slot_matches(const ext4_data_cache_slot_t *slot, const ext4_mount_t *mnt, u64 block) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size && slot->block == block;
}

static ext4_data_cache_slot_t *data_cache_find_slot(const ext4_mount_t *mnt, u64 block) {
    if (!g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_matches(slot, mnt, block)) {
            data_cache_touch(slot);
            return slot;
        }
    }
    return 0;
}

static void data_cache_drop_slot(ext4_data_cache_slot_t *slot) {
    if (!slot) return;
    slot->dirty = false;
    slot->valid = false;
    slot->readahead = false;
    slot->last_used = 0;
}

static void data_cache_discard_block(const ext4_mount_t *mnt, u64 block) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_matches(slot, mnt, block)) {
            data_cache_drop_slot(slot);
            ++g_ext4_perf_stats.data_cache_invalidations;
        }
    }
}

static bool data_cache_slot_mount_matches(const ext4_data_cache_slot_t *slot, const ext4_mount_t *mnt) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size;
}

static void data_cache_discard_clean_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_mount_matches(slot, mnt) && !slot->dirty) {
            data_cache_drop_slot(slot);
            ++g_ext4_perf_stats.data_cache_invalidations;
        }
    }
}

static bool write_cache_slot_mount_matches(const ext4_write_cache_slot_t *slot, const ext4_mount_t *mnt) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size;
}

static void write_cache_discard_clean_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_write_cache || !g_ext4_write_cache_slots) return;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (write_cache_slot_mount_matches(slot, mnt) && !slot->dirty) {
            slot->valid = false;
            ++g_ext4_perf_stats.cache_invalidations;
        }
    }
}

static ext4_status_t ext4_flush_and_invalidate_read_caches(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    ext4_status_t st = ext4_flush_data_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    data_cache_discard_clean_mount(mnt);
    write_cache_discard_clean_mount(mnt);
    return EXT4_OK;
}

static ext4_status_t data_cache_flush_slot(ext4_mount_t *mnt, ext4_data_cache_slot_t *slot) {
    if (!slot || !slot->valid || !slot->dirty) return EXT4_OK;
    ++g_ext4_perf_stats.data_cache_flushes;
    ext4_mount_t tmp;
    ext4_mount_t *use = mnt;
    if (!use || slot->dev != use->dev || slot->partition_lba != use->partition_lba ||
        slot->partition_sectors != use->partition_sectors || slot->block_size != (u32)use->block_size ||
        slot->block >= use->blocks_count) {
        memset(&tmp, 0, sizeof(tmp));
        tmp.dev = slot->dev;
        tmp.partition_lba = slot->partition_lba;
        tmp.partition_sectors = slot->partition_sectors;
        tmp.block_size = slot->block_size;
        if (!ext4_partition_blocks_from_sectors(slot->partition_sectors, slot->block_size, &tmp.blocks_count)) return EXT4_ERR_RANGE;
        use = &tmp;
    }
    ext4_status_t st = raw_write_block(use, slot->block, slot->data);
    if (st != EXT4_OK) return st;
    data_cache_drop_slot(slot);
    return EXT4_OK;
}

static ext4_status_t ext4_flush_data_cache_for_mount(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (!g_ext4_data_cache || !g_ext4_data_cache_slots) return EXT4_OK;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!slot->valid || !slot->dirty || slot->dev != mnt->dev || slot->partition_lba != mnt->partition_lba ||
            slot->partition_sectors != mnt->partition_sectors || slot->block_size != (u32)mnt->block_size) continue;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static ext4_status_t ext4_flush_data_cache_for_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *inode) {
    if (!mnt || !inode) return EXT4_ERR_RANGE;
    if (!g_ext4_data_cache || !g_ext4_data_cache_slots) return EXT4_OK;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u64 size = ext4_inode_size(inode);
    u64 blocks64 = div_round_up_u64(size, mnt->block_size);
    if (blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    for (u64 logical64 = 0; logical64 < blocks64; ++logical64) {
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, inode, (u32)logical64, &phys);
        if (st == EXT4_ERR_RANGE) continue;
        if (st != EXT4_OK) return st;
        for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
            ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
            if (data_cache_slot_matches(slot, mnt, phys) && slot->dirty) {
                st = data_cache_flush_slot(mnt, slot);
                if (st != EXT4_OK) return st;
            }
        }
    }
    return EXT4_OK;
}

static u32 data_cache_dirty_count_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    u32 count = 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        const ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_mount_matches(slot, mnt) && slot->dirty) ++count;
    }
    return count;
}

static ext4_data_cache_slot_t *data_cache_lru_slot(const ext4_mount_t *mnt, bool prefer_clean) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    ext4_data_cache_slot_t *best = 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!slot->valid) return slot;
        if (!data_cache_slot_mount_matches(slot, mnt)) continue;
        if (prefer_clean && slot->dirty) continue;
        if (!best || slot->last_used < best->last_used) best = slot;
    }
    if (best) return best;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!best || slot->last_used < best->last_used) best = slot;
    }
    return best;
}

static ext4_status_t data_cache_flush_pressure(ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return EXT4_OK;
    if (data_cache_dirty_count_mount(mnt) <= EXT4_DATA_CACHE_DIRTY_HIGH_WATER) return EXT4_OK;
    ++g_ext4_perf_stats.data_cache_pressure_flushes;
    ++g_ext4_perf_stats.data_cache_writeback_runs;
    for (u32 n = 0; n < EXT4_DATA_CACHE_WRITEBACK_BATCH; ++n) {
        ext4_data_cache_slot_t *best = 0;
        for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
            ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
            if (!data_cache_slot_mount_matches(slot, mnt) || !slot->dirty) continue;
            if (!best || slot->last_used < best->last_used) best = slot;
        }
        if (!best) break;
        ext4_status_t st = data_cache_flush_slot(mnt, best);
        if (st != EXT4_OK) return st;
        if (data_cache_dirty_count_mount(mnt) <= EXT4_DATA_CACHE_DIRTY_HIGH_WATER) break;
    }
    return EXT4_OK;
}

static ext4_status_t data_cache_install_slot(ext4_mount_t *mnt, u64 block, const void *buffer, bool dirty, bool readahead) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (!data_cache_ensure()) return dirty ? raw_write_block(mnt, block, buffer) : EXT4_OK;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (!slot) slot = data_cache_lru_slot(mnt, !dirty);
    if (!slot) return dirty ? raw_write_block(mnt, block, buffer) : EXT4_OK;
    if (slot->valid && (slot->dirty || !data_cache_slot_matches(slot, mnt, block))) {
        ++g_ext4_perf_stats.data_cache_evictions;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    } else if (slot->valid && !data_cache_slot_matches(slot, mnt, block)) {
        ++g_ext4_perf_stats.data_cache_evictions;
        data_cache_drop_slot(slot);
    }
    slot->valid = true;
    slot->dirty = dirty;
    slot->dev = mnt->dev;
    slot->partition_lba = mnt->partition_lba;
    slot->partition_sectors = mnt->partition_sectors;
    slot->block = block;
    slot->block_size = (u32)mnt->block_size;
    slot->readahead = readahead;
    slot->last_used = data_cache_tick();
    memcpy(slot->data, buffer, (usize)mnt->block_size);
    if (dirty) ++g_ext4_perf_stats.data_cache_stores;
    else ++g_ext4_perf_stats.data_cache_clean_stores;
    return dirty ? data_cache_flush_pressure(mnt) : EXT4_OK;
}

static ext4_status_t data_cache_store_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return raw_write_block(mnt, block, buffer);
    return data_cache_install_slot(mnt, block, buffer, true, false);
}

static ext4_status_t data_cache_store_clean_block(ext4_mount_t *mnt, u64 block, const void *buffer, bool readahead) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return EXT4_OK;
    if (data_cache_find_slot(mnt, block)) return EXT4_OK;
    ext4_status_t st = data_cache_install_slot(mnt, block, buffer, false, readahead);
    if (st == EXT4_OK && readahead) ++g_ext4_perf_stats.data_cache_readahead;
    return st;
}

static ext4_status_t data_cache_prepare_slot(ext4_mount_t *mnt, u64 block, bool prefer_clean, ext4_data_cache_slot_t **slot_out) {
    if (slot_out) *slot_out = 0;
    if (!mnt || block >= mnt->blocks_count || !slot_out) return EXT4_ERR_RANGE;
    if (!data_cache_ensure()) return EXT4_ERR_NO_MEMORY;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (!slot) slot = data_cache_lru_slot(mnt, prefer_clean);
    if (!slot) return EXT4_ERR_NO_MEMORY;
    if (slot->valid && slot->dirty) {
        ++g_ext4_perf_stats.data_cache_evictions;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    } else if (slot->valid && !data_cache_slot_matches(slot, mnt, block)) {
        ++g_ext4_perf_stats.data_cache_evictions;
        data_cache_drop_slot(slot);
    }
    *slot_out = slot;
    return EXT4_OK;
}

static void data_cache_readahead(ext4_mount_t *mnt, u64 first_block) {
    if (!mnt || mnt->block_size > MAX_BLOCK_SIZE) return;
    for (u32 i = 0; i < EXT4_DATA_CACHE_READAHEAD; ++i) {
        u64 next = first_block + i;
        if (next >= mnt->blocks_count || ext4_block_is_journal(mnt, next)) break;
        if (data_cache_find_slot(mnt, next) || write_cache_find_slot(mnt, next)) continue;
        ext4_data_cache_slot_t *slot = 0;
        ext4_status_t st = data_cache_prepare_slot(mnt, next, true, &slot);
        if (st != EXT4_OK || !slot) break;
        u64 byte_offset = 0;
        if (!checked_mul_u64(next, mnt->block_size, &byte_offset)) {
            data_cache_drop_slot(slot);
            break;
        }
        ++g_ext4_perf_stats.raw_reads;
        st = read_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, slot->data, (usize)mnt->block_size);
        if (st != EXT4_OK) {
            data_cache_drop_slot(slot);
            break;
        }
        slot->valid = true;
        slot->dirty = false;
        slot->dev = mnt->dev;
        slot->partition_lba = mnt->partition_lba;
        slot->partition_sectors = mnt->partition_sectors;
        slot->block = next;
        slot->block_size = (u32)mnt->block_size;
        slot->readahead = true;
        slot->last_used = data_cache_tick();
        ++g_ext4_perf_stats.data_cache_clean_stores;
        ++g_ext4_perf_stats.data_cache_readahead;
    }
}

