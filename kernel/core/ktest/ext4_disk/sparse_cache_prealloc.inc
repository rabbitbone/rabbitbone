    const char *sparse_path = "/disk0/aurora-sparse.bin";
    (void)vfs_unlink(sparse_path);
    check(vfs_create(sparse_path, 0, 0) == VFS_OK, "VFS create empty ext4 extent file");
    const char tail[] = "tail";
    wrote = 0;
    check(vfs_write(sparse_path, 12288u + 5u, tail, sizeof(tail) - 1u, &wrote) == VFS_OK && wrote == sizeof(tail) - 1u, "VFS sparse ext4 write beyond EOF");
    vfs_stat_t sparse_st;
    check(vfs_stat(sparse_path, &sparse_st) == VFS_OK && sparse_st.size == 12288u + 5u + sizeof(tail) - 1u, "VFS sparse ext4 stat after gap write");
    char zeros[32];
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool zero_gap = vfs_read(sparse_path, 0, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && zero_gap; ++zi) zero_gap = zeros[zi] == 0;
    check(zero_gap, "EXT4 sparse hole reads back as zero-filled data");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(sparse_path, 12288u + 5u, text, sizeof(tail) - 1u, &got) == VFS_OK && got == sizeof(tail) - 1u && memcmp(text, tail, sizeof(tail) - 1u) == 0, "EXT4 sparse tail data survives readback");
    check(vfs_truncate(sparse_path, 1u) == VFS_OK && vfs_stat(sparse_path, &sparse_st) == VFS_OK && sparse_st.size == 1u, "EXT4 sparse extent file truncates smaller");
    check(vfs_unlink(sparse_path) == VFS_OK, "VFS unlink ext4 sparse extent file");

    const char *perf_path = "/disk0/aurora-fullblock-perf.bin";
    (void)vfs_unlink(perf_path);
    if (part) {
        ext4_mount_t perf_mnt_before;
        ext4_perf_stats_t perf_before;
        ext4_perf_stats_t perf_after;
        char *perf_block = 0;
        bool perf_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &perf_mnt_before) == EXT4_OK &&
                       ext4_get_perf_stats(&perf_mnt_before, &perf_before) == EXT4_OK &&
                       perf_mnt_before.block_size <= 4096u &&
                       (perf_block = (char *)kmalloc((usize)perf_mnt_before.block_size)) != 0 &&
                       vfs_create(perf_path, 0, 0) == VFS_OK;
        if (perf_ok) {
            for (usize pi = 0; pi < (usize)perf_mnt_before.block_size; ++pi) perf_block[pi] = (char)('A' + (pi % 23u));
            wrote = 0;
            perf_ok = vfs_write(perf_path, 0, perf_block, (usize)perf_mnt_before.block_size, &wrote) == VFS_OK &&
                      wrote == (usize)perf_mnt_before.block_size &&
                      ext4_get_perf_stats(&perf_mnt_before, &perf_after) == EXT4_OK &&
                      perf_after.zero_block_skips > perf_before.zero_block_skips;
        }
        if (perf_block) kfree(perf_block);
        check(perf_ok, "EXT4 full-block data allocation skips eager zero-write safely");
        check(vfs_unlink(perf_path) == VFS_OK, "VFS unlink ext4 full-block perf file");

        const char *buffered_path = "/disk0/aurora-buffered-data.bin";
        (void)vfs_unlink(buffered_path);
        ext4_mount_t buffered_mnt;
        ext4_perf_stats_t buffered_before;
        ext4_perf_stats_t buffered_after;
        char *buffered_write = 0;
        char *buffered_read = 0;
        bool buffered_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &buffered_mnt) == EXT4_OK &&
                           ext4_get_perf_stats(&buffered_mnt, &buffered_before) == EXT4_OK &&
                           buffered_mnt.block_size <= 4096u &&
                           (buffered_write = (char *)kmalloc((usize)buffered_mnt.block_size)) != 0 &&
                           (buffered_read = (char *)kmalloc((usize)buffered_mnt.block_size)) != 0 &&
                           vfs_create(buffered_path, 0, 0) == VFS_OK;
        if (buffered_ok) {
            for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('a' + (bi % 19u));
            wrote = 0;
            memset(buffered_read, 0, (usize)buffered_mnt.block_size);
            buffered_ok = vfs_write(buffered_path, 0, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                          wrote == (usize)buffered_mnt.block_size &&
                          vfs_read(buffered_path, 0, buffered_read, (usize)buffered_mnt.block_size, &got) == VFS_OK &&
                          got == (usize)buffered_mnt.block_size &&
                          memcmp(buffered_read, buffered_write, (usize)buffered_mnt.block_size) == 0 &&
                          ext4_get_perf_stats(&buffered_mnt, &buffered_after) == EXT4_OK &&
                          buffered_after.data_cache_stores > buffered_before.data_cache_stores &&
                          buffered_after.data_cache_hits > buffered_before.data_cache_hits;
        }
        check(buffered_ok, "EXT4 buffered data cache serves immediate readback before sync");
        check(vfs_unlink(buffered_path) == VFS_OK, "VFS unlink ext4 buffered-data file");

        const char *readahead_path = "/disk0/aurora-readahead-data.bin";
        (void)vfs_unlink(readahead_path);
        ext4_perf_stats_t read_policy_before;
        ext4_perf_stats_t read_policy_after;
        bool read_policy_ok = ext4_get_perf_stats(&buffered_mnt, &read_policy_before) == EXT4_OK &&
                              vfs_create(readahead_path, 0, 0) == VFS_OK;
        if (read_policy_ok && buffered_write) {
            for (u32 ri = 0; ri < 3u && read_policy_ok; ++ri) {
                for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('R' + ri + (bi % 7u));
                wrote = 0;
                read_policy_ok = vfs_write(readahead_path, (u64)ri * buffered_mnt.block_size, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                                 wrote == (usize)buffered_mnt.block_size;
            }
            memset(buffered_read, 0, (usize)buffered_mnt.block_size);
            read_policy_ok = read_policy_ok &&
                             vfs_sync_path(readahead_path) == VFS_OK &&
                             ext4_get_perf_stats(&buffered_mnt, &read_policy_before) == EXT4_OK &&
                             vfs_read(readahead_path, 0, buffered_read, (usize)buffered_mnt.block_size, &got) == VFS_OK &&
                             got == (usize)buffered_mnt.block_size &&
                             ext4_get_perf_stats(&buffered_mnt, &read_policy_after) == EXT4_OK &&
                             read_policy_after.data_cache_clean_stores > read_policy_before.data_cache_clean_stores &&
                             read_policy_after.data_cache_readahead > read_policy_before.data_cache_readahead;
        }
        check(read_policy_ok, "EXT4 data cache read miss installs clean page and read-ahead");
        check(vfs_unlink(readahead_path) == VFS_OK, "VFS unlink ext4 readahead-data file");

        const char *pressure_path = "/disk0/aurora-cache-pressure.bin";
        (void)vfs_unlink(pressure_path);
        ext4_perf_stats_t pressure_before;
        ext4_perf_stats_t pressure_after;
        bool pressure_ok = ext4_get_perf_stats(&buffered_mnt, &pressure_before) == EXT4_OK &&
                           vfs_create(pressure_path, 0, 0) == VFS_OK;
        if (pressure_ok && buffered_write) {
            for (u32 pi = 0; pi < 32u && pressure_ok; ++pi) {
                for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('p' + (pi % 11u));
                wrote = 0;
                pressure_ok = vfs_write(pressure_path, (u64)pi * buffered_mnt.block_size, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                              wrote == (usize)buffered_mnt.block_size;
            }
            pressure_ok = pressure_ok &&
                          ext4_get_perf_stats(&buffered_mnt, &pressure_after) == EXT4_OK &&
                          pressure_after.data_cache_pressure_flushes > pressure_before.data_cache_pressure_flushes &&
                          pressure_after.data_cache_writeback_runs > pressure_before.data_cache_writeback_runs &&
                          vfs_sync_path(pressure_path) == VFS_OK;
        }
        check(pressure_ok, "EXT4 data cache pressure triggers bounded writeback");
        check(vfs_unlink(pressure_path) == VFS_OK, "VFS unlink ext4 cache-pressure file");
        if (buffered_write) kfree(buffered_write);
        if (buffered_read) kfree(buffered_read);

        const char *unwritten_path = "/disk0/aurora-unwritten.bin";
        (void)vfs_unlink(unwritten_path);
        ext4_mount_t unw_mnt;
        ext4_inode_disk_t unw_inode;
        u32 unw_ino = 0;
        ext4_extent_report_t unw_report;
        ext4_perf_stats_t unw_before;
        ext4_perf_stats_t unw_after;
        char unw_read[128];
        bool unw_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &unw_mnt) == EXT4_OK &&
                      ext4_get_perf_stats(&unw_mnt, &unw_before) == EXT4_OK &&
                      unw_mnt.block_size * 3u <= 16384u &&
                      vfs_create(unwritten_path, 0, 0) == VFS_OK &&
                      ext4_preallocate_file_path(&unw_mnt, "/aurora-unwritten.bin", unw_mnt.block_size * 3u) == EXT4_OK &&
                      ext4_lookup_path(&unw_mnt, "/aurora-unwritten.bin", &unw_inode, &unw_ino) == EXT4_OK &&
                      ext4_inspect_inode_extents(&unw_mnt, &unw_inode, &unw_report) == EXT4_OK &&
                      unw_report.unwritten_blocks == 3u && unw_report.unwritten_extents == 1u &&
                      vfs_read(unwritten_path, 0, unw_read, sizeof(unw_read), &got) == VFS_OK && got == sizeof(unw_read);
        for (usize ui = 0; ui < sizeof(unw_read) && unw_ok; ++ui) unw_ok = unw_read[ui] == 0;
        check(unw_ok, "EXT4 unwritten preallocation reads back as zero-filled data");

        char uw = 'Z';
        wrote = 0;
        bool unw_convert_ok = unw_ok &&
                              vfs_write(unwritten_path, unw_mnt.block_size + 17u, &uw, 1u, &wrote) == VFS_OK && wrote == 1u &&
                              ext4_lookup_path(&unw_mnt, "/aurora-unwritten.bin", &unw_inode, &unw_ino) == EXT4_OK &&
                              ext4_inspect_inode_extents(&unw_mnt, &unw_inode, &unw_report) == EXT4_OK &&
                              unw_report.unwritten_blocks == 2u && unw_report.data_blocks == 3u &&
                              ext4_get_perf_stats(&unw_mnt, &unw_after) == EXT4_OK &&
                              unw_after.unwritten_allocations > unw_before.unwritten_allocations &&
                              unw_after.unwritten_conversions > unw_before.unwritten_conversions;
        memset(unw_read, 0x5a, sizeof(unw_read));
        got = 0;
        unw_convert_ok = unw_convert_ok &&
                         vfs_read(unwritten_path, unw_mnt.block_size + 16u, unw_read, 3u, &got) == VFS_OK &&
                         got == 3u && unw_read[0] == 0 && unw_read[1] == 'Z' && unw_read[2] == 0;
        check(unw_convert_ok, "EXT4 converts a written unwritten extent block without leaking stale data");
        check(vfs_truncate(unwritten_path, 0) == VFS_OK && vfs_unlink(unwritten_path) == VFS_OK, "VFS truncate/unlink ext4 unwritten preallocation file");

        const char *vfs_prealloc_path = "/disk0/aurora-vfs-prealloc.bin";
        (void)vfs_unlink(vfs_prealloc_path);
        bool vfs_prealloc_ok = vfs_create(vfs_prealloc_path, 0, 0) == VFS_OK &&
                               vfs_preallocate(vfs_prealloc_path, unw_mnt.block_size * 5u + 19u) == VFS_OK;
        vfs_stat_t vfs_prealloc_st;
        memset(&vfs_prealloc_st, 0, sizeof(vfs_prealloc_st));
        u8 vfs_prealloc_buf[32];
        memset(vfs_prealloc_buf, 0xa5, sizeof(vfs_prealloc_buf));
        usize vfs_prealloc_got = 0;
        vfs_prealloc_ok = vfs_prealloc_ok && vfs_stat(vfs_prealloc_path, &vfs_prealloc_st) == VFS_OK &&
                          vfs_prealloc_st.size == unw_mnt.block_size * 5u + 19u &&
                          vfs_read(vfs_prealloc_path, unw_mnt.block_size * 4u + 7u, vfs_prealloc_buf, sizeof(vfs_prealloc_buf), &vfs_prealloc_got) == VFS_OK &&
                          vfs_prealloc_got == sizeof(vfs_prealloc_buf);
        bool vfs_prealloc_zero = vfs_prealloc_ok;
        for (usize vi = 0; vi < sizeof(vfs_prealloc_buf); ++vi) if (vfs_prealloc_buf[vi] != 0) vfs_prealloc_zero = false;
        check(vfs_prealloc_ok && vfs_prealloc_zero, "VFS preallocate exposes zero-readable EXT4 unwritten extents");
        const char vfs_prealloc_payload[] = "prealloc-write";
        usize vfs_prealloc_written = 0;
        memset(vfs_prealloc_buf, 0, sizeof(vfs_prealloc_buf));
        bool vfs_prealloc_convert = vfs_write(vfs_prealloc_path, unw_mnt.block_size * 2u + 3u, vfs_prealloc_payload, sizeof(vfs_prealloc_payload) - 1u, &vfs_prealloc_written) == VFS_OK &&
                                    vfs_prealloc_written == sizeof(vfs_prealloc_payload) - 1u &&
                                    vfs_read(vfs_prealloc_path, unw_mnt.block_size * 2u + 3u, vfs_prealloc_buf, sizeof(vfs_prealloc_payload) - 1u, &vfs_prealloc_got) == VFS_OK &&
                                    vfs_prealloc_got == sizeof(vfs_prealloc_payload) - 1u &&
                                    memcmp(vfs_prealloc_buf, vfs_prealloc_payload, sizeof(vfs_prealloc_payload) - 1u) == 0;
        check(vfs_prealloc_convert, "VFS write converts preallocated EXT4 unwritten blocks");
        check(vfs_truncate(vfs_prealloc_path, 0) == VFS_OK && vfs_unlink(vfs_prealloc_path) == VFS_OK, "VFS truncate/unlink ext4 preallocated file");
    } else {
        skip("EXT4 full-block data allocation skips eager zero-write safely");
        skip("VFS unlink ext4 full-block perf file");
        skip("EXT4 buffered data cache serves immediate readback before sync");
        skip("VFS unlink ext4 buffered-data file");
        skip("EXT4 data cache read miss installs clean page and read-ahead");
        skip("VFS unlink ext4 readahead-data file");
        skip("EXT4 data cache pressure triggers bounded writeback");
        skip("VFS unlink ext4 cache-pressure file");
        skip("EXT4 unwritten preallocation reads back as zero-filled data");
        skip("EXT4 converts a written unwritten extent block without leaking stale data");
        skip("VFS truncate/unlink ext4 unwritten preallocation file");
        skip("VFS preallocate exposes zero-readable EXT4 unwritten extents");
        skip("VFS write converts preallocated EXT4 unwritten blocks");
        skip("VFS truncate/unlink ext4 preallocated file");
    }
