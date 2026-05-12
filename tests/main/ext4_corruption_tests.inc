static void make_ext4_base(std::vector<u8> &data) {
    data.assign(128 * 1024, 0);
    const std::size_t sb = 1024;
    put32(data, sb + 0, 16);
    put32(data, sb + 4, 64);
    put32(data, sb + 20, 1);
    put32(data, sb + 24, 0);
    put32(data, sb + 32, 64);
    put32(data, sb + 40, 16);
    put16(data, sb + 56, 0xef53);
    put32(data, sb + 84, 11);
    put16(data, sb + 88, 128);
    put16(data, sb + 254, 32);
    put32(data, 2048 + 8, 5);
}

static ext4_status_t mount_mem_ext4(std::vector<u8> &data) {
    MemDisk disk{data};
    block_device_t dev{};
    strncpy(dev.name, "memx", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;
    ext4_mount_t mnt;
    return ext4_mount(&dev, 0, &mnt);
}

static void test_ext4_corrupt_inputs() {
    std::vector<u8> data;
    make_ext4_base(data);
    put32(data, 1024 + 24, 64);
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects huge block-size shift");
    make_ext4_base(data);
    put32(data, 1024 + 20, 65);
    require(mount_mem_ext4(data) == EXT4_ERR_CORRUPT, "ext4 rejects first_data beyond blocks");
    make_ext4_base(data);
    put16(data, 1024 + 88, 64);
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects tiny inode size");
    make_ext4_base(data);
    put16(data, 1024 + 254, 34);
    put32(data, 1024 + 96, 0x80); // enable 64bit desc-size field
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects unaligned group desc size");

    make_ext4_base(data);
    MemDisk disk{data};
    block_device_t dev{};
    strncpy(dev.name, "memx", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;
    ext4_mount_t mnt;
    require(ext4_mount_bounded(&dev, 0, 120, &mnt) == EXT4_ERR_RANGE, "ext4 bounded mount rejects filesystem larger than partition");
    require(ext4_mount_bounded(&dev, dev.sector_count - 4, 8, &mnt) == EXT4_ERR_RANGE, "ext4 bounded mount rejects partition beyond device");
    require(ext4_mount(&dev, 0, &mnt) == EXT4_OK, "ext4 corrupt extent base mount");
    ext4_inode_disk_t inode{};
    inode.i_mode = 0x8000;
    inode.i_size_lo = 4096;
    inode.i_flags = 0x00080000u;
    u8 extent_root[EXT4_N_BLOCKS * sizeof(u32)]{};
    extent_root[0] = 0x0a; extent_root[1] = 0xf3;
    extent_root[2] = 1; extent_root[3] = 0;
    extent_root[4] = 1; extent_root[5] = 0;
    extent_root[6] = 0xff; extent_root[7] = 0xff;
    memcpy(reinterpret_cast<u8 *>(&inode) + offsetof(ext4_inode_disk_t, i_block), extent_root, sizeof(extent_root));
    u8 out[16]{};
    usize got = 0;
    require(ext4_read_file(&mnt, &inode, 0, out, sizeof(out), &got) == EXT4_ERR_CORRUPT, "ext4 rejects excessive extent depth");
}

