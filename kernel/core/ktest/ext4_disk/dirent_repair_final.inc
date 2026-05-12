    check(ktest_cleanup_disk_prefix("aurora-dirent-repair"), "cleanup old aurora-dirent-repair artifacts");
    char dirent_dir[96];
    char dirent_child[128];
    ksnprintf(dirent_dir, sizeof(dirent_dir), "/disk0/aurora-dirent-repair");
    ksnprintf(dirent_child, sizeof(dirent_child), "%s/file.txt", dirent_dir);
    const char *dirent_raw = ktest_disk_raw_path(dirent_dir);
    (void)vfs_unlink(dirent_child);
    (void)vfs_unlink(dirent_dir);
    check(vfs_mkdir(dirent_dir) == VFS_OK && vfs_create(dirent_child, "R", 1u) == VFS_OK, "VFS prepares ext4 dirent repair target");
    if (part) {
        typedef struct raw_dirent_for_test { u32 inode; u16 rec_len; u8 name_len; u8 file_type; char name[]; } raw_dirent_for_test_t;
        ext4_mount_t dirent_mnt;
        ext4_inode_disk_t dirent_inode;
        u32 dirent_ino = 0;
        u8 *raw_dir_block = repair_scratch;
        u8 *raw_dir_original = repair_original;
        u64 raw_dir_first_sector = 0;
        u32 raw_dir_sector_count = 0;
        bool raw_dir_original_valid = false;
        bool dirent_repair_ok = raw_dir_block && raw_dir_original && ext4_mount_bounded(dev, part->lba_first, part->sector_count, &dirent_mnt) == EXT4_OK &&
                                ext4_lookup_path(&dirent_mnt, dirent_raw, &dirent_inode, &dirent_ino) == EXT4_OK &&
                                dirent_ino != 0 && dirent_mnt.block_size <= 4096u &&
                                ext4_sync_metadata(&dirent_mnt) == EXT4_OK;
        if (dirent_repair_ok) {
            const u16 *ew = (const u16 *)dirent_inode.i_block;
            const u32 *ed = (const u32 *)dirent_inode.i_block;
            u64 phys = ((u64)ew[9] << 32) | ed[5];
            u64 first_sector = part->lba_first + (phys * dirent_mnt.block_size) / BLOCKDEV_SECTOR_SIZE;
            u32 sector_count = (u32)(dirent_mnt.block_size / BLOCKDEV_SECTOR_SIZE);
            dirent_repair_ok = ew[0] == EXT4_EXTENT_MAGIC && ew[3] == 0 && phys != 0 && sector_count != 0 &&
                                block_read(dev, first_sector, sector_count, raw_dir_block) == BLOCK_OK;
            if (dirent_repair_ok) {
                memcpy(raw_dir_original, raw_dir_block, dirent_mnt.block_size);
                raw_dir_first_sector = first_sector;
                raw_dir_sector_count = sector_count;
                raw_dir_original_valid = true;
                bool corrupted = false;
                usize ppos = 0;
                while (ppos + 8u <= dirent_mnt.block_size) {
                    raw_dirent_for_test_t *de = (raw_dirent_for_test_t *)(raw_dir_block + ppos);
                    u16 rec_len = de->rec_len;
                    if (rec_len < 8u || ppos + rec_len > dirent_mnt.block_size) break;
                    if (de->inode && de->name_len == 8u && memcmp(de->name, "file.txt", 8u) == 0) {
                        u16 used = (u16)((8u + de->name_len + 3u) & ~3u);
                        if (rec_len >= used + 8u) {
                            de->rec_len = used;
                            raw_dirent_for_test_t *free_de = (raw_dirent_for_test_t *)(raw_dir_block + ppos + used);
                            memset(free_de, 0, rec_len - used);
                            free_de->rec_len = (u16)(rec_len - used);
                            free_de->name_len = 99u;
                            free_de->file_type = 7u;
                            corrupted = true;
                        }
                        break;
                    }
                    ppos += rec_len;
                }
                dirent_repair_ok = corrupted && block_write(dev, first_sector, sector_count, raw_dir_block) == BLOCK_OK;
            }
        }
        ext4_mount_t broken_dirent_mnt;
        ext4_fsck_report_t broken_dirent_report;
        ext4_fsck_report_t fixed_dirent_report;
        char dirent_payload = 0;
        usize dirent_got = 0;
        bool fixed_dirent = dirent_repair_ok &&
                            ext4_mount_bounded(dev, part->lba_first, part->sector_count, &broken_dirent_mnt) == EXT4_OK &&
                            ext4_validate_metadata(&broken_dirent_mnt, &broken_dirent_report) == EXT4_ERR_CORRUPT && broken_dirent_report.errors > 0 &&
                            ext4_repair_metadata(&broken_dirent_mnt, &fixed_dirent_report) == EXT4_OK && fixed_dirent_report.errors == 0 && fixed_dirent_report.repaired_dirents >= 1u &&
                            vfs_read(dirent_child, 0, &dirent_payload, 1u, &dirent_got) == VFS_OK && dirent_got == 1u && dirent_payload == 'R';
        if (raw_dir_original_valid && !ktest_restore_raw_block(dev, raw_dir_first_sector, raw_dir_sector_count, raw_dir_original)) fixed_dirent = false;
        check(fixed_dirent, "EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata");
    } else {
        skip("EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata");
    }
    check(vfs_unlink(dirent_child) == VFS_OK && vfs_unlink(dirent_dir) == VFS_OK, "VFS cleanup ext4 dirent repair target");

    if (part && split_baseline_ok) {
        ext4_mount_t after_mnt;
        ext4_fsck_report_t after_report;
        bool no_leak = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &after_mnt) == EXT4_OK &&
                       ext4_validate_metadata(&after_mnt, &after_report) == EXT4_OK &&
                       after_report.errors == 0 && after_report.sb_free_blocks == split_free_before;
        check(no_leak, "EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    } else {
        skip("EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    }

    check(vfs_unlink(renamed_path) == VFS_OK, "VFS unlink ext4 renamed file");
    if (part) {
        ext4_mount_t final_mnt;
        ext4_fsck_report_t final_report;
        bool final_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &final_mnt) == EXT4_OK &&
                        ext4_validate_metadata(&final_mnt, &final_report) == EXT4_OK && final_report.errors == 0;
        check(final_ok, "EXT4 metadata remains consistent after mutation tests");
        ext4_mount_t counter_mnt;
        ext4_fsck_report_t broken_report;
        ext4_fsck_report_t fixed_report;
        bool counter_repair = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &counter_mnt) == EXT4_OK;
        if (counter_repair) {
            counter_mnt.sb.s_free_blocks_count_lo ^= 7u;
            counter_mnt.sb.s_free_inodes_count ^= 3u;
            counter_repair = ext4_validate_metadata(&counter_mnt, &broken_report) == EXT4_ERR_CORRUPT && broken_report.errors > 0 &&
                             ext4_repair_metadata(&counter_mnt, &fixed_report) == EXT4_OK && fixed_report.errors == 0 &&
                             fixed_report.repaired_counters >= 1u;
        }
        check(counter_repair, "EXT4 repair-lite reconciles corrupted free counters");
    } else {
        skip("EXT4 metadata remains consistent after mutation tests");
        skip("EXT4 repair-lite reconciles corrupted free counters");
    }
    kfree(repair_original);
    kfree(repair_scratch);
    suite_end();
