static ext4_status_t partition_byte_range_ok(u64 partition_sectors, u64 byte_offset, usize bytes) {
    if (!bytes) return EXT4_OK;
    if (partition_sectors == 0) return EXT4_OK;
    u64 byte_len = 0;
    if (!checked_mul_u64(partition_sectors, 512u, &byte_len)) return EXT4_ERR_RANGE;
    if (byte_offset >= byte_len) return EXT4_ERR_RANGE;
    if ((u64)bytes > byte_len - byte_offset) return EXT4_ERR_RANGE;
    return EXT4_OK;
}

static ext4_status_t read_bytes(block_device_t *dev, u64 partition_lba, u64 partition_sectors, u64 byte_offset, void *buffer, usize bytes) {
    if (!dev || !buffer) return EXT4_ERR_IO;
    ext4_status_t bounds = partition_byte_range_ok(partition_sectors, byte_offset, bytes);
    if (bounds != EXT4_OK) return bounds;
    u8 *out = (u8 *)buffer;
    u8 sector[512];
    while (bytes) {
        u64 rel_sector = byte_offset / 512u;
        if (partition_sectors && rel_sector >= partition_sectors) return EXT4_ERR_RANGE;
        u64 lba = 0;
        if (!checked_add_u64(partition_lba, rel_sector, &lba)) return EXT4_ERR_RANGE;
        u32 off = (u32)(byte_offset % 512u);
        if (block_read(dev, lba, 1, sector) != BLOCK_OK) return EXT4_ERR_IO;
        usize take = 512u - off;
        if (take > bytes) take = bytes;
        memcpy(out, sector + off, take);
        out += take;
        byte_offset += take;
        bytes -= take;
    }
    return EXT4_OK;
}


static ext4_status_t write_bytes(block_device_t *dev, u64 partition_lba, u64 partition_sectors, u64 byte_offset, const void *buffer, usize bytes) {
    if (!dev || !buffer) return EXT4_ERR_IO;
    ext4_status_t bounds = partition_byte_range_ok(partition_sectors, byte_offset, bytes);
    if (bounds != EXT4_OK) return bounds;
    const u8 *in = (const u8 *)buffer;
    u8 sector[512];
    while (bytes) {
        u64 rel_sector = byte_offset / 512u;
        if (partition_sectors && rel_sector >= partition_sectors) return EXT4_ERR_RANGE;
        u64 lba = 0;
        if (!checked_add_u64(partition_lba, rel_sector, &lba)) return EXT4_ERR_RANGE;
        u32 off = (u32)(byte_offset % 512u);
        usize take = 512u - off;
        if (take > bytes) take = bytes;
        if (take != 512u) {
            if (block_read(dev, lba, 1, sector) != BLOCK_OK) return EXT4_ERR_IO;
            memcpy(sector + off, in, take);
        } else {
            memcpy(sector, in, 512u);
        }
        if (block_write(dev, lba, 1, sector) != BLOCK_OK) return EXT4_ERR_IO;
        in += take;
        byte_offset += take;
        bytes -= take;
    }
    return EXT4_OK;
}

#define EXT4_WRITE_CACHE_SLOTS 12u

typedef struct ext4_write_cache_slot {
    bool valid;
    bool dirty;
    block_device_t *dev;
    u64 partition_lba;
    u64 partition_sectors;
    u64 block;
    u32 block_size;
    u8 data[MAX_BLOCK_SIZE];
} ext4_write_cache_slot_t;

static ext4_write_cache_slot_t *g_ext4_write_cache;
static u32 g_ext4_write_cache_slots;

#define EXT4_DATA_CACHE_SLOTS 12u
#define EXT4_DATA_CACHE_READAHEAD 2u
#define EXT4_DATA_CACHE_DIRTY_HIGH_WATER ((EXT4_DATA_CACHE_SLOTS * 3u) / 4u)
#define EXT4_DATA_CACHE_WRITEBACK_BATCH 4u

typedef struct ext4_data_cache_slot {
    bool valid;
    bool dirty;
    block_device_t *dev;
    u64 partition_lba;
    u64 partition_sectors;
    u64 block;
    u64 last_used;
    u32 block_size;
    bool readahead;
    u8 data[MAX_BLOCK_SIZE];
} ext4_data_cache_slot_t;

static ext4_data_cache_slot_t *g_ext4_data_cache;
static u32 g_ext4_data_cache_slots;
static u64 g_ext4_cache_clock;
static ext4_perf_stats_t g_ext4_perf_stats;
static ext4_status_t journal_commit_ordered_block(ext4_mount_t *mnt, u64 target, const void *buffer);
static ext4_status_t raw_write_block(ext4_mount_t *mnt, u64 block, const void *buffer);
static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer);
static ext4_status_t ext4_flush_data_cache_for_mount(ext4_mount_t *mnt);
static void data_cache_discard_block(const ext4_mount_t *mnt, u64 block);
static ext4_status_t read_data_block(ext4_mount_t *mnt, u64 block, void *buffer);
static ext4_status_t write_data_block(ext4_mount_t *mnt, u64 block, const void *buffer);

static u8 *ext4_tmp_block(ext4_mount_t *mnt) {
    if (!mnt || mnt->block_size == 0 || mnt->block_size > MAX_BLOCK_SIZE) return 0;
    return (u8 *)kmalloc((usize)mnt->block_size);
}

