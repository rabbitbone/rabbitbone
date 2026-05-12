static void inode_add_blocks(ext4_mount_t *mnt, ext4_inode_disk_t *inode, i32 blocks) {
    if (!mnt || !inode) return;
    u32 sectors = (u32)(mnt->block_size / 512u);
    u32 count = blocks >= 0 ? (u32)blocks : (u32)(-blocks);
    u32 delta = 0;
    if (__builtin_mul_overflow(sectors, count, &delta)) {
        inode->i_blocks_lo = blocks >= 0 ? 0xffffffffu : 0u;
        return;
    }
    if (blocks >= 0) {
        u32 out = 0;
        if (__builtin_add_overflow(inode->i_blocks_lo, delta, &out)) out = 0xffffffffu;
        inode->i_blocks_lo = out;
    } else {
        inode->i_blocks_lo = inode->i_blocks_lo > delta ? inode->i_blocks_lo - delta : 0;
    }
}

static u64 allocator_first_data_block(const ext4_mount_t *mnt) {
    if (!mnt) return 0;
    u64 first = le32(mnt->sb.s_first_data_block);
    if (first >= mnt->blocks_count) return 0;
    return first;
}

static u64 allocator_normalize_goal(ext4_mount_t *mnt, u64 goal) {
    u64 first = allocator_first_data_block(mnt);
    if (goal < first || goal >= mnt->blocks_count || ext4_block_is_journal(mnt, goal)) {
        goal = mnt->allocator_next_block;
    }
    if (goal < first || goal >= mnt->blocks_count || ext4_block_is_journal(mnt, goal)) goal = first;
    return goal;
}

static void allocator_note_success(ext4_mount_t *mnt, u64 block) {
    if (!mnt) return;
    u64 first = allocator_first_data_block(mnt);
    u64 next = block + 1u;
    if (next >= mnt->blocks_count || ext4_block_is_journal(mnt, next)) next = first;
    mnt->allocator_next_block = next;
}

static ext4_status_t alloc_block_goal(ext4_mount_t *mnt, u64 goal, bool zero_fill, u64 *block_out) {
    if (!mnt || !block_out || mnt->blocks_per_group == 0 || mnt->group_count == 0) return EXT4_ERR_RANGE;
    goal = allocator_normalize_goal(mnt, goal);
    u32 start_group = (u32)(goal / mnt->blocks_per_group);
    u32 start_bit = (u32)(goal % mnt->blocks_per_group);
    u64 first_data = allocator_first_data_block(mnt);
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t final = EXT4_ERR_NO_MEMORY;
    for (u32 pass = 0; pass < mnt->group_count; ++pass) {
        u32 group = start_group + pass;
        if (group >= mnt->group_count) {
            group -= mnt->group_count;
            ++g_ext4_perf_stats.allocator_wraps;
        }
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { final = st; goto out; }
        if (gd_free_blocks(&gd) == 0) continue;
        u64 bitmap_block = group_block_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) { final = st; goto out; }
        u32 valid = group_valid_blocks(mnt, group);
        if (valid == 0) continue;
        u32 bit0 = (group == start_group) ? start_bit : 0u;
        for (u32 scan = 0; scan < valid; ++scan) {
            u32 bit = bit0 + scan;
            if (bit >= valid) bit -= valid;
            ++g_ext4_perf_stats.allocator_scans;
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < first_data || abs >= mnt->blocks_count) continue;
            if (ext4_block_is_journal(mnt, abs)) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) { final = st; goto out; }
            u32 gfree = gd_free_blocks(&gd);
            u64 sfree = sb_free_blocks(mnt);
            if (gfree) gd_set_free_blocks(&gd, gfree - 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) {
                bitmap_clear8(bm, bit);
                (void)write_block(mnt, bitmap_block, bm);
                final = st;
                goto out;
            }
            if (sfree) sb_set_free_blocks(mnt, sfree - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) {
                if (sfree) sb_set_free_blocks(mnt, sfree);
                bitmap_clear8(bm, bit);
                (void)write_block(mnt, bitmap_block, bm);
                gd_set_free_blocks(&gd, gfree);
                (void)write_group_desc(mnt, group, &gd);
                final = st;
                goto out;
            }
            if (zero_fill) {
                u8 *zero = ext4_tmp_block(mnt);
                if (!zero) { final = EXT4_ERR_NO_MEMORY; goto out; }
                memset(zero, 0, (usize)mnt->block_size);
                st = write_data_block(mnt, abs, zero);
                ext4_tmp_block_free(zero);
                if (st != EXT4_OK) { final = st; goto out; }
                ++g_ext4_perf_stats.zero_block_writes;
            } else {
                ++g_ext4_perf_stats.zero_block_skips;
            }
            allocator_note_success(mnt, abs);
            *block_out = abs;
            final = EXT4_OK;
            goto out;
        }
    }
out:
    ext4_tmp_block_free(bm);
    return final;
}

static ext4_status_t alloc_block(ext4_mount_t *mnt, u64 *block_out) {
    u64 goal = mnt ? mnt->allocator_next_block : 0;
    return alloc_block_goal(mnt, goal, true, block_out);
}

static ext4_status_t alloc_data_block(ext4_mount_t *mnt, u64 goal, bool zero_fill, u64 *block_out) {
    return alloc_block_goal(mnt, goal, zero_fill, block_out);
}

static bool ext4_block_is_known_metadata(ext4_mount_t *mnt, u64 block, const ext4_group_desc_disk_t *block_group_gd) {
    if (!mnt || !block_group_gd) return true;
    u64 first = allocator_first_data_block(mnt);
    if (block < first) return true;
    if (ext4_block_is_journal(mnt, block)) return true;

    u64 super_block = (mnt->block_size == 1024u) ? 1u : 0u;
    if (block == super_block) return true;
    u64 gdt_first = group_desc_table_block(mnt);
    u64 gdt_bytes = 0;
    if (!checked_mul_u64((u64)mnt->group_count, (u64)mnt->group_desc_size, &gdt_bytes)) return true;
    u64 gdt_blocks = div_round_up_u64(gdt_bytes, mnt->block_size);
    if (block >= gdt_first && block < gdt_first + gdt_blocks) return true;

    if (block == group_block_bitmap(block_group_gd)) return true;
    if (block == group_inode_bitmap(block_group_gd)) return true;
    u64 inode_table = group_inode_table(block_group_gd);
    u64 table_bytes = 0;
    if (!checked_mul_u64((u64)mnt->inodes_per_group, (u64)mnt->inode_size, &table_bytes)) return true;
    u64 table_blocks = div_round_up_u64(table_bytes, mnt->block_size);
    if (block >= inode_table && block < inode_table + table_blocks) return true;
    return false;
}

static ext4_status_t free_block(ext4_mount_t *mnt, u64 block) {
    if (!mnt || mnt->blocks_per_group == 0 || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    if (group >= mnt->group_count) return EXT4_ERR_RANGE;
    u32 bit = (u32)(block % mnt->blocks_per_group);
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    if (ext4_block_is_known_metadata(mnt, block, &gd)) return EXT4_ERR_CORRUPT;

    write_cache_discard_block(mnt, block);
    data_cache_discard_block(mnt, block);
    u64 bitmap_block = group_block_bitmap(&gd);
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, bitmap_block, bm);
    if (st != EXT4_OK) goto out;
    if (!bitmap_get(bm, bit)) { st = EXT4_ERR_CORRUPT; goto out; }

    u32 old_gfree = gd_free_blocks(&gd);
    u64 old_sfree = sb_free_blocks(mnt);
    bitmap_clear8(bm, bit);
    st = write_block(mnt, bitmap_block, bm);
    if (st != EXT4_OK) goto out;
    gd_set_free_blocks(&gd, old_gfree + 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) {
        bitmap_set8(bm, bit);
        (void)write_block(mnt, bitmap_block, bm);
        goto out;
    }
    sb_set_free_blocks(mnt, old_sfree + 1u);
    st = write_super(mnt);
    if (st != EXT4_OK) {
        sb_set_free_blocks(mnt, old_sfree);
        bitmap_set8(bm, bit);
        (void)write_block(mnt, bitmap_block, bm);
        gd_set_free_blocks(&gd, old_gfree);
        (void)write_group_desc(mnt, group, &gd);
    }
out:
    ext4_tmp_block_free(bm);
    return st;
}

static ext4_status_t deferred_free_add(ext4_deferred_free_t *df, u64 block) {
    if (!df) return EXT4_ERR_RANGE;
    if (df->count >= EXT4_DEFERRED_FREE_MAX) return EXT4_ERR_UNSUPPORTED;
    df->blocks[df->count++] = block;
    return EXT4_OK;
}

static ext4_status_t deferred_free_run(ext4_mount_t *mnt, ext4_deferred_free_t *df) {
    if (!df) return EXT4_OK;
    for (u16 i = 0; i < df->count; ++i) {
        ext4_status_t st = free_block(mnt, df->blocks[i]);
        if (st != EXT4_OK) return st;
    }
    df->count = 0;
    return EXT4_OK;
}


static ext4_status_t reserve_block_if_free(ext4_mount_t *mnt, u64 block) {
    if (!mnt || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    if (bitmap_get(bm, bit)) { st = EXT4_OK; goto out; }
    u64 bitmap_block = group_block_bitmap(&gd);
    u32 gfree = gd_free_blocks(&gd);
    u64 sfree = sb_free_blocks(mnt);
    bitmap_set8(bm, bit);
    st = raw_write_block(mnt, bitmap_block, bm);
    if (st != EXT4_OK) goto out;
    if (gfree) gd_set_free_blocks(&gd, gfree - 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) {
        bitmap_clear8(bm, bit);
        (void)raw_write_block(mnt, bitmap_block, bm);
        goto out;
    }
    if (sfree) sb_set_free_blocks(mnt, sfree - 1u);
    st = write_super(mnt);
    if (st != EXT4_OK) {
        if (sfree) sb_set_free_blocks(mnt, sfree);
        bitmap_clear8(bm, bit);
        (void)raw_write_block(mnt, bitmap_block, bm);
        gd_set_free_blocks(&gd, gfree);
        (void)write_group_desc(mnt, group, &gd);
    }
out:
    ext4_tmp_block_free(bm);
    return st;
}

ext4_status_t ext4_recover(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (mnt->journal_blocks >= 2u && mnt->block_size <= MAX_BLOCK_SIZE) {
        u8 *header_block = ext4_tmp_block(mnt);
        if (!header_block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, mnt->journal_first_block, header_block);
        if (st == EXT4_OK) {
            aurora_ext4_journal_record_t jr;
            memcpy(&jr, header_block, sizeof(jr));
            u32 want = journal_header_crc(&jr);
            if (jr.magic == AURORA_EXT4_JOURNAL_MAGIC && jr.version == 1u && jr.state == 1u &&
                jr.block_size == (u32)mnt->block_size && jr.header_crc == want && jr.target_block < mnt->blocks_count &&
                !ext4_block_is_journal(mnt, jr.target_block)) {
                u8 *payload = ext4_tmp_block(mnt);
                if (!payload) { ext4_tmp_block_free(header_block); return EXT4_ERR_NO_MEMORY; }
                st = read_block(mnt, mnt->journal_first_block + 1u, payload);
                if (st == EXT4_OK && crc32(payload, (usize)mnt->block_size) == jr.payload_crc) {
                    st = raw_write_block(mnt, jr.target_block, payload);
                    if (st == EXT4_OK) {
                        ++mnt->recovered_journal_replays;
                        if (report) ++report->journal_replays;
                    }
                }
                ext4_tmp_block_free(payload);
                if (st != EXT4_OK) { ext4_tmp_block_free(header_block); return st; }
            }
        }
        ext4_tmp_block_free(header_block);
        st = journal_clear(mnt);
        if (st != EXT4_OK) return st;
    }
    ext4_status_t ost = ext4_recover_orphans(mnt, report);
    if (ost != EXT4_OK) return ost;
    return EXT4_OK;
}

static ext4_status_t alloc_inode(ext4_mount_t *mnt, bool dir, u32 *ino_out) {
    if (!mnt || !ino_out) return EXT4_ERR_RANGE;
    u32 first = le32(mnt->sb.s_first_ino);
    if (first == 0) first = 11;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_NO_MEMORY;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        if (gd_free_inodes(&gd) == 0) continue;
        u64 bitmap_block = group_inode_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        for (u32 bit = 0; bit < mnt->inodes_per_group; ++bit) {
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino < first || ino > mnt->inodes_count) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) { result = st; goto out; }
            gd_set_free_inodes(&gd, gd_free_inodes(&gd) - 1u);
            if (dir) gd_set_used_dirs(&gd, gd_used_dirs(&gd) + 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) { result = st; goto out; }
            sb_set_free_inodes(mnt, sb_free_inodes(mnt) - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) { result = st; goto out; }
            *ino_out = ino;
            result = EXT4_OK;
            goto out;
        }
    }
out:
    ext4_tmp_block_free(bm);
    return result;
}

static ext4_status_t free_inode(ext4_mount_t *mnt, u32 ino, bool dir) {
    if (!mnt || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1u;
    u32 group = idx / mnt->inodes_per_group;
    u32 bit = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    if (!bitmap_get(bm, bit)) { st = EXT4_ERR_CORRUPT; goto out; }
    bitmap_clear8(bm, bit);
    st = write_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    gd_set_free_inodes(&gd, gd_free_inodes(&gd) + 1u);
    if (dir && gd_used_dirs(&gd)) gd_set_used_dirs(&gd, gd_used_dirs(&gd) - 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) goto out;
    sb_set_free_inodes(mnt, sb_free_inodes(mnt) + 1u);
    st = write_super(mnt);
out:
    ext4_tmp_block_free(bm);
    return st;
}

