    check(ktest_cleanup_disk_prefix("aurora-htree"), "cleanup old aurora-htree artifacts");
    char htree_dir[96];
    ksnprintf(htree_dir, sizeof(htree_dir), "/disk0/aurora-htree");
    const char *htree_raw_path = ktest_disk_raw_path(htree_dir);
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        (void)vfs_unlink(pbuf);
    }
    (void)vfs_unlink(htree_dir);
    check(vfs_mkdir(htree_dir) == VFS_OK, "VFS mkdir ext4 htree candidate dir");
    bool htree_create_ok = true;
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        char payload = (char)('0' + (i % 10u));
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        if (vfs_create(pbuf, &payload, 1u) != VFS_OK) { htree_create_ok = false; break; }
    }
    check(htree_create_ok, "EXT4 creates enough directory entries to build htree index");
    if (part) {
        ext4_mount_t htree_mnt;
        ext4_inode_disk_t htree_inode;
        u32 htree_ino = 0;
        ext4_fsck_report_t htree_report;
        bool htree_indexed = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &htree_mnt) == EXT4_OK &&
                             ext4_lookup_path(&htree_mnt, htree_raw_path, &htree_inode, &htree_ino) == EXT4_OK &&
                             (htree_inode.i_flags & 0x00001000u) != 0 && htree_inode.i_file_acl_lo != 0 &&
                             ext4_validate_metadata(&htree_mnt, &htree_report) == EXT4_OK && htree_report.htree_dirs >= 1u &&
                             htree_report.htree_entries >= 20u && htree_report.htree_errors == 0;
        check(htree_indexed, "EXT4 maintains persistent htree-indexed directory metadata");
    } else {
        skip("EXT4 maintains persistent htree-indexed directory metadata");
    }
    char htree_child19[128];
    ksnprintf(htree_child19, sizeof(htree_child19), "%s/f19.txt", htree_dir);
    char htree_read = 0;
    got = 0;
    check(vfs_read(htree_child19, 0, &htree_read, 1u, &got) == VFS_OK && got == 1u && htree_read == '9', "EXT4 htree lookup resolves indexed directory entry");
    if (part) {
        ext4_mount_t repair_mnt;
        ext4_inode_disk_t repair_dir;
        u32 repair_dir_ino = 0;
        u8 *htree_block = repair_scratch;
        u8 *htree_original = repair_original;
        u64 htree_first_sector = 0;
        u32 htree_sector_count = 0;
        bool htree_original_valid = false;
        bool repair_ok = htree_block && htree_original && ext4_mount_bounded(dev, part->lba_first, part->sector_count, &repair_mnt) == EXT4_OK &&
                         ext4_lookup_path(&repair_mnt, htree_raw_path, &repair_dir, &repair_dir_ino) == EXT4_OK &&
                         repair_dir_ino != 0 && repair_dir.i_file_acl_lo != 0 && repair_mnt.block_size <= 4096u &&
                         ext4_sync_metadata(&repair_mnt) == EXT4_OK;
        if (repair_ok) {
            u64 index_block = repair_dir.i_file_acl_lo;
            u64 first_sector = part->lba_first + (index_block * repair_mnt.block_size) / BLOCKDEV_SECTOR_SIZE;
            u32 sector_count = (u32)(repair_mnt.block_size / BLOCKDEV_SECTOR_SIZE);
            repair_ok = sector_count != 0 && sector_count <= 4096u / BLOCKDEV_SECTOR_SIZE &&
                        block_read(dev, first_sector, sector_count, htree_block) == BLOCK_OK;
            if (repair_ok) {
                memcpy(htree_original, htree_block, repair_mnt.block_size);
                htree_first_sector = first_sector;
                htree_sector_count = sector_count;
                htree_original_valid = true;
                htree_block[0] ^= 0x5au;
                repair_ok = block_write(dev, first_sector, sector_count, htree_block) == BLOCK_OK;
            }
        }
        ext4_mount_t corrupt_mnt;
        ext4_fsck_report_t corrupt_report;
        ext4_fsck_report_t repair_report;
        char repaired_read = 0;
        usize repaired_got = 0;
        bool repaired = repair_ok &&
                        ext4_mount_bounded(dev, part->lba_first, part->sector_count, &corrupt_mnt) == EXT4_OK &&
                        ext4_validate_metadata(&corrupt_mnt, &corrupt_report) == EXT4_ERR_CORRUPT &&
                        corrupt_report.htree_errors > 0 &&
                        ext4_repair_metadata(&corrupt_mnt, &repair_report) == EXT4_OK &&
                        repair_report.errors == 0 && repair_report.repaired_htree >= 1u &&
                        vfs_read(htree_child19, 0, &repaired_read, 1u, &repaired_got) == VFS_OK &&
                        repaired_got == 1u && repaired_read == '9';
        if (htree_original_valid && !ktest_restore_raw_block(dev, htree_first_sector, htree_sector_count, htree_original)) repaired = false;
        check(repaired, "EXT4 repair-lite rebuilds corrupted htree metadata");
    } else {
        skip("EXT4 repair-lite rebuilds corrupted htree metadata");
    }
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        (void)vfs_unlink(pbuf);
    }
    check(vfs_unlink(htree_dir) == VFS_OK, "VFS unlink empty ext4 htree directory");
