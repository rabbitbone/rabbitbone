    check(ktest_cleanup_disk_prefix("rabbitbone-extent-split"), "cleanup old rabbitbone-extent-split artifacts");
    char split_path[96];
    ksnprintf(split_path, sizeof(split_path), "/disk0/rabbitbone-extent-split.bin");
    const char *split_raw = ktest_disk_raw_path(split_path);
    (void)vfs_unlink(split_path);
    u64 split_free_before = 0;
    bool split_baseline_ok = false;
    if (part) {
        ext4_mount_t before_mnt;
        ext4_fsck_report_t before_report;
        split_baseline_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &before_mnt) == EXT4_OK &&
                            ext4_validate_metadata(&before_mnt, &before_report) == EXT4_OK &&
                            before_report.errors == 0;
        split_free_before = before_report.sb_free_blocks;
    }
    check(vfs_create(split_path, 0, 0) == VFS_OK, "VFS create ext4 extent leaf-split stress file");
    vfs_node_ref_t split_ref;
    bool split_ref_ok = vfs_get_ref(split_path, &split_ref) == VFS_OK;
    check(split_ref_ok, "VFS captures ext4 leaf-split inode ref");
    bool split_writes_ok = split_ref_ok;
    enum { EXT4_SPLIT_KTEST_EXTENTS = 96u };
    for (u32 i = 0; split_writes_ok && i < EXT4_SPLIT_KTEST_EXTENTS; ++i) {
        char ch = (char)('a' + (i % 26u));
        wrote = 0;
        if (vfs_write_ref(&split_ref, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) {
            split_writes_ok = false;
            break;
        }
    }
    check(split_writes_ok, "EXT4 writes sparse extents and splits indexed leaves");
    if (part) {
        ext4_mount_t split_mnt;
        ext4_inode_disk_t split_inode;
        u32 split_ino = 0;
        ext4_extent_report_t split_extents;
        bool split_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &split_mnt) == EXT4_OK &&
                          ext4_lookup_path(&split_mnt, split_raw, &split_inode, &split_ino) == EXT4_OK &&
                          split_ino != 0 && ext4_inspect_inode_extents(&split_mnt, &split_inode, &split_extents) == EXT4_OK &&
                          split_extents.uses_extents && split_extents.depth == 1u &&
                          split_extents.root_entries >= 2u && split_extents.leaf_nodes >= 2u &&
                          split_extents.extent_entries == EXT4_SPLIT_KTEST_EXTENTS && split_extents.data_blocks == EXT4_SPLIT_KTEST_EXTENTS &&
                          split_extents.metadata_blocks >= 2u && split_extents.errors == 0;
        check(split_tree, "EXT4 indexed extent root tracks multiple leaf blocks after split");
    } else {
        skip("EXT4 indexed extent root tracks multiple leaf blocks after split");
    }
    bool split_read_ok = true;
    for (u32 i = 0; i < EXT4_SPLIT_KTEST_EXTENTS; i += 17u) {
        char ch = 0;
        got = 0;
        if (vfs_read(split_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('a' + (i % 26u))) {
            split_read_ok = false;
            break;
        }
    }
    check(split_read_ok, "EXT4 multi-leaf indexed extents read back sparse samples");
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool split_hole_zero = vfs_read(split_path, 4096u, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && split_hole_zero; ++zi) split_hole_zero = zeros[zi] == 0;
    check(split_hole_zero, "EXT4 multi-leaf indexed extent holes remain zero-filled");
    check(vfs_truncate(split_path, 17u) == VFS_OK && vfs_stat(split_path, &indexed_st) == VFS_OK && indexed_st.size == 17u, "EXT4 multi-leaf indexed extent file truncates across leaves");
    if (part) {
        ext4_mount_t split_demote_mnt;
        ext4_inode_disk_t split_demote_inode;
        u32 split_demote_ino = 0;
        ext4_extent_report_t split_demote_extents;
        bool split_demoted_inline = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &split_demote_mnt) == EXT4_OK &&
                                    ext4_lookup_path(&split_demote_mnt, split_raw, &split_demote_inode, &split_demote_ino) == EXT4_OK &&
                                    split_demote_ino != 0 &&
                                    ext4_inspect_inode_extents(&split_demote_mnt, &split_demote_inode, &split_demote_extents) == EXT4_OK &&
                                    split_demote_extents.uses_extents && split_demote_extents.depth == 0u &&
                                    split_demote_extents.root_entries == 1u && split_demote_extents.leaf_nodes == 0u &&
                                    split_demote_extents.metadata_blocks == 0u && split_demote_extents.data_blocks == 1u &&
                                    split_demote_extents.errors == 0;
        check(split_demoted_inline, "EXT4 demotes multi-leaf file to inline extents after deep truncate");
    } else {
        skip("EXT4 demotes multi-leaf file to inline extents after deep truncate");
    }
    check(vfs_unlink(split_path) == VFS_OK, "VFS unlink ext4 multi-leaf indexed extent file");
    bool split_release_no_leak = false;
    if (part && split_baseline_ok) {
        ext4_mount_t split_after_mnt;
        ext4_fsck_report_t split_after_report;
        split_release_no_leak = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &split_after_mnt) == EXT4_OK &&
                                ext4_validate_metadata(&split_after_mnt, &split_after_report) == EXT4_OK &&
                                split_after_report.errors == 0 && split_after_report.sb_free_blocks == split_free_before;
        check(split_release_no_leak, "EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    } else {
        skip("EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    }

    check(ktest_cleanup_disk_prefix("rabbitbone-depth2-extents"), "cleanup old rabbitbone-depth2-extents artifacts");
    char deep_path[96];
    ksnprintf(deep_path, sizeof(deep_path), "/disk0/rabbitbone-depth2-extents.bin");
    const char *deep_raw = ktest_disk_raw_path(deep_path);
    (void)vfs_unlink(deep_path);
    check(vfs_create(deep_path, 0, 0) == VFS_OK, "VFS create ext4 depth-2 extent stress file");
    vfs_node_ref_t deep_ref;
    bool deep_ref_ok = vfs_get_ref(deep_path, &deep_ref) == VFS_OK;
    check(deep_ref_ok, "VFS captures ext4 depth-2 inode ref");
    bool deep_writes_ok = deep_ref_ok;
    enum { EXT4_DEPTH2_KTEST_EXTENTS = 360u };
    for (u32 i = 0; deep_writes_ok && i < EXT4_DEPTH2_KTEST_EXTENTS; ++i) {
        char ch = (char)('A' + (i % 26u));
        wrote = 0;
        if (vfs_write_ref(&deep_ref, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) {
            deep_writes_ok = false;
            break;
        }
    }
    check(deep_writes_ok, "EXT4 writes enough sparse extents to grow past root leaf-index capacity");
    if (part) {
        ext4_mount_t deep_mnt;
        ext4_inode_disk_t deep_inode;
        u32 deep_ino = 0;
        ext4_extent_report_t deep_extents;
        bool deep_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &deep_mnt) == EXT4_OK &&
                         ext4_lookup_path(&deep_mnt, deep_raw, &deep_inode, &deep_ino) == EXT4_OK &&
                         deep_ino != 0 && ext4_inspect_inode_extents(&deep_mnt, &deep_inode, &deep_extents) == EXT4_OK &&
                         deep_extents.uses_extents && deep_extents.depth == 2u && deep_extents.index_nodes >= 1u &&
                         deep_extents.leaf_nodes >= 5u && deep_extents.extent_entries == EXT4_DEPTH2_KTEST_EXTENTS &&
                         deep_extents.data_blocks == EXT4_DEPTH2_KTEST_EXTENTS && deep_extents.errors == 0;
        check(deep_tree, "EXT4 supports depth > 1 indexed extent tree");
    } else {
        skip("EXT4 supports depth > 1 indexed extent tree");
    }
    bool deep_read_ok = true;
    for (u32 i = 0; i < EXT4_DEPTH2_KTEST_EXTENTS; i += 42u) {
        char ch = 0;
        got = 0;
        if (vfs_read(deep_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('A' + (i % 26u))) {
            deep_read_ok = false;
            break;
        }
    }
    check(deep_read_ok, "EXT4 depth-2 extent tree reads back sparse samples");
    check(vfs_truncate(deep_path, 9u) == VFS_OK, "EXT4 depth-2 extent file truncates through internal index nodes");
    if (part) {
        ext4_mount_t deep_demote_mnt;
        ext4_inode_disk_t deep_demote_inode;
        u32 deep_demote_ino = 0;
        ext4_extent_report_t deep_demote_extents;
        bool deep_demoted = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &deep_demote_mnt) == EXT4_OK &&
                            ext4_lookup_path(&deep_demote_mnt, deep_raw, &deep_demote_inode, &deep_demote_ino) == EXT4_OK &&
                            deep_demote_ino != 0 && ext4_inspect_inode_extents(&deep_demote_mnt, &deep_demote_inode, &deep_demote_extents) == EXT4_OK &&
                            deep_demote_extents.uses_extents && deep_demote_extents.depth == 0u && deep_demote_extents.data_blocks == 1u &&
                            deep_demote_extents.metadata_blocks == 0u && deep_demote_extents.errors == 0;
        check(deep_demoted, "EXT4 demotes depth-2 extent file after deep truncate");
    } else {
        skip("EXT4 demotes depth-2 extent file after deep truncate");
    }
    check(vfs_unlink(deep_path) == VFS_OK, "VFS unlink ext4 depth-2 extent file");
