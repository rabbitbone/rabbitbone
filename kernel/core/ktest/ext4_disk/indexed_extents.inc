    const char *indexed_path = "/disk0/aurora-indexed-extents.bin";
    (void)vfs_unlink(indexed_path);
    check(vfs_create(indexed_path, 0, 0) == VFS_OK, "VFS create empty ext4 indexed-extent candidate");
    bool indexed_writes_ok = true;
    for (u32 i = 0; i < 5u; ++i) {
        char ch = (char)('A' + i);
        wrote = 0;
        if (vfs_write(indexed_path, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) indexed_writes_ok = false;
    }
    check(indexed_writes_ok, "EXT4 writes five discontiguous blocks into one file");
    vfs_stat_t indexed_st;
    check(vfs_stat(indexed_path, &indexed_st) == VFS_OK && indexed_st.size == 32769u, "EXT4 indexed extent candidate has sparse final size");
    if (part) {
        ext4_mount_t indexed_mnt;
        ext4_inode_disk_t indexed_inode;
        u32 indexed_ino = 0;
        bool indexed_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &indexed_mnt) == EXT4_OK &&
                            ext4_lookup_path(&indexed_mnt, "/aurora-indexed-extents.bin", &indexed_inode, &indexed_ino) == EXT4_OK &&
                            indexed_ino != 0 && ext4_inode_uses_extents(&indexed_inode) &&
                            ext4_inode_extent_depth(&indexed_inode) == 1u &&
                            ext4_inode_extent_root_entries(&indexed_inode) == 1u;
        check(indexed_tree, "EXT4 promotes overflowed inline extents into indexed leaf tree");
    } else {
        skip("EXT4 promotes overflowed inline extents into indexed leaf tree");
    }
    bool indexed_read_ok = true;
    for (u32 i = 0; i < 5u; ++i) {
        char ch = 0;
        got = 0;
        if (vfs_read(indexed_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('A' + i)) indexed_read_ok = false;
    }
    check(indexed_read_ok, "EXT4 indexed extent data survives sparse readback");
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool indexed_hole_zero = vfs_read(indexed_path, 4096u, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && indexed_hole_zero; ++zi) indexed_hole_zero = zeros[zi] == 0;
    check(indexed_hole_zero, "EXT4 indexed extent hole reads as zero-filled data");
    check(vfs_truncate(indexed_path, 1u) == VFS_OK && vfs_stat(indexed_path, &indexed_st) == VFS_OK && indexed_st.size == 1u, "EXT4 indexed extent file truncates through leaf entries");
    if (part) {
        ext4_mount_t indexed_demote_mnt;
        ext4_inode_disk_t indexed_demote_inode;
        u32 indexed_demote_ino = 0;
        ext4_extent_report_t indexed_demote_extents;
        bool demoted_inline = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &indexed_demote_mnt) == EXT4_OK &&
                              ext4_lookup_path(&indexed_demote_mnt, "/aurora-indexed-extents.bin", &indexed_demote_inode, &indexed_demote_ino) == EXT4_OK &&
                              indexed_demote_ino != 0 &&
                              ext4_inspect_inode_extents(&indexed_demote_mnt, &indexed_demote_inode, &indexed_demote_extents) == EXT4_OK &&
                              indexed_demote_extents.uses_extents && indexed_demote_extents.depth == 0u &&
                              indexed_demote_extents.root_entries == 1u && indexed_demote_extents.leaf_nodes == 0u &&
                              indexed_demote_extents.metadata_blocks == 0u && indexed_demote_extents.data_blocks == 1u &&
                              indexed_demote_extents.errors == 0;
        check(demoted_inline, "EXT4 demotes small truncated indexed extent file back to inline extents");
    } else {
        skip("EXT4 demotes small truncated indexed extent file back to inline extents");
    }
    check(vfs_unlink(indexed_path) == VFS_OK, "VFS unlink ext4 indexed extent file");

