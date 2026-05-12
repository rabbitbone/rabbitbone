static block_status_t mem_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    auto *disk = static_cast<MemDisk *>(dev->ctx);
    if (lba > UINT64_MAX / 512ull || static_cast<u64>(count) > UINT64_MAX / 512ull) return BLOCK_ERR_RANGE;
    const u64 off = lba * 512ull;
    const u64 len = static_cast<u64>(count) * 512ull;
    if (off > disk->data.size() || len > disk->data.size() - off) return BLOCK_ERR_RANGE;
    memcpy(buffer, disk->data.data() + off, static_cast<std::size_t>(len));
    return BLOCK_OK;
}

static block_status_t mem_write(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    auto *disk = static_cast<MemDisk *>(dev->ctx);
    if (lba > UINT64_MAX / 512ull || static_cast<u64>(count) > UINT64_MAX / 512ull) return BLOCK_ERR_RANGE;
    const u64 off = lba * 512ull;
    const u64 len = static_cast<u64>(count) * 512ull;
    if (off > disk->data.size() || len > disk->data.size() - off) return BLOCK_ERR_RANGE;
    memcpy(disk->data.data() + off, buffer, static_cast<std::size_t>(len));
    return BLOCK_OK;
}

static block_status_t fail_read(block_device_t *, u64, u32, void *) {
    return BLOCK_ERR_IO;
}

static void test_block_ranges() {
    block_device_t dev{};
    dev.sector_count = 16;
    dev.sector_size = 512;
    dev.read = fail_read;
    u8 sector[512]{};
    require(block_read(&dev, 15, 2, sector) == BLOCK_ERR_RANGE, "block range end overflow rejected");
    require(block_read(&dev, UINT64_MAX, 2, sector) == BLOCK_ERR_RANGE, "block lba overflow rejected");
    mbr_partition_t part{};
    part.type = 0x83;
    part.lba_first = 15;
    part.sector_count = 2;
    require(!mbr_partition_valid(&dev, &part), "mbr range validator rejects oversized partition");
}

