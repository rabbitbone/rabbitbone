static u32 journal_header_crc(rabbitbone_ext4_journal_record_t *jr) {
    u32 saved = jr->header_crc;
    jr->header_crc = 0;
    u32 c = crc32(jr, sizeof(*jr));
    jr->header_crc = saved;
    return c;
}

static bool ext4_block_is_journal(const ext4_mount_t *mnt, u64 block) {
    if (!mnt || !mnt->journal_blocks) return false;
    u64 journal_end = 0;
    if (!checked_add_u64(mnt->journal_first_block, mnt->journal_blocks, &journal_end)) return true;
    if (mnt->journal_first_block >= mnt->blocks_count || journal_end > mnt->blocks_count) return true;
    return block >= mnt->journal_first_block && block < journal_end;
}

static void journal_make_idle_record(ext4_mount_t *mnt, rabbitbone_ext4_journal_record_t *jr) {
    memset(jr, 0, sizeof(*jr));
    jr->magic = RABBITBONE_EXT4_JOURNAL_MAGIC;
    jr->version = 1u;
    jr->state = 0u;
    jr->seq = mnt ? mnt->journal_seq : 0u;
    jr->block_size = mnt ? (u32)mnt->block_size : 0u;
    jr->header_crc = journal_header_crc(jr);
}

static bool journal_record_header_valid(ext4_mount_t *mnt, const rabbitbone_ext4_journal_record_t *jr) {
    if (!mnt || !jr) return false;
    rabbitbone_ext4_journal_record_t copy;
    memcpy(&copy, jr, sizeof(copy));
    u32 want = journal_header_crc(&copy);
    if (jr->magic != RABBITBONE_EXT4_JOURNAL_MAGIC || jr->version != 1u || jr->header_crc != want) return false;
    if (jr->block_size != (u32)mnt->block_size) return false;
    if (jr->state == 0u) return true;
    if (jr->state == 1u && jr->target_block < mnt->blocks_count && !ext4_block_is_journal(mnt, jr->target_block)) return true;
    return false;
}

static ext4_status_t journal_clear(ext4_mount_t *mnt) {
    if (!mnt || !mnt->journal_blocks) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    memset(block, 0, (usize)mnt->block_size);
    rabbitbone_ext4_journal_record_t jr;
    journal_make_idle_record(mnt, &jr);
    memcpy(block, &jr, sizeof(jr));
    ext4_status_t st = raw_write_block(mnt, mnt->journal_first_block, block);
    ext4_tmp_block_free(block);
    return st;
}

static ext4_status_t journal_commit_ordered_block(ext4_mount_t *mnt, u64 target, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (!mnt->journal_ready || ext4_block_is_journal(mnt, target)) return raw_write_block(mnt, target, buffer);
    ++g_ext4_perf_stats.journal_commits;
    if (mnt->journal_blocks < 2u || mnt->block_size > MAX_BLOCK_SIZE) return raw_write_block(mnt, target, buffer);

    rabbitbone_ext4_journal_record_t jr;
    memset(&jr, 0, sizeof(jr));
    jr.magic = RABBITBONE_EXT4_JOURNAL_MAGIC;
    jr.version = 1u;
    jr.state = 1u;
    jr.seq = ++mnt->journal_seq;
    jr.target_block = target;
    jr.block_size = (u32)mnt->block_size;
    jr.payload_crc = crc32(buffer, (usize)mnt->block_size);
    jr.header_crc = journal_header_crc(&jr);

    u8 *header_block = ext4_tmp_block(mnt);
    if (!header_block) return EXT4_ERR_NO_MEMORY;
    memset(header_block, 0, (usize)mnt->block_size);
    memcpy(header_block, &jr, sizeof(jr));
    u64 payload_block = 0;
    if (!checked_add_u64(mnt->journal_first_block, 1u, &payload_block) || payload_block >= mnt->blocks_count) {
        ext4_tmp_block_free(header_block);
        return raw_write_block(mnt, target, buffer);
    }
    ext4_status_t st = raw_write_block(mnt, payload_block, buffer);
    if (st == EXT4_OK) st = raw_write_block(mnt, mnt->journal_first_block, header_block);
    if (st == EXT4_OK) st = raw_write_block(mnt, target, buffer);
    ext4_tmp_block_free(header_block);
    return st;
}

static ext4_status_t write_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    return write_cache_store_block(mnt, block, buffer);
}

static ext4_status_t write_data_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    write_cache_discard_block(mnt, block);
    ++g_ext4_perf_stats.data_writes;
    return data_cache_store_block(mnt, block, buffer);
}

static ext4_status_t read_metadata_bytes(ext4_mount_t *mnt, u64 byte_offset, void *buffer, usize bytes) {
    if (!mnt || (!buffer && bytes)) return EXT4_ERR_IO;
    if (bytes == 0) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *dst = (u8 *)buffer;
    usize done = 0;
    ext4_status_t result = EXT4_OK;
    while (done < bytes) {
        u64 cur = 0;
        if (!checked_add_u64(byte_offset, (u64)done, &cur)) { result = EXT4_ERR_RANGE; goto out; }
        u64 fs_block = cur / mnt->block_size;
        u32 block_off = (u32)(cur % mnt->block_size);
        usize take = mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        ext4_status_t st = read_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(dst + done, block + block_off, take);
        done += take;
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t write_metadata_bytes(ext4_mount_t *mnt, u64 byte_offset, const void *buffer, usize bytes) {
    if (!mnt || (!buffer && bytes)) return EXT4_ERR_IO;
    if (bytes == 0) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    const u8 *src = (const u8 *)buffer;
    usize done = 0;
    ext4_status_t result = EXT4_OK;
    while (done < bytes) {
        u64 cur = 0;
        if (!checked_add_u64(byte_offset, (u64)done, &cur)) { result = EXT4_ERR_RANGE; goto out; }
        u64 fs_block = cur / mnt->block_size;
        u32 block_off = (u32)(cur % mnt->block_size);
        usize take = mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        ext4_status_t st = read_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(block + block_off, src + done, take);
        st = write_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        done += take;
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

