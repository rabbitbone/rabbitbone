static void ext4_tmp_block_free(u8 *p) {
    if (p) kfree(p);
}

static bool write_cache_ensure(void) {
    if (g_ext4_write_cache && g_ext4_write_cache_slots) return true;
    g_ext4_write_cache = (ext4_write_cache_slot_t *)kcalloc(EXT4_WRITE_CACHE_SLOTS, sizeof(ext4_write_cache_slot_t));
    if (!g_ext4_write_cache) {
        g_ext4_write_cache_slots = 0;
        return false;
    }
    g_ext4_write_cache_slots = EXT4_WRITE_CACHE_SLOTS;
    return true;
}

static bool write_cache_slot_matches(const ext4_write_cache_slot_t *slot, const ext4_mount_t *mnt, u64 block) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size && slot->block == block;
}

static ext4_write_cache_slot_t *write_cache_find_slot(const ext4_mount_t *mnt, u64 block) {
    if (!g_ext4_write_cache || !g_ext4_write_cache_slots) return 0;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        if (write_cache_slot_matches(&g_ext4_write_cache[i], mnt, block)) return &g_ext4_write_cache[i];
    }
    return 0;
}

static void write_cache_discard_block(const ext4_mount_t *mnt, u64 block) {
    if (!mnt || !g_ext4_write_cache || !g_ext4_write_cache_slots) return;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (write_cache_slot_matches(slot, mnt, block)) {
            slot->dirty = false;
            slot->valid = false;
            ++g_ext4_perf_stats.cache_invalidations;
        }
    }
}

