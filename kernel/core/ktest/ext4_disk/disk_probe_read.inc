    suite_begin("block/mbr/ext4/disk0");
    u8 *repair_scratch = (u8 *)kmalloc(4096u);
    u8 *repair_original = (u8 *)kmalloc(4096u);
    block_device_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.sector_count = 16u;
    fake.sector_size = BLOCKDEV_SECTOR_SIZE;
    fake.read = ktest_block_read_should_not_run;
    u8 fake_sector[512];
    check(block_read(&fake, 15u, 2u, fake_sector) == BLOCK_ERR_RANGE && block_read(&fake, 0xffffffffffffffffull, 2u, fake_sector) == BLOCK_ERR_RANGE, "block_read rejects overflow ranges before driver");
    mbr_partition_t bad_part;
    memset(&bad_part, 0, sizeof(bad_part));
    bad_part.type = 0x83;
    bad_part.lba_first = 15u;
    bad_part.sector_count = 2u;
    check(!mbr_partition_valid(&fake, &bad_part), "MBR partition range validator rejects overflow partition");
    usize n = block_count();
    check(n > 0u, "block registry discovers at least one storage device");
    check(pci_device_count() > 0u, "PCI enumerator captured platform devices");
    bool have_storage = false;
    for (usize bi = 0; bi < n; ++bi) {
        block_device_t *bd = block_get(bi);
        if (bd && bd->driver && (strcmp(bd->driver, "ata_pio") == 0 || strcmp(bd->driver, "ahci") == 0)) have_storage = true;
    }
    check(have_storage, "block registry exposes storage driver identity");
    mbr_table_t mbr;
    const mbr_partition_t *part = 0;
    block_device_t *dev = ktest_find_linux_block_device(&mbr, &part);
    if (!dev) {
        skip("MBR read");
        skip("EXT4 raw mount");
        skip("VFS /disk0 read");
        kfree(repair_original);
        kfree(repair_scratch);
        suite_end();
        return;
    }
    check(dev->sector_size == BLOCKDEV_SECTOR_SIZE && dev->sector_count > 0u && dev->read != 0, "selected block device geometry sane");
    u8 sector[512];
    block_status_t br = block_read(dev, 0, 1, sector);
    if (br != BLOCK_OK) kprintf("[ detail] block_read(%s,lba=0,count=1)=%s\n", dev->name, block_status_name(br));
    else if (sector[510] != 0x55 || sector[511] != 0xaa) kprintf("[ detail] MBR signature bytes=%02x %02x\n", sector[510], sector[511]);
    check(br == BLOCK_OK && sector[510] == 0x55 && sector[511] == 0xaa, "block read MBR sector");
    bool mbr_ok = mbr_read(dev, &mbr);
    if (!mbr_ok) kprintf("[ detail] mbr_read(%s) failed after direct read status=%s\n", dev->name, block_status_name(br));
    check(mbr_ok, "MBR parser accepts boot disk");
    part = mbr_ok ? mbr_find_linux_on_device(dev, &mbr) : 0;
    check(part && part->lba_first >= 2048u && part->sector_count > 4096u, "MBR has Linux EXT partition on selected block device");
    if (part) {
        ext4_mount_t mnt;
        ext4_status_t est = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &mnt);
        check(est == EXT4_OK, "EXT4 raw mount first Linux partition");
        if (est == EXT4_OK) {
            ext4_inode_disk_t root;
            ext4_fsck_report_t report;
            check(ext4_validate_metadata(&mnt, &report) == EXT4_OK && report.errors == 0 && report.checked_groups == mnt.group_count, "EXT4 metadata counters match allocation bitmaps");
            check(ext4_read_inode(&mnt, EXT4_ROOT_INO, &root) == EXT4_OK && ext4_inode_is_dir(&root), "EXT4 root inode is directory");
            seen_name_ctx_t hello_seen = { "hello.txt", false };
            check(ext4_list_dir(&mnt, &root, ext4_seen_name, &hello_seen) == EXT4_OK && hello_seen.seen, "EXT4 root directory iteration finds hello.txt");
            ext4_inode_disk_t hello;
            u32 ino = 0;
            check(ext4_lookup_path(&mnt, "/hello.txt", &hello, &ino) == EXT4_OK && ino != 0 && ext4_inode_is_regular(&hello), "EXT4 path lookup /hello.txt");
            char text[96];
            usize rd = 0;
            memset(text, 0, sizeof(text));
            check(ext4_read_file(&mnt, &hello, 0, text, sizeof(text) - 1u, &rd) == EXT4_OK && rd > 0u && strstr(text, "AuroraOS") != 0, "EXT4 read regular file");
        }
    }
    char text[96];
    memset(text, 0, sizeof(text));
    usize got = 0;
    check(vfs_read("/disk0/hello.txt", 0, text, sizeof(text) - 1u, &got) == VFS_OK && got > 0u && strstr(text, "AuroraOS") != 0, "VFS read /disk0/hello.txt through ext4 adapter");
    vfs_statvfs_t sv;
    memset(&sv, 0, sizeof(sv));
    check(vfs_statvfs("/disk0", &sv) == VFS_OK && sv.block_size >= 1024u && sv.total_blocks > 0u && sv.free_blocks <= sv.total_blocks && (sv.flags & VFS_STATVFS_FLAG_PERSISTENT) != 0, "VFS statvfs exposes EXT4 capacity and persistence flags");
    check(vfs_sync_path("/disk0") == VFS_OK && vfs_sync_all() == VFS_OK, "VFS sync flushes EXT4 writeback state");
    check(ktest_cleanup_disk_prefix("aurora-"), "cleanup old aurora artifacts");
