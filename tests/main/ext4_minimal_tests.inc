static void test_ext4_minimal() {
    MemDisk disk{std::vector<u8>(128 * 1024)};
    block_device_t dev{};
    strncpy(dev.name, "mem0", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;

    // Superblock at byte 1024, block size 1024.
    const std::size_t sb = 1024;
    put32(disk.data, sb + 0, 16);      // inodes
    put32(disk.data, sb + 4, 64);      // blocks
    put32(disk.data, sb + 20, 1);      // first_data_block
    put32(disk.data, sb + 24, 0);      // log_block_size
    put32(disk.data, sb + 32, 64);     // blocks_per_group
    put32(disk.data, sb + 40, 16);     // inodes_per_group
    put16(disk.data, sb + 56, 0xef53); // magic
    put32(disk.data, sb + 84, 11);     // first ino
    put16(disk.data, sb + 88, 128);    // inode size
    put16(disk.data, sb + 254, 32);    // desc size

    // Group descriptor table at block 2, bitmaps at 3/4, inode table at 5.
    const std::size_t gd = 2048;
    put32(disk.data, gd + 0, 3);
    put32(disk.data, gd + 4, 4);
    put32(disk.data, gd + 8, 5);
    put16(disk.data, gd + 12, 63 - 10);
    put16(disk.data, gd + 14, 16 - 12);
    for (u32 b = 0; b <= 10; ++b) disk.data[3 * 1024 + b / 8] |= static_cast<u8>(1u << (b % 8));
    for (u32 ino_b = 0; ino_b < 12; ++ino_b) disk.data[4 * 1024 + ino_b / 8] |= static_cast<u8>(1u << (ino_b % 8));

    // Root inode #2 at inode table + 1 * 128.
    const std::size_t root = 5 * 1024 + 128;
    put16(disk.data, root + 0, 0x4000 | 0755);
    put32(disk.data, root + 4, 1024);
    put32(disk.data, root + 40, 10);

    // Directory block 10.
    const std::size_t dir = 10 * 1024;
    make_dirent(disk.data, dir, 2, 12, 2, ".");
    make_dirent(disk.data, dir + 12, 2, 12, 2, "..");
    make_dirent(disk.data, dir + 24, 12, 1000, 1, "hello.txt");

    ext4_mount_t mnt;
    require(ext4_mount(&dev, 0, &mnt) == EXT4_OK, "ext4 mount");
    ext4_inode_disk_t inode;
    require(ext4_read_inode(&mnt, EXT4_ROOT_INO, &inode) == EXT4_OK, "ext4 read root");
    require(ext4_inode_is_dir(&inode), "root is dir");
    int found = 0;
    require(ext4_list_dir(&mnt, &inode, count_entry, &found) == EXT4_OK, "ext4 list dir");
    require(found == 1, "ext4 found hello.txt");
    u32 ino = 0;
    require(ext4_find_in_dir(&mnt, &inode, "hello.txt", &ino, nullptr) == EXT4_OK && ino == 12, "ext4 find in dir");
    char too_long_name[EXT4_NAME_LEN + 2]{};
    memset(too_long_name, 'x', sizeof(too_long_name) - 1);
    require(ext4_find_in_dir(&mnt, &inode, too_long_name, &ino, nullptr) == EXT4_ERR_RANGE, "ext4 find rejects overlong name");
    const char payload[] = "persistent ext4 write";
    require(ext4_create_file(&mnt, "/new.txt", payload, sizeof(payload) - 1) == EXT4_OK, "ext4 create file");
    ext4_inode_disk_t created{};
    u32 created_ino = 0;
    require(ext4_lookup_path(&mnt, "/new.txt", &created, &created_ino) == EXT4_OK && ext4_inode_is_regular(&created), "ext4 lookup created file");
    char readback[64]{};
    usize rb = 0;
    require(ext4_read_file(&mnt, &created, 0, readback, sizeof(readback), &rb) == EXT4_OK && rb == sizeof(payload) - 1 && strcmp(readback, payload) == 0, "ext4 read created file");
    const char patch[] = "EXT4";
    require(ext4_write_file(&mnt, created_ino, &created, 11, patch, sizeof(patch) - 1, nullptr) == EXT4_OK, "ext4 overwrite created file");
    memset(readback, 0, sizeof(readback)); rb = 0;
    require(ext4_read_file(&mnt, &created, 0, readback, sizeof(readback), &rb) == EXT4_OK && strstr(readback, "EXT4") != nullptr, "ext4 read overwritten data");
    require(ext4_lookup_path(&mnt, "/./new.txt", &created, &created_ino) == EXT4_OK && ext4_inode_is_regular(&created), "ext4 lookup skips dot component");
    const u64 before_gap_size = ext4_inode_size(&created);
    const char gap_payload[] = "Z";
    require(ext4_write_file(&mnt, created_ino, &created, 4099, gap_payload, sizeof(gap_payload) - 1, nullptr) == EXT4_OK, "ext4 write past EOF zero-fills gap");
    require(ext4_read_inode(&mnt, created_ino, &created) == EXT4_OK && ext4_inode_size(&created) == 4100, "ext4 sparse write updates file size");
    memset(readback, 0x5a, sizeof(readback)); rb = 0;
    require(ext4_read_file(&mnt, &created, before_gap_size, readback, 16, &rb) == EXT4_OK && rb == 16, "ext4 reads zero-filled gap range");
    bool gap_zero = true;
    for (usize i = 0; i < rb; ++i) gap_zero = gap_zero && readback[i] == 0;
    require(gap_zero, "ext4 gap bytes are zero-filled");
    usize zero_written = 99;
    require(ext4_write_file(&mnt, created_ino, &created, 8192, "", 0, &zero_written) == EXT4_OK && zero_written == 0, "ext4 zero-byte write succeeds");
    require(ext4_read_inode(&mnt, created_ino, &created) == EXT4_OK && ext4_inode_size(&created) == 4100, "ext4 zero-byte write does not extend file");

    require(ext4_sync_file(&mnt, created_ino, true) == EXT4_OK, "ext4 clears prior dirty file data before delayed-write probe");
    std::vector<char> full_block(static_cast<std::size_t>(mnt.block_size), 'd');
    const u64 writes_before_delayed = disk.writes;
    require(ext4_write_file(&mnt, created_ino, &created, 0, full_block.data(), static_cast<usize>(full_block.size()), nullptr) == EXT4_OK, "ext4 full-block rewrite succeeds");
    require(disk.writes == writes_before_delayed, "ext4 data rewrite stays in writeback cache before fsync");
    require(ext4_sync_file(&mnt, created_ino, true) == EXT4_OK && disk.writes > writes_before_delayed, "ext4 fsync flushes cached data rewrite");

    const char other_name[] = "/other.txt";
    require(ext4_create_file(&mnt, other_name, full_block.data(), static_cast<usize>(full_block.size())) == EXT4_OK, "ext4 create second dirty file");
    ext4_inode_disk_t other_inode{};
    u32 other_ino = 0;
    ext4_perf_stats_t sync_before{};
    ext4_perf_stats_t sync_after{};
    require(ext4_lookup_path(&mnt, other_name, &other_inode, &other_ino) == EXT4_OK && other_ino != 0, "ext4 lookup second dirty file");
    require(ext4_get_perf_stats(&mnt, &sync_before) == EXT4_OK && sync_before.dirty_slots > 0, "ext4 reports dirty slots before targeted fsync");
    require(ext4_sync_file(&mnt, created_ino, true) == EXT4_OK && ext4_get_perf_stats(&mnt, &sync_after) == EXT4_OK && sync_after.dirty_slots > 0, "ext4 targeted fsync leaves unrelated dirty data queued");
    require(ext4_sync_file(&mnt, other_ino, true) == EXT4_OK, "ext4 fsync flushes second dirty file");
    require(ext4_unlink(&mnt, other_name) == EXT4_OK, "ext4 unlink second dirty file");
    require(ext4_mkdir(&mnt, "/.") == EXT4_ERR_RANGE, "ext4 rejects mkdir dot entry");
    require(ext4_mkdir(&mnt, "/..") == EXT4_ERR_RANGE, "ext4 rejects mkdir dotdot entry");
    require(ext4_mkdir(&mnt, "/dir") == EXT4_OK, "ext4 mkdir");
    ext4_inode_disk_t dir_inode{};
    u32 dir_ino = 0;
    require(ext4_lookup_path(&mnt, "/dir", &dir_inode, &dir_ino) == EXT4_OK && ext4_inode_is_dir(&dir_inode) &&
            ext4_inode_uses_extents(&dir_inode) && ext4_inode_extent_depth(&dir_inode) == 0,
            "ext4 mkdir creates extent-backed directory");
    require(ext4_unlink(&mnt, "/dir/.") == EXT4_ERR_RANGE, "ext4 refuses unlink dot entry");
    require(ext4_rename(&mnt, "/dir", "/.") == EXT4_ERR_RANGE, "ext4 refuses rename over dot entry");
    require(ext4_create_file(&mnt, "/dir/child", "x", 1) == EXT4_OK, "ext4 create child in directory");
    require(ext4_unlink(&mnt, "/dir") == EXT4_ERR_NOT_EMPTY, "ext4 refuses unlink non-empty dir");
    require(ext4_unlink(&mnt, "/dir/child") == EXT4_OK, "ext4 unlink child");
    require(ext4_create_file(&mnt, "/dir/child2", "y", 1) == EXT4_OK, "ext4 reuses directory entry space after unlink");
    require(ext4_unlink(&mnt, "/dir/child2") == EXT4_OK, "ext4 unlink replacement child");
    require(ext4_unlink(&mnt, "/dir") == EXT4_OK, "ext4 unlink empty dir");
    require(ext4_unlink(&mnt, "/new.txt") == EXT4_OK, "ext4 unlink file");

    // 32-byte group descriptors must not read high fields from the next descriptor.
    put32(disk.data, gd + 40, 0x7fffffffu);
    require(ext4_read_inode(&mnt, EXT4_ROOT_INO, &inode) == EXT4_OK, "ext4 32-byte group desc ignores neighbor high fields");
}

