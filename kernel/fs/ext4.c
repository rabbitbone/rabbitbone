#include <aurora/ext4.h>
#include <aurora/vfs.h>
#include <aurora/libc.h>
#include <aurora/crc32.h>
#include <aurora/kmem.h>
#include <aurora/path.h>

#define EXT4_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define EXT4_FEATURE_INCOMPAT_EXTENTS  0x0040u
#define EXT4_FEATURE_INCOMPAT_64BIT    0x0080u
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE 0x0008u
#define EXT4_EXTENTS_FL EXT4_INODE_FLAG_EXTENTS
#define EXT4_INDEX_FL 0x00001000u
#define AURORA_EXT4_JOURNAL_MAGIC 0x4a524f41u /* AORJ */
#define AURORA_EXT4_HTREE_MAGIC 0x45525448u /* HTRE */
#define AURORA_EXT4_HTREE_VERSION 1u
#define AURORA_EXT4_HTREE_MIN_ENTRIES 12u
#define EXT4_IFDIR 0x4000u
#define EXT4_IFREG 0x8000u
#define EXT4_IFLNK 0xA000u
#define EXT4_EXT_MAGIC EXT4_EXTENT_MAGIC
#define MAX_BLOCK_SIZE 4096u
#define EXT4_MAX_EXTENT_DEPTH 5u

typedef struct AURORA_PACKED ext4_extent_header {
    u16 eh_magic;
    u16 eh_entries;
    u16 eh_max;
    u16 eh_depth;
    u32 eh_generation;
} ext4_extent_header_t;

typedef struct AURORA_PACKED ext4_extent_idx {
    u32 ei_block;
    u32 ei_leaf_lo;
    u16 ei_leaf_hi;
    u16 ei_unused;
} ext4_extent_idx_t;

typedef struct AURORA_PACKED ext4_extent {
    u32 ee_block;
    u16 ee_len;
    u16 ee_start_hi;
    u32 ee_start_lo;
} ext4_extent_t;



typedef struct AURORA_PACKED aurora_ext4_journal_record {
    u32 magic;
    u32 version;
    u32 state;
    u32 seq;
    u64 target_block;
    u32 block_size;
    u32 payload_crc;
    u32 header_crc;
    u32 reserved[8];
} aurora_ext4_journal_record_t;

typedef struct AURORA_PACKED aurora_htree_header {
    u32 magic;
    u16 version;
    u16 entry_size;
    u16 entries;
    u16 max_entries;
    u32 dir_ino;
    u32 checksum;
    u32 reserved[3];
} aurora_htree_header_t;

typedef struct AURORA_PACKED aurora_htree_entry {
    u32 hash;
    u32 inode;
    u16 logical_block;
    u16 offset;
    u8 name_len;
    u8 file_type;
    u16 reserved;
} aurora_htree_entry_t;

typedef struct AURORA_PACKED ext4_dir_entry_2 {
    u32 inode;
    u16 rec_len;
    u8 name_len;
    u8 file_type;
    char name[];
} ext4_dir_entry_2_t;

static u16 le16(u16 v) { return v; }
static u32 le32(u32 v) { return v; }
static void put32(u32 *p, u32 v) { *p = v; }
static u64 le64_from_lo_hi(u32 lo, u32 hi) { return ((u64)le32(hi) << 32) | le32(lo); }
static u64 extent_start_block(const ext4_extent_t *ex) { return le64_from_lo_hi(ex->ee_start_lo, ex->ee_start_hi); }
static u32 extent_len_blocks(const ext4_extent_t *ex) {
    u16 raw = le16(ex->ee_len);
    if (raw == 0x8000u) return 32768u;
    return (u32)(raw & 0x7fffu);
}
static bool extent_is_unwritten(const ext4_extent_t *ex) { return le16(ex->ee_len) > 0x8000u; }
static void extent_set_start_block(ext4_extent_t *ex, u64 block) {
    ex->ee_start_lo = (u32)block;
    ex->ee_start_hi = (u16)(block >> 32);
}
static void extent_set_len_blocks_state(ext4_extent_t *ex, u32 len, bool unwritten) {
    u16 raw = (u16)(len == 32768u ? 0x8000u : len);
    if (unwritten && raw != 0) raw |= 0x8000u;
    ex->ee_len = raw;
}
static bool checked_add_u64(u64 a, u64 b, u64 *out) { return !__builtin_add_overflow(a, b, out); }
static bool checked_mul_u64(u64 a, u64 b, u64 *out) { return !__builtin_mul_overflow(a, b, out); }
static bool checked_add_usize(usize a, usize b, usize *out) { return !__builtin_add_overflow(a, b, out); }
static u64 div_round_up_u64(u64 a, u64 b) { return b ? (a + b - 1) / b : 0; }
static u16 ext4_rec_len(usize name_len) { return (u16)((8u + name_len + 3u) & ~3u); }
static bool ext4_name_is_dot_or_dotdot(const char *name) { return name && name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)); }
static bool bitmap_get(const u8 *bm, u32 bit) { return (bm[bit / 8u] & (u8)(1u << (bit % 8u))) != 0; }
static void bitmap_set8(u8 *bm, u32 bit) { bm[bit / 8u] |= (u8)(1u << (bit % 8u)); }
static void bitmap_clear8(u8 *bm, u32 bit) { bm[bit / 8u] &= (u8)~(1u << (bit % 8u)); }
static u16 extent_inline_capacity(void);
static u16 extent_root_index_capacity(void);
static u16 extent_leaf_capacity(ext4_mount_t *mnt);
static u16 extent_index_node_capacity(ext4_mount_t *mnt);
static ext4_status_t raw_write_block(ext4_mount_t *mnt, u64 block, const void *buffer);
static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer);
static bool ext4_block_is_journal(const ext4_mount_t *mnt, u64 block);
static ext4_status_t ext4_flush_write_cache_for_mount(ext4_mount_t *mnt);
static ext4_status_t write_super(ext4_mount_t *mnt);
static ext4_status_t write_inode(ext4_mount_t *mnt, u32 ino, const ext4_inode_disk_t *inode);
static ext4_status_t reserve_block_if_free(ext4_mount_t *mnt, u64 block);
static ext4_status_t ext4_recover_orphans(ext4_mount_t *mnt, ext4_fsck_report_t *report);
static ext4_status_t htree_load(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, u8 *block, aurora_htree_header_t **hdr_out, aurora_htree_entry_t **entries_out);
static ext4_status_t ext4_recount_free_counters(ext4_mount_t *mnt);
static ext4_status_t free_removed_inode(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, bool is_dir);
static ext4_status_t drop_removed_link(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, bool is_dir);
static ext4_status_t dir_replace_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 new_ino, u8 new_type, u32 *old_ino_out, u8 *old_type_out);
static ext4_status_t map_inode_block(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u32 logical, u64 *phys_out);
static u32 htree_hash_name(const char *name, usize len);

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

static void ext4_tmp_block_free(u8 *p) {
    if (p) kfree(p);
}

static bool write_cache_ensure(void) {
    if (g_ext4_write_cache && g_ext4_write_cache_slots) return true;
    g_ext4_write_cache = (ext4_write_cache_slot_t *)kcalloc(EXT4_WRITE_CACHE_SLOTS, sizeof(ext4_write_cache_slot_t));
    if (!g_ext4_write_cache) {
        g_ext4_write_cache_slots = 0;
        return false;
    }
    g_ext4_write_cache_slots = EXT4_WRITE_CACHE_SLOTS;
    return true;
}

static bool write_cache_slot_matches(const ext4_write_cache_slot_t *slot, const ext4_mount_t *mnt, u64 block) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size && slot->block == block;
}

static ext4_write_cache_slot_t *write_cache_find_slot(const ext4_mount_t *mnt, u64 block) {
    if (!g_ext4_write_cache || !g_ext4_write_cache_slots) return 0;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        if (write_cache_slot_matches(&g_ext4_write_cache[i], mnt, block)) return &g_ext4_write_cache[i];
    }
    return 0;
}

static void write_cache_discard_block(const ext4_mount_t *mnt, u64 block) {
    if (!mnt || !g_ext4_write_cache || !g_ext4_write_cache_slots) return;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (write_cache_slot_matches(slot, mnt, block)) {
            slot->dirty = false;
            slot->valid = false;
            ++g_ext4_perf_stats.cache_invalidations;
        }
    }
}

static u64 data_cache_tick(void) {
    ++g_ext4_cache_clock;
    if (g_ext4_cache_clock == 0) ++g_ext4_cache_clock;
    return g_ext4_cache_clock;
}

static bool data_cache_ensure(void) {
    if (g_ext4_data_cache && g_ext4_data_cache_slots) return true;
    g_ext4_data_cache = (ext4_data_cache_slot_t *)kcalloc(EXT4_DATA_CACHE_SLOTS, sizeof(ext4_data_cache_slot_t));
    if (!g_ext4_data_cache) {
        g_ext4_data_cache_slots = 0;
        return false;
    }
    g_ext4_data_cache_slots = EXT4_DATA_CACHE_SLOTS;
    return true;
}

static void data_cache_touch(ext4_data_cache_slot_t *slot) {
    if (slot) slot->last_used = data_cache_tick();
}

static bool data_cache_slot_matches(const ext4_data_cache_slot_t *slot, const ext4_mount_t *mnt, u64 block) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size && slot->block == block;
}

static ext4_data_cache_slot_t *data_cache_find_slot(const ext4_mount_t *mnt, u64 block) {
    if (!g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_matches(slot, mnt, block)) {
            data_cache_touch(slot);
            return slot;
        }
    }
    return 0;
}

static void data_cache_drop_slot(ext4_data_cache_slot_t *slot) {
    if (!slot) return;
    slot->dirty = false;
    slot->valid = false;
    slot->readahead = false;
    slot->last_used = 0;
}

static void data_cache_discard_block(const ext4_mount_t *mnt, u64 block) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_matches(slot, mnt, block)) {
            data_cache_drop_slot(slot);
            ++g_ext4_perf_stats.data_cache_invalidations;
        }
    }
}

static bool data_cache_slot_mount_matches(const ext4_data_cache_slot_t *slot, const ext4_mount_t *mnt) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size;
}

static void data_cache_discard_clean_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_mount_matches(slot, mnt) && !slot->dirty) {
            data_cache_drop_slot(slot);
            ++g_ext4_perf_stats.data_cache_invalidations;
        }
    }
}

static bool write_cache_slot_mount_matches(const ext4_write_cache_slot_t *slot, const ext4_mount_t *mnt) {
    return slot && mnt && slot->valid && slot->dev == mnt->dev &&
           slot->partition_lba == mnt->partition_lba &&
           slot->partition_sectors == mnt->partition_sectors &&
           slot->block_size == (u32)mnt->block_size;
}

static void write_cache_discard_clean_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_write_cache || !g_ext4_write_cache_slots) return;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (write_cache_slot_mount_matches(slot, mnt) && !slot->dirty) {
            slot->valid = false;
            ++g_ext4_perf_stats.cache_invalidations;
        }
    }
}

static ext4_status_t ext4_flush_and_invalidate_read_caches(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    ext4_status_t st = ext4_flush_data_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    data_cache_discard_clean_mount(mnt);
    write_cache_discard_clean_mount(mnt);
    return EXT4_OK;
}

static ext4_status_t data_cache_flush_slot(ext4_mount_t *mnt, ext4_data_cache_slot_t *slot) {
    if (!slot || !slot->valid || !slot->dirty) return EXT4_OK;
    ++g_ext4_perf_stats.data_cache_flushes;
    ext4_mount_t tmp;
    ext4_mount_t *use = mnt;
    if (!use || slot->dev != use->dev || slot->partition_lba != use->partition_lba ||
        slot->partition_sectors != use->partition_sectors || slot->block_size != (u32)use->block_size ||
        slot->block >= use->blocks_count) {
        memset(&tmp, 0, sizeof(tmp));
        tmp.dev = slot->dev;
        tmp.partition_lba = slot->partition_lba;
        tmp.partition_sectors = slot->partition_sectors;
        tmp.block_size = slot->block_size;
        tmp.blocks_count = slot->partition_sectors ? (slot->partition_sectors * 512u) / slot->block_size : ((u64)~0ull) / slot->block_size;
        use = &tmp;
    }
    ext4_status_t st = raw_write_block(use, slot->block, slot->data);
    if (st != EXT4_OK) return st;
    data_cache_drop_slot(slot);
    return EXT4_OK;
}

static ext4_status_t ext4_flush_data_cache_for_mount(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (!g_ext4_data_cache || !g_ext4_data_cache_slots) return EXT4_OK;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!slot->valid || !slot->dirty || slot->dev != mnt->dev || slot->partition_lba != mnt->partition_lba ||
            slot->partition_sectors != mnt->partition_sectors || slot->block_size != (u32)mnt->block_size) continue;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static u32 data_cache_dirty_count_mount(const ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    u32 count = 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        const ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (data_cache_slot_mount_matches(slot, mnt) && slot->dirty) ++count;
    }
    return count;
}

static ext4_data_cache_slot_t *data_cache_lru_slot(const ext4_mount_t *mnt, bool prefer_clean) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return 0;
    ext4_data_cache_slot_t *best = 0;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!slot->valid) return slot;
        if (!data_cache_slot_mount_matches(slot, mnt)) continue;
        if (prefer_clean && slot->dirty) continue;
        if (!best || slot->last_used < best->last_used) best = slot;
    }
    if (best) return best;
    for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
        ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
        if (!best || slot->last_used < best->last_used) best = slot;
    }
    return best;
}

static ext4_status_t data_cache_flush_pressure(ext4_mount_t *mnt) {
    if (!mnt || !g_ext4_data_cache || !g_ext4_data_cache_slots) return EXT4_OK;
    if (data_cache_dirty_count_mount(mnt) <= EXT4_DATA_CACHE_DIRTY_HIGH_WATER) return EXT4_OK;
    ++g_ext4_perf_stats.data_cache_pressure_flushes;
    ++g_ext4_perf_stats.data_cache_writeback_runs;
    for (u32 n = 0; n < EXT4_DATA_CACHE_WRITEBACK_BATCH; ++n) {
        ext4_data_cache_slot_t *best = 0;
        for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
            ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
            if (!data_cache_slot_mount_matches(slot, mnt) || !slot->dirty) continue;
            if (!best || slot->last_used < best->last_used) best = slot;
        }
        if (!best) break;
        ext4_status_t st = data_cache_flush_slot(mnt, best);
        if (st != EXT4_OK) return st;
        if (data_cache_dirty_count_mount(mnt) <= EXT4_DATA_CACHE_DIRTY_HIGH_WATER) break;
    }
    return EXT4_OK;
}

static ext4_status_t data_cache_install_slot(ext4_mount_t *mnt, u64 block, const void *buffer, bool dirty, bool readahead) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (!data_cache_ensure()) return dirty ? raw_write_block(mnt, block, buffer) : EXT4_OK;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (!slot) slot = data_cache_lru_slot(mnt, !dirty);
    if (!slot) return dirty ? raw_write_block(mnt, block, buffer) : EXT4_OK;
    if (slot->valid && (slot->dirty || !data_cache_slot_matches(slot, mnt, block))) {
        ++g_ext4_perf_stats.data_cache_evictions;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    } else if (slot->valid && !data_cache_slot_matches(slot, mnt, block)) {
        ++g_ext4_perf_stats.data_cache_evictions;
        data_cache_drop_slot(slot);
    }
    slot->valid = true;
    slot->dirty = dirty;
    slot->dev = mnt->dev;
    slot->partition_lba = mnt->partition_lba;
    slot->partition_sectors = mnt->partition_sectors;
    slot->block = block;
    slot->block_size = (u32)mnt->block_size;
    slot->readahead = readahead;
    slot->last_used = data_cache_tick();
    memcpy(slot->data, buffer, (usize)mnt->block_size);
    if (dirty) ++g_ext4_perf_stats.data_cache_stores;
    else ++g_ext4_perf_stats.data_cache_clean_stores;
    return dirty ? data_cache_flush_pressure(mnt) : EXT4_OK;
}

static ext4_status_t data_cache_store_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return raw_write_block(mnt, block, buffer);
    return data_cache_install_slot(mnt, block, buffer, true, false);
}

static ext4_status_t data_cache_store_clean_block(ext4_mount_t *mnt, u64 block, const void *buffer, bool readahead) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return EXT4_OK;
    if (data_cache_find_slot(mnt, block)) return EXT4_OK;
    ext4_status_t st = data_cache_install_slot(mnt, block, buffer, false, readahead);
    if (st == EXT4_OK && readahead) ++g_ext4_perf_stats.data_cache_readahead;
    return st;
}

static ext4_status_t data_cache_prepare_slot(ext4_mount_t *mnt, u64 block, bool prefer_clean, ext4_data_cache_slot_t **slot_out) {
    if (slot_out) *slot_out = 0;
    if (!mnt || block >= mnt->blocks_count || !slot_out) return EXT4_ERR_RANGE;
    if (!data_cache_ensure()) return EXT4_ERR_NO_MEMORY;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (!slot) slot = data_cache_lru_slot(mnt, prefer_clean);
    if (!slot) return EXT4_ERR_NO_MEMORY;
    if (slot->valid && slot->dirty) {
        ++g_ext4_perf_stats.data_cache_evictions;
        ext4_status_t st = data_cache_flush_slot(mnt, slot);
        if (st != EXT4_OK) return st;
    } else if (slot->valid && !data_cache_slot_matches(slot, mnt, block)) {
        ++g_ext4_perf_stats.data_cache_evictions;
        data_cache_drop_slot(slot);
    }
    *slot_out = slot;
    return EXT4_OK;
}

static void data_cache_readahead(ext4_mount_t *mnt, u64 first_block) {
    if (!mnt || mnt->block_size > MAX_BLOCK_SIZE) return;
    for (u32 i = 0; i < EXT4_DATA_CACHE_READAHEAD; ++i) {
        u64 next = first_block + i;
        if (next >= mnt->blocks_count || ext4_block_is_journal(mnt, next)) break;
        if (data_cache_find_slot(mnt, next) || write_cache_find_slot(mnt, next)) continue;
        ext4_data_cache_slot_t *slot = 0;
        ext4_status_t st = data_cache_prepare_slot(mnt, next, true, &slot);
        if (st != EXT4_OK || !slot) break;
        u64 byte_offset = 0;
        if (!checked_mul_u64(next, mnt->block_size, &byte_offset)) {
            data_cache_drop_slot(slot);
            break;
        }
        ++g_ext4_perf_stats.raw_reads;
        st = read_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, slot->data, (usize)mnt->block_size);
        if (st != EXT4_OK) {
            data_cache_drop_slot(slot);
            break;
        }
        slot->valid = true;
        slot->dirty = false;
        slot->dev = mnt->dev;
        slot->partition_lba = mnt->partition_lba;
        slot->partition_sectors = mnt->partition_sectors;
        slot->block = next;
        slot->block_size = (u32)mnt->block_size;
        slot->readahead = true;
        slot->last_used = data_cache_tick();
        ++g_ext4_perf_stats.data_cache_clean_stores;
        ++g_ext4_perf_stats.data_cache_readahead;
    }
}

static ext4_status_t read_data_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    ext4_data_cache_slot_t *slot = data_cache_find_slot(mnt, block);
    if (slot) {
        memcpy(buffer, slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.data_cache_hits;
        return EXT4_OK;
    }
    ext4_status_t st = read_block(mnt, block, buffer);
    if (st != EXT4_OK) return st;
    st = data_cache_store_clean_block(mnt, block, buffer, false);
    if (st != EXT4_OK) return st;
    data_cache_readahead(mnt, block + 1u);
    return EXT4_OK;
}

static ext4_status_t write_cache_flush_slot_with_mount(ext4_mount_t *mnt, ext4_write_cache_slot_t *slot) {
    if (!slot || !slot->valid || !slot->dirty) return EXT4_OK;
    ++g_ext4_perf_stats.cache_flushes;
    ext4_status_t st;
    if (mnt && slot->dev == mnt->dev && slot->partition_lba == mnt->partition_lba &&
        slot->partition_sectors == mnt->partition_sectors && slot->block_size == (u32)mnt->block_size &&
        slot->block < mnt->blocks_count) {
        st = journal_commit_ordered_block(mnt, slot->block, slot->data);
    } else {
        ext4_mount_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.dev = slot->dev;
        tmp.partition_lba = slot->partition_lba;
        tmp.partition_sectors = slot->partition_sectors;
        tmp.block_size = slot->block_size;
        tmp.blocks_count = slot->partition_sectors ? (slot->partition_sectors * 512u) / slot->block_size : ((u64)~0ull) / slot->block_size;
        tmp.journal_ready = false;
        st = raw_write_block(&tmp, slot->block, slot->data);
    }
    if (st != EXT4_OK) return st;
    slot->dirty = false;
    slot->valid = false;
    return EXT4_OK;
}

static ext4_status_t ext4_flush_write_cache_for_mount(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (!g_ext4_write_cache || !g_ext4_write_cache_slots) return EXT4_OK;
    for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
        ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
        if (!slot->valid || !slot->dirty || slot->dev != mnt->dev || slot->partition_lba != mnt->partition_lba ||
            slot->partition_sectors != mnt->partition_sectors || slot->block_size != (u32)mnt->block_size) continue;
        ++g_ext4_perf_stats.cache_flushes;
        ext4_status_t st = journal_commit_ordered_block(mnt, slot->block, slot->data);
        if (st != EXT4_OK) return st;
        slot->dirty = false;
        slot->valid = false;
    }
    return EXT4_OK;
}

static ext4_status_t write_cache_store_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    data_cache_discard_block(mnt, block);
    if (mnt->block_size > MAX_BLOCK_SIZE || ext4_block_is_journal(mnt, block)) return journal_commit_ordered_block(mnt, block, buffer);
    ext4_write_cache_slot_t *slot = write_cache_find_slot(mnt, block);
    if (!slot) {
        if (!write_cache_ensure()) return journal_commit_ordered_block(mnt, block, buffer);
        for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
            if (!g_ext4_write_cache[i].valid) { slot = &g_ext4_write_cache[i]; break; }
        }
    }
    if (!slot) {
        if (!write_cache_ensure()) return journal_commit_ordered_block(mnt, block, buffer);
        slot = &g_ext4_write_cache[0];
        ++g_ext4_perf_stats.cache_evictions;
        ext4_status_t st = write_cache_flush_slot_with_mount(mnt, slot);
        if (st != EXT4_OK) return st;
    }
    slot->valid = true;
    slot->dirty = true;
    slot->dev = mnt->dev;
    slot->partition_lba = mnt->partition_lba;
    slot->partition_sectors = mnt->partition_sectors;
    slot->block = block;
    slot->block_size = (u32)mnt->block_size;
    memcpy(slot->data, buffer, (usize)mnt->block_size);
    ++g_ext4_perf_stats.cache_stores;
    return EXT4_OK;
}

static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    ext4_write_cache_slot_t *slot = write_cache_find_slot(mnt, block);
    if (slot) {
        memcpy(buffer, slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.cache_hits;
        return EXT4_OK;
    }
    ext4_data_cache_slot_t *data_slot = data_cache_find_slot(mnt, block);
    if (data_slot) {
        memcpy(buffer, data_slot->data, (usize)mnt->block_size);
        ++g_ext4_perf_stats.data_cache_hits;
        return EXT4_OK;
    }
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    ++g_ext4_perf_stats.raw_reads;
    return read_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, buffer, (usize)mnt->block_size);
}


static ext4_status_t raw_write_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    ++g_ext4_perf_stats.raw_writes;
    return write_bytes(mnt->dev, mnt->partition_lba, mnt->partition_sectors, byte_offset, buffer, (usize)mnt->block_size);
}

static u32 journal_header_crc(aurora_ext4_journal_record_t *jr) {
    u32 saved = jr->header_crc;
    jr->header_crc = 0;
    u32 c = crc32(jr, sizeof(*jr));
    jr->header_crc = saved;
    return c;
}

static bool ext4_block_is_journal(const ext4_mount_t *mnt, u64 block) {
    return mnt && mnt->journal_blocks && block >= mnt->journal_first_block && block < mnt->journal_first_block + mnt->journal_blocks;
}

static ext4_status_t journal_clear(ext4_mount_t *mnt) {
    if (!mnt || !mnt->journal_blocks) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    memset(block, 0, (usize)mnt->block_size);
    ext4_status_t st = raw_write_block(mnt, mnt->journal_first_block, block);
    ext4_tmp_block_free(block);
    return st;
}

static ext4_status_t journal_commit_ordered_block(ext4_mount_t *mnt, u64 target, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (!mnt->journal_ready || ext4_block_is_journal(mnt, target)) return raw_write_block(mnt, target, buffer);
    ++g_ext4_perf_stats.journal_commits;
    if (mnt->journal_blocks < 2u || mnt->block_size > MAX_BLOCK_SIZE) return raw_write_block(mnt, target, buffer);

    aurora_ext4_journal_record_t jr;
    memset(&jr, 0, sizeof(jr));
    jr.magic = AURORA_EXT4_JOURNAL_MAGIC;
    jr.version = 1u;
    jr.state = 1u;
    jr.seq = ++mnt->journal_seq;
    jr.target_block = target;
    jr.block_size = (u32)mnt->block_size;
    jr.payload_crc = crc32(buffer, (usize)mnt->block_size);
    jr.header_crc = journal_header_crc(&jr);

    u8 *header_block = ext4_tmp_block(mnt);
    if (!header_block) return EXT4_ERR_NO_MEMORY;
    memset(header_block, 0, (usize)mnt->block_size);
    memcpy(header_block, &jr, sizeof(jr));
    ext4_status_t st = raw_write_block(mnt, mnt->journal_first_block + 1u, buffer);
    if (st == EXT4_OK) st = raw_write_block(mnt, mnt->journal_first_block, header_block);
    if (st == EXT4_OK) st = raw_write_block(mnt, target, buffer);
    ext4_tmp_block_free(header_block);
    return st;
}

static ext4_status_t write_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    return write_cache_store_block(mnt, block, buffer);
}

static ext4_status_t write_data_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    write_cache_discard_block(mnt, block);
    ++g_ext4_perf_stats.data_writes;
    return data_cache_store_block(mnt, block, buffer);
}

static ext4_status_t read_metadata_bytes(ext4_mount_t *mnt, u64 byte_offset, void *buffer, usize bytes) {
    if (!mnt || (!buffer && bytes)) return EXT4_ERR_IO;
    if (bytes == 0) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *dst = (u8 *)buffer;
    usize done = 0;
    ext4_status_t result = EXT4_OK;
    while (done < bytes) {
        u64 cur = byte_offset + done;
        u64 fs_block = cur / mnt->block_size;
        u32 block_off = (u32)(cur % mnt->block_size);
        usize take = mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        ext4_status_t st = read_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(dst + done, block + block_off, take);
        done += take;
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t write_metadata_bytes(ext4_mount_t *mnt, u64 byte_offset, const void *buffer, usize bytes) {
    if (!mnt || (!buffer && bytes)) return EXT4_ERR_IO;
    if (bytes == 0) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    const u8 *src = (const u8 *)buffer;
    usize done = 0;
    ext4_status_t result = EXT4_OK;
    while (done < bytes) {
        u64 cur = byte_offset + done;
        u64 fs_block = cur / mnt->block_size;
        u32 block_off = (u32)(cur % mnt->block_size);
        usize take = mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        ext4_status_t st = read_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        memcpy(block + block_off, src + done, take);
        st = write_block(mnt, fs_block, block);
        if (st != EXT4_OK) { result = st; goto out; }
        done += take;
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static u64 group_desc_table_block(const ext4_mount_t *mnt) {
    return mnt->block_size == 1024 ? 2 : 1;
}

static ext4_status_t read_group_desc(ext4_mount_t *mnt, u32 group, ext4_group_desc_disk_t *out) {
    if (!mnt || !out || group >= mnt->group_count) return EXT4_ERR_RANGE;
    usize desc_size = mnt->group_desc_size;
    if (desc_size < 32u || desc_size > sizeof(*out)) return EXT4_ERR_UNSUPPORTED;
    memset(out, 0, sizeof(*out));
    u64 table = 0;
    u64 group_off = 0;
    u64 off = 0;
    if (!checked_mul_u64(group_desc_table_block(mnt), mnt->block_size, &table)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)group, mnt->group_desc_size, &group_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table, group_off, &off)) return EXT4_ERR_RANGE;
    return read_metadata_bytes(mnt, off, out, desc_size);
}


static u16 group_desc_checksum(ext4_mount_t *mnt, u32 group, const ext4_group_desc_disk_t *in) {
    ext4_group_desc_disk_t tmp = *in;
    tmp.bg_checksum = 0;
    u32 seed = crc32(&mnt->sb.s_uuid, sizeof(mnt->sb.s_uuid));
    seed = crc32_update(seed, &group, sizeof(group));
    return (u16)crc32_update(seed, &tmp, mnt->group_desc_size);
}

static u32 super_checksum(const ext4_superblock_disk_t *sb) {
    ext4_superblock_disk_t tmp = *sb;
    tmp.s_checksum = 0;
    return crc32(&tmp, sizeof(tmp));
}

static ext4_status_t write_group_desc(ext4_mount_t *mnt, u32 group, const ext4_group_desc_disk_t *in) {
    if (!mnt || !in || group >= mnt->group_count) return EXT4_ERR_RANGE;
    usize desc_size = mnt->group_desc_size;
    if (desc_size < 32u || desc_size > sizeof(*in)) return EXT4_ERR_UNSUPPORTED;
    ext4_group_desc_disk_t tmp = *in;
    tmp.bg_checksum = group_desc_checksum(mnt, group, &tmp);
    u64 table = 0, group_off = 0, off = 0;
    if (!checked_mul_u64(group_desc_table_block(mnt), mnt->block_size, &table)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)group, mnt->group_desc_size, &group_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table, group_off, &off)) return EXT4_ERR_RANGE;
    return write_metadata_bytes(mnt, off, &tmp, desc_size);
}

static u32 group_valid_blocks(const ext4_mount_t *mnt, u32 group) {
    u64 start = (u64)group * mnt->blocks_per_group;
    if (start >= mnt->blocks_count) return 0;
    u64 remain = mnt->blocks_count - start;
    return (u32)(remain < mnt->blocks_per_group ? remain : mnt->blocks_per_group);
}

static u32 group_valid_inodes(const ext4_mount_t *mnt, u32 group) {
    u64 start = (u64)group * mnt->inodes_per_group;
    if (start >= mnt->inodes_count) return 0;
    u64 remain = (u64)mnt->inodes_count - start;
    return (u32)(remain < mnt->inodes_per_group ? remain : mnt->inodes_per_group);
}

static u64 group_block_bitmap(const ext4_group_desc_disk_t *gd) { return le64_from_lo_hi(gd->bg_block_bitmap_lo, gd->bg_block_bitmap_hi); }
static u64 group_inode_bitmap(const ext4_group_desc_disk_t *gd) { return le64_from_lo_hi(gd->bg_inode_bitmap_lo, gd->bg_inode_bitmap_hi); }
static u32 gd_free_blocks(const ext4_group_desc_disk_t *gd) { return ((u32)le16(gd->bg_free_blocks_count_hi) << 16) | le16(gd->bg_free_blocks_count_lo); }
static u32 gd_free_inodes(const ext4_group_desc_disk_t *gd) { return ((u32)le16(gd->bg_free_inodes_count_hi) << 16) | le16(gd->bg_free_inodes_count_lo); }
static void gd_set_free_blocks(ext4_group_desc_disk_t *gd, u32 v) { gd->bg_free_blocks_count_lo = (u16)v; gd->bg_free_blocks_count_hi = (u16)(v >> 16); }
static void gd_set_free_inodes(ext4_group_desc_disk_t *gd, u32 v) { gd->bg_free_inodes_count_lo = (u16)v; gd->bg_free_inodes_count_hi = (u16)(v >> 16); }
static void gd_set_used_dirs(ext4_group_desc_disk_t *gd, u32 v) { gd->bg_used_dirs_count_lo = (u16)v; gd->bg_used_dirs_count_hi = (u16)(v >> 16); }
static u32 gd_used_dirs(const ext4_group_desc_disk_t *gd) { return ((u32)le16(gd->bg_used_dirs_count_hi) << 16) | le16(gd->bg_used_dirs_count_lo); }
static u64 sb_free_blocks(ext4_mount_t *mnt) { return le64_from_lo_hi(mnt->sb.s_free_blocks_count_lo, mnt->sb.s_free_blocks_count_hi); }
static void sb_set_free_blocks(ext4_mount_t *mnt, u64 v) { mnt->sb.s_free_blocks_count_lo = (u32)v; mnt->sb.s_free_blocks_count_hi = (u32)(v >> 32); }
static u32 sb_free_inodes(ext4_mount_t *mnt) { return le32(mnt->sb.s_free_inodes_count); }
static void sb_set_free_inodes(ext4_mount_t *mnt, u32 v) { mnt->sb.s_free_inodes_count = v; }
static ext4_status_t write_super(ext4_mount_t *mnt) {
    mnt->sb.s_checksum = super_checksum(&mnt->sb);
    return write_metadata_bytes(mnt, 1024, &mnt->sb, sizeof(mnt->sb));
}

static u64 group_inode_table(const ext4_group_desc_disk_t *gd) {
    return le64_from_lo_hi(gd->bg_inode_table_lo, gd->bg_inode_table_hi);
}

u64 ext4_inode_size(const ext4_inode_disk_t *inode) {
    if (!inode) return 0;
    return ((u64)le32(inode->i_size_high) << 32) | le32(inode->i_size_lo);
}

bool ext4_inode_is_dir(const ext4_inode_disk_t *inode) { return inode && ((le16(inode->i_mode) & 0xf000u) == EXT4_IFDIR); }
bool ext4_inode_is_regular(const ext4_inode_disk_t *inode) { return inode && ((le16(inode->i_mode) & 0xf000u) == EXT4_IFREG); }
bool ext4_inode_is_symlink(const ext4_inode_disk_t *inode) { return inode && ((le16(inode->i_mode) & 0xf000u) == EXT4_IFLNK); }

bool ext4_inode_uses_extents(const ext4_inode_disk_t *inode) {
    return inode && ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0);
}

u16 ext4_inode_extent_depth(const ext4_inode_disk_t *inode) {
    if (!ext4_inode_uses_extents(inode)) return 0xffffu;
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return 0xffffu;
    return le16(hdr->eh_depth);
}

u16 ext4_inode_extent_root_entries(const ext4_inode_disk_t *inode) {
    if (!ext4_inode_uses_extents(inode)) return 0;
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return 0;
    return le16(hdr->eh_entries);
}

static ext4_status_t inspect_extent_array(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext, ext4_extent_report_t *report) {
    if (!mnt || !hdr || !ext || !report) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    if (entries > max_entries) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        bool unwritten = extent_is_unwritten(&ext[i]);
        if (len == 0 || __builtin_add_overflow(first, len, &end) || (i && first < prev_end)) {
            ++report->errors;
            return EXT4_ERR_CORRUPT;
        }
        u64 start = extent_start_block(&ext[i]);
        if (start >= mnt->blocks_count || len > mnt->blocks_count || start + len > mnt->blocks_count) {
            ++report->errors;
            return EXT4_ERR_RANGE;
        }
        ++report->extent_entries;
        report->data_blocks += len;
        if (unwritten) {
            ++report->unwritten_extents;
            report->unwritten_blocks += len;
        }
        prev_end = end;
    }
    return EXT4_OK;
}

static ext4_status_t inspect_extent_node(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 expected_depth, ext4_extent_report_t *report, u32 *first_out) {
    if (!mnt || !node || !report) return EXT4_ERR_RANGE;
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u16 depth = le16(hdr->eh_depth);
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize cap = (node_bytes - sizeof(*hdr)) / (depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (depth != expected_depth || depth > EXT4_MAX_EXTENT_DEPTH || entries > max_entries || max_entries > cap) { ++report->errors; return EXT4_ERR_CORRUPT; }
    if (depth > report->max_depth_seen) report->max_depth_seen = depth;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        if (entries && first_out) *first_out = le32(ext[0].ee_block);
        return inspect_extent_array(mnt, hdr, ext, report);
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    u32 prev_first = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (i && first <= prev_first) { ++report->errors; return EXT4_ERR_CORRUPT; }
        if (child >= mnt->blocks_count || ext4_block_is_journal(mnt, child)) { ++report->errors; return EXT4_ERR_RANGE; }
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        ++report->metadata_blocks;
        if (depth == 1) ++report->leaf_nodes;
        else ++report->index_nodes;
        u32 child_first = 0;
        st = inspect_extent_node(mnt, block, (usize)mnt->block_size, depth - 1u, report, &child_first);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
        if (entries && first != child_first) { ++report->errors; return EXT4_ERR_CORRUPT; }
        if (i == 0 && first_out) *first_out = first;
        prev_first = first;
    }
    return EXT4_OK;
}

ext4_status_t ext4_inspect_inode_extents(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_extent_report_t *report) {
    if (!mnt || !inode || !report) return EXT4_ERR_RANGE;
    memset(report, 0, sizeof(*report));
    report->depth = 0xffffu;
    if (!ext4_inode_uses_extents(inode)) return EXT4_OK;
    report->uses_extents = true;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC) { ++report->errors; return EXT4_ERR_CORRUPT; }
    report->depth = le16(root->eh_depth);
    report->root_entries = le16(root->eh_entries);
    report->root_capacity = le16(root->eh_max);
    if (report->root_entries > report->root_capacity) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u32 first = 0;
    return inspect_extent_node(mnt, root, EXT4_N_BLOCKS * sizeof(u32), report->depth, report, &first);
}

const char *ext4_status_name(ext4_status_t status) {
    switch (status) {
        case EXT4_OK: return "ok";
        case EXT4_ERR_IO: return "io";
        case EXT4_ERR_BAD_MAGIC: return "bad-magic";
        case EXT4_ERR_UNSUPPORTED: return "unsupported";
        case EXT4_ERR_RANGE: return "range";
        case EXT4_ERR_CORRUPT: return "corrupt";
        case EXT4_ERR_NOT_FOUND: return "not-found";
        case EXT4_ERR_NOT_DIR: return "not-dir";
        case EXT4_ERR_NO_MEMORY: return "no-memory";
        case EXT4_ERR_EXIST: return "exists";
        case EXT4_ERR_NOT_EMPTY: return "not-empty";
        default: return "unknown";
    }
}

static ext4_status_t block_bitmap_is_allocated(ext4_mount_t *mnt, u64 block, bool *allocated_out) {
    if (!mnt || !allocated_out || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    if (group >= mnt->group_count) return EXT4_ERR_RANGE;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st == EXT4_OK) *allocated_out = bitmap_get(bm, bit);
    ext4_tmp_block_free(bm);
    return st;
}

static ext4_status_t validate_extent_array_allocations(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext, ext4_fsck_report_t *report) {
    if (!mnt || !hdr || !ext || !report) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u64 start = extent_start_block(&ext[i]);
        u32 len = extent_len_blocks(&ext[i]);
        for (u32 j = 0; j < len; ++j) {
            bool allocated = false;
            ext4_status_t st = block_bitmap_is_allocated(mnt, start + j, &allocated);
            if (st != EXT4_OK) return st;
            if (!allocated) { ++report->errors; return EXT4_ERR_CORRUPT; }
            ++report->extent_data_blocks;
        }
    }
    return EXT4_OK;
}

static ext4_status_t validate_extent_node_allocations(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 expected_depth, ext4_fsck_report_t *report) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != expected_depth) { ++report->errors; return EXT4_ERR_CORRUPT; }
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize cap = (node_bytes - sizeof(*hdr)) / (expected_depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || max_entries > cap) { ++report->errors; return EXT4_ERR_CORRUPT; }
    if (expected_depth == 0) return validate_extent_array_allocations(mnt, hdr, (const ext4_extent_t *)(hdr + 1), report);
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < entries; ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        bool allocated = false;
        ext4_status_t st = block_bitmap_is_allocated(mnt, child, &allocated);
        if (st != EXT4_OK) return st;
        if (!allocated) { ++report->errors; return EXT4_ERR_CORRUPT; }
        ++report->extent_metadata_blocks;
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = validate_extent_node_allocations(mnt, block, (usize)mnt->block_size, expected_depth - 1u, report);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static ext4_status_t validate_inode_extent_allocations(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_fsck_report_t *report) {
    if (!mnt || !inode || !report || !ext4_inode_uses_extents(inode)) return EXT4_OK;
    ext4_extent_report_t er;
    ext4_status_t st = ext4_inspect_inode_extents(mnt, inode, &er);
    if (st != EXT4_OK) { ++report->errors; return st; }
    if (er.errors) { report->errors += er.errors; return EXT4_ERR_CORRUPT; }
    ++report->extent_inodes;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    return validate_extent_node_allocations(mnt, root, EXT4_N_BLOCKS * sizeof(u32), le16(root->eh_depth), report);
}

static ext4_status_t validate_htree_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, ext4_fsck_report_t *report) {
    if (!mnt || !inode || !report || !ext4_inode_is_dir(inode) || (le32(inode->i_flags) & EXT4_INDEX_FL) == 0) return EXT4_OK;
    u32 blockno = le32(inode->i_file_acl_lo);
    if (!blockno) { ++report->htree_errors; ++report->errors; return EXT4_ERR_CORRUPT; }
    bool allocated = false;
    ext4_status_t st = block_bitmap_is_allocated(mnt, blockno, &allocated);
    if (st != EXT4_OK) return st;
    if (!allocated) { ++report->htree_errors; ++report->errors; return EXT4_ERR_CORRUPT; }
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *dir_block = ext4_tmp_block(mnt);
    if (!dir_block) { ext4_tmp_block_free(block); return EXT4_ERR_NO_MEMORY; }
    aurora_htree_header_t *hdr = 0;
    aurora_htree_entry_t *entries = 0;
    st = htree_load(mnt, inode, block, &hdr, &entries);
    if (st != EXT4_OK) { ++report->htree_errors; ++report->errors; goto out; }
    ++report->htree_dirs;
    report->htree_entries += le16(hdr->entries);
    u32 prev_hash = 0;
    for (u16 i = 0; i < le16(hdr->entries); ++i) {
        u32 h = le32(entries[i].hash);
        if (i && h < prev_hash) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        if (le16(entries[i].offset) >= mnt->block_size) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        u64 phys = 0;
        st = map_inode_block(mnt, inode, le16(entries[i].logical_block), &phys);
        if (st != EXT4_OK) { ++report->htree_errors; ++report->errors; goto out; }
        st = read_block(mnt, phys, dir_block);
        if (st != EXT4_OK) goto out;
        u16 off = le16(entries[i].offset);
        if ((usize)off + 8u > mnt->block_size) { ++report->htree_errors; ++report->errors; st = EXT4_ERR_CORRUPT; goto out; }
        const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(dir_block + off);
        u16 rec_len = le16(de->rec_len);
        if (rec_len < 8u || (usize)off + rec_len > mnt->block_size || de->inode == 0 ||
            de->name_len != entries[i].name_len || de->file_type != entries[i].file_type ||
            le32(de->inode) != le32(entries[i].inode) || 8u + de->name_len > rec_len ||
            htree_hash_name(de->name, de->name_len) != h) {
            ++report->htree_errors;
            ++report->errors;
            st = EXT4_ERR_CORRUPT;
            goto out;
        }
        prev_hash = h;
    }
    st = EXT4_OK;
out:
    ext4_tmp_block_free(dir_block);
    ext4_tmp_block_free(block);
    return st;
}


static ext4_status_t validate_dirent_rec_len_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_fsck_report_t *report) {
    if (!mnt || !dir || !report || !ext4_inode_is_dir(dir)) return EXT4_OK;
    u64 size = ext4_inode_size(dir);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, dir, (u32)(off / mnt->block_size), &phys);
        if (st != EXT4_OK) { ++report->errors; result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || (rec_len & 3u) != 0 || p + rec_len > mnt->block_size) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && (de->name_len == 0 || ext4_rec_len(de->name_len) > rec_len)) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            if (!de->inode && (de->name_len != 0 || de->file_type != 0)) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
            p += rec_len;
        }
        if (p != mnt->block_size) { ++report->errors; result = EXT4_ERR_CORRUPT; goto out; }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t ext4_recount_free_counters(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u64 total_free_blocks = 0;
    u32 total_free_inodes = 0;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, group_block_bitmap(&gd), bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_blocks = 0;
        u32 valid_blocks = group_valid_blocks(mnt, group);
        for (u32 bit = 0; bit < valid_blocks; ++bit) {
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < le32(mnt->sb.s_first_data_block)) continue;
            if (!bitmap_get(bm, bit)) ++free_blocks;
        }
        st = read_block(mnt, group_inode_bitmap(&gd), bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_inodes = 0;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) ++free_inodes;
        }
        if (free_blocks != gd_free_blocks(&gd) || free_inodes != gd_free_inodes(&gd)) {
            gd_set_free_blocks(&gd, free_blocks);
            gd_set_free_inodes(&gd, free_inodes);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) { result = st; goto out; }
        }
        total_free_blocks += free_blocks;
        total_free_inodes += free_inodes;
    }
    if (total_free_blocks != sb_free_blocks(mnt) || total_free_inodes != sb_free_inodes(mnt)) {
        sb_set_free_blocks(mnt, total_free_blocks);
        sb_set_free_inodes(mnt, total_free_inodes);
        result = write_super(mnt);
        goto out;
    }
out:
    ext4_tmp_block_free(bm);
    return result;
}

ext4_status_t ext4_validate_metadata(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt || !report) return EXT4_ERR_RANGE;
    ext4_status_t flush_st = ext4_flush_and_invalidate_read_caches(mnt);
    if (flush_st != EXT4_OK) return flush_st;
    memset(report, 0, sizeof(*report));
    report->sb_free_blocks = sb_free_blocks(mnt);
    report->sb_free_inodes = sb_free_inodes(mnt);
    report->orphan_recovered = mnt->recovered_orphans;
    report->journal_replays = mnt->recovered_journal_replays;
    if (mnt->sb.s_checksum && mnt->sb.s_checksum != super_checksum(&mnt->sb)) { ++report->checksum_errors; ++report->errors; }
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u32 inode_table_blocks = (u32)div_round_up_u64((u64)mnt->inodes_per_group * mnt->inode_size, mnt->block_size);
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        ++report->checked_groups;
        if (gd.bg_checksum && gd.bg_checksum != group_desc_checksum(mnt, group, &gd)) { ++report->checksum_errors; ++report->errors; }
        u64 block_bm = group_block_bitmap(&gd);
        u64 inode_bm = group_inode_bitmap(&gd);
        u64 inode_table = group_inode_table(&gd);
        if (block_bm >= mnt->blocks_count || inode_bm >= mnt->blocks_count || inode_table >= mnt->blocks_count) {
            ++report->errors;
            result = EXT4_ERR_CORRUPT;
            goto out;
        }
        if (inode_table_blocks && inode_table + inode_table_blocks > mnt->blocks_count) {
            ++report->errors;
            result = EXT4_ERR_CORRUPT;
            goto out;
        }
        st = read_block(mnt, block_bm, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_blocks = 0;
        u32 valid_blocks = group_valid_blocks(mnt, group);
        for (u32 bit = 0; bit < valid_blocks; ++bit) {
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < le32(mnt->sb.s_first_data_block)) continue;
            if (!bitmap_get(bm, bit)) ++free_blocks;
        }
        report->bitmap_free_blocks += free_blocks;
        if (free_blocks != gd_free_blocks(&gd)) ++report->errors;

        st = read_block(mnt, inode_bm, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        u32 free_inodes = 0;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) ++free_inodes;
        }
        report->bitmap_free_inodes += free_inodes;
        if (free_inodes != gd_free_inodes(&gd)) ++report->errors;

        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) continue;
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino == 0 || ino > mnt->inodes_count) continue;
            ext4_inode_disk_t inode;
            st = ext4_read_inode(mnt, ino, &inode);
            if (st != EXT4_OK) { result = st; goto out; }
            if (inode.i_mode == 0) continue;
            ++report->checked_inodes;
            st = validate_dirent_rec_len_inode(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
            st = validate_inode_extent_allocations(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
            st = validate_htree_inode(mnt, &inode, report);
            if (st != EXT4_OK) { result = st; goto out; }
        }
    }
    if (report->bitmap_free_blocks != report->sb_free_blocks) ++report->errors;
    if (report->bitmap_free_inodes != report->sb_free_inodes) ++report->errors;
    result = report->errors ? EXT4_ERR_CORRUPT : EXT4_OK;
out:
    ext4_tmp_block_free(bm);
    return result;
}

static u64 default_partition_sectors(block_device_t *dev, u64 partition_lba) {
    if (!dev || dev->sector_count == 0) return 0;
    if (partition_lba >= dev->sector_count) return 0;
    return dev->sector_count - partition_lba;
}

ext4_status_t ext4_mount_bounded(block_device_t *dev, u64 partition_lba, u64 partition_sectors, ext4_mount_t *out) {
    if (!dev || !out) return EXT4_ERR_IO;
    if (dev->sector_size && dev->sector_size != 512u) return EXT4_ERR_UNSUPPORTED;
    if (dev->sector_count) {
        if (partition_lba >= dev->sector_count) return EXT4_ERR_RANGE;
        if (partition_sectors == 0 || partition_sectors > dev->sector_count - partition_lba) return EXT4_ERR_RANGE;
    }
    memset(out, 0, sizeof(*out));
    ext4_superblock_disk_t sb;
    ext4_status_t st = read_bytes(dev, partition_lba, partition_sectors, 1024, &sb, sizeof(sb));
    if (st != EXT4_OK) return st;
    if (le16(sb.s_magic) != EXT4_SUPER_MAGIC) return EXT4_ERR_BAD_MAGIC;
    u32 log_block = le32(sb.s_log_block_size);
    if (log_block > 2u) return EXT4_ERR_UNSUPPORTED;
    u64 block_size = 1024ull << log_block;
    if (block_size < 1024 || block_size > MAX_BLOCK_SIZE) return EXT4_ERR_UNSUPPORTED;
    u64 blocks_count = le64_from_lo_hi(sb.s_blocks_count_lo, sb.s_blocks_count_hi);
    if (partition_sectors) {
        u64 fs_bytes = 0;
        u64 part_bytes = 0;
        if (!checked_mul_u64(blocks_count, block_size, &fs_bytes)) return EXT4_ERR_RANGE;
        if (!checked_mul_u64(partition_sectors, 512u, &part_bytes)) return EXT4_ERR_RANGE;
        if (fs_bytes > part_bytes) return EXT4_ERR_RANGE;
    }
    u32 incompat = le32(sb.s_feature_incompat);
    u32 supported = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT;
    if ((incompat & ~supported) != 0) return EXT4_ERR_UNSUPPORTED;
    out->dev = dev;
    out->partition_lba = partition_lba;
    out->partition_sectors = partition_sectors;
    out->block_size = block_size;
    out->blocks_count = blocks_count;
    out->inodes_count = le32(sb.s_inodes_count);
    out->inodes_per_group = le32(sb.s_inodes_per_group);
    out->blocks_per_group = le32(sb.s_blocks_per_group);
    out->inode_size = le16(sb.s_inode_size) ? le16(sb.s_inode_size) : 128;
    out->group_desc_size = (incompat & EXT4_FEATURE_INCOMPAT_64BIT) ? le16(sb.s_desc_size) : 32;
    u64 first_data = le32(sb.s_first_data_block);
    if (!out->blocks_count || !out->inodes_count || !out->blocks_per_group || !out->inodes_per_group) return EXT4_ERR_CORRUPT;
    if (first_data > out->blocks_count) return EXT4_ERR_CORRUPT;
    if (out->inode_size < 128u || out->inode_size > block_size) return EXT4_ERR_UNSUPPORTED;
    if (out->group_desc_size < 32 || out->group_desc_size > sizeof(ext4_group_desc_disk_t) || (out->group_desc_size % 8u) != 0) return EXT4_ERR_UNSUPPORTED;
    out->group_count = (u32)div_round_up_u64(out->blocks_count - first_data, out->blocks_per_group);
    if (out->group_count == 0) return EXT4_ERR_CORRUPT;
    out->sb = sb;
    out->allocator_next_block = first_data;
    data_cache_discard_clean_mount(out);
    write_cache_discard_clean_mount(out);
    if (out->blocks_count > first_data + 8u) {
        out->journal_blocks = 2u;
        out->journal_first_block = out->blocks_count - out->journal_blocks;
        out->journal_ready = false;
        st = ext4_recover(out, 0);
        if (st != EXT4_OK) return st;
        for (u32 i = 0; i < out->journal_blocks; ++i) {
            st = reserve_block_if_free(out, out->journal_first_block + i);
            if (st != EXT4_OK) return st;
        }
        out->journal_ready = true;
        st = ext4_recount_free_counters(out);
        if (st != EXT4_OK) return st;
        st = journal_clear(out);
        if (st != EXT4_OK) return st;
    }
    if (out->sb.s_checksum == 0 || out->sb.s_checksum != super_checksum(&out->sb)) {
        st = write_super(out);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}


ext4_status_t ext4_sync_metadata(ext4_mount_t *mnt) {
    if (!mnt) return EXT4_ERR_RANGE;
    ext4_status_t st = ext4_flush_data_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    st = write_super(mnt);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    return journal_clear(mnt);
}

ext4_status_t ext4_sync_file(ext4_mount_t *mnt, u32 ino, bool data_only) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_regular(&inode) && !ext4_inode_is_dir(&inode)) return EXT4_ERR_UNSUPPORTED;
    st = ext4_flush_data_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    st = ext4_flush_write_cache_for_mount(mnt);
    if (st != EXT4_OK) return st;
    if (!data_only) {
        st = write_super(mnt);
        if (st != EXT4_OK) return st;
    }
    return journal_clear(mnt);
}

ext4_status_t ext4_get_perf_stats(ext4_mount_t *mnt, ext4_perf_stats_t *out) {
    if (!mnt || !out) return EXT4_ERR_RANGE;
    *out = g_ext4_perf_stats;
    out->cache_slots = g_ext4_write_cache_slots ? g_ext4_write_cache_slots : EXT4_WRITE_CACHE_SLOTS;
    out->dirty_slots = 0;
    if (g_ext4_write_cache && g_ext4_write_cache_slots) {
        for (u32 i = 0; i < g_ext4_write_cache_slots; ++i) {
            ext4_write_cache_slot_t *slot = &g_ext4_write_cache[i];
            if (slot->valid && slot->dirty && slot->dev == mnt->dev &&
                slot->partition_lba == mnt->partition_lba &&
                slot->partition_sectors == mnt->partition_sectors &&
                slot->block_size == (u32)mnt->block_size) ++out->dirty_slots;
        }
    }
    if (g_ext4_data_cache && g_ext4_data_cache_slots) {
        for (u32 i = 0; i < g_ext4_data_cache_slots; ++i) {
            ext4_data_cache_slot_t *slot = &g_ext4_data_cache[i];
            if (slot->valid && slot->dirty && slot->dev == mnt->dev &&
                slot->partition_lba == mnt->partition_lba &&
                slot->partition_sectors == mnt->partition_sectors &&
                slot->block_size == (u32)mnt->block_size) ++out->dirty_slots;
        }
    }
    return EXT4_OK;
}

ext4_status_t ext4_mount(block_device_t *dev, u64 partition_lba, ext4_mount_t *out) {
    return ext4_mount_bounded(dev, partition_lba, default_partition_sectors(dev, partition_lba), out);
}

ext4_status_t ext4_read_inode(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *out) {
    if (!mnt || !out || ino == 0) return EXT4_ERR_RANGE;
    u32 idx = ino - 1;
    u32 group = idx / mnt->inodes_per_group;
    u32 local = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u64 table = group_inode_table(&gd);
    u64 table_off = 0;
    u64 inode_off = 0;
    u64 off = 0;
    if (!checked_mul_u64(table, mnt->block_size, &table_off)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)local, mnt->inode_size, &inode_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table_off, inode_off, &off)) return EXT4_ERR_RANGE;
    memset(out, 0, sizeof(*out));
    usize bytes = mnt->inode_size < sizeof(*out) ? mnt->inode_size : sizeof(*out);
    return read_metadata_bytes(mnt, off, out, bytes);
}

static ext4_status_t map_extent_tree(ext4_mount_t *mnt, const void *node, usize node_bytes, u32 logical, u64 *phys_out, u32 depth) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    u16 node_depth = le16(hdr->eh_depth);
    if (node_bytes < sizeof(ext4_extent_header_t)) return EXT4_ERR_CORRUPT;
    usize capacity = (node_bytes - sizeof(ext4_extent_header_t)) /
                     (node_depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || entries > capacity) return EXT4_ERR_CORRUPT;
    if (node_depth != depth) return EXT4_ERR_CORRUPT;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        for (u16 i = 0; i < entries; ++i) {
            u32 first = le32(ext[i].ee_block);
            u32 len = extent_len_blocks(&ext[i]);
            u32 end = 0;
            if (extent_is_unwritten(&ext[i])) continue;
            if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
            if (logical >= first && logical < end) {
                u64 start = extent_start_block(&ext[i]);
                u64 phys = 0;
                if (!checked_add_u64(start, (u64)(logical - first), &phys)) return EXT4_ERR_RANGE;
                if (phys >= mnt->blocks_count) return EXT4_ERR_RANGE;
                *phys_out = phys;
                return EXT4_OK;
            }
        }
        return EXT4_ERR_RANGE;
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    const ext4_extent_idx_t *best = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) best = &idx[i];
        else break;
    }
    if (!best) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u64 child = le64_from_lo_hi(best->ei_leaf_lo, best->ei_leaf_hi);
    ext4_status_t st = read_block(mnt, child, block);
    if (st == EXT4_OK) st = map_extent_tree(mnt, block, (usize)mnt->block_size, logical, phys_out, depth - 1);
    ext4_tmp_block_free(block);
    return st;
}

static ext4_status_t map_legacy_block(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u32 logical, u64 *phys_out) {
    if (logical < 12) {
        u32 b = le32(inode->i_block[logical]);
        if (!b) return EXT4_ERR_RANGE;
        *phys_out = b;
        return EXT4_OK;
    }
    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical < 12 + per_block) {
        u32 indirect_block = le32(inode->i_block[12]);
        if (!indirect_block) return EXT4_ERR_RANGE;
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, indirect_block, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        u32 *entries = (u32 *)block;
        u32 b = le32(entries[logical - 12]);
        if (!b) { ext4_tmp_block_free(block); return EXT4_ERR_RANGE; }
        *phys_out = b;
        ext4_tmp_block_free(block);
        return EXT4_OK;
    }
    return EXT4_ERR_UNSUPPORTED;
}

static ext4_status_t map_inode_block(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u32 logical, u64 *phys_out) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        u16 depth = le16(hdr->eh_depth);
        if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_CORRUPT;
        return map_extent_tree(mnt, hdr, EXT4_N_BLOCKS * sizeof(u32), logical, phys_out, depth);
    }
    return map_legacy_block(mnt, inode, logical, phys_out);
}

__attribute__((noinline)) ext4_status_t ext4_read_file(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u64 offset, void *buffer, usize bytes, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    u64 size = ext4_inode_size(inode);
    if (offset >= size || bytes == 0) return EXT4_OK;
    if (bytes > size - offset) bytes = (usize)(size - offset);
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *out = (u8 *)buffer;
    usize done = 0;
    ext4_status_t final = EXT4_OK;
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) { final = EXT4_ERR_RANGE; goto out; }
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) { final = EXT4_ERR_RANGE; goto out; }
        u32 logical = (u32)logical64;
        u32 block_off = (u32)(abs % mnt->block_size);
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, inode, logical, &phys);
        if (st == EXT4_ERR_RANGE) {
            memset(out + done, 0, take);
            done += take;
            continue;
        }
        if (st != EXT4_OK) { final = st; goto out; }
        st = read_data_block(mnt, phys, block);
        if (st != EXT4_OK) { final = st; goto out; }
        memcpy(out + done, block + block_off, take);
        done += take;
    }
    if (read_out) *read_out = done;
out:
    ext4_tmp_block_free(block);
    return final;
}


static ext4_status_t write_inode(ext4_mount_t *mnt, u32 ino, const ext4_inode_disk_t *in) {
    if (!mnt || !in || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1;
    u32 group = idx / mnt->inodes_per_group;
    u32 local = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u64 table = group_inode_table(&gd);
    u64 table_off = 0, inode_off = 0, off = 0;
    if (!checked_mul_u64(table, mnt->block_size, &table_off)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)local, mnt->inode_size, &inode_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table_off, inode_off, &off)) return EXT4_ERR_RANGE;
    usize bytes = mnt->inode_size < sizeof(*in) ? mnt->inode_size : sizeof(*in);
    return write_metadata_bytes(mnt, off, in, bytes);
}

static void inode_set_size(ext4_inode_disk_t *inode, u64 size) {
    inode->i_size_lo = (u32)size;
    inode->i_size_high = (u32)(size >> 32);
}

static void inode_add_blocks(ext4_mount_t *mnt, ext4_inode_disk_t *inode, i32 blocks) {
    u32 sectors = (u32)(mnt->block_size / 512u);
    if (blocks >= 0) inode->i_blocks_lo += sectors * (u32)blocks;
    else {
        u32 dec = sectors * (u32)(-blocks);
        inode->i_blocks_lo = inode->i_blocks_lo > dec ? inode->i_blocks_lo - dec : 0;
    }
}

static u64 allocator_first_data_block(const ext4_mount_t *mnt) {
    if (!mnt) return 0;
    u64 first = le32(mnt->sb.s_first_data_block);
    if (first >= mnt->blocks_count) return 0;
    return first;
}

static u64 allocator_normalize_goal(ext4_mount_t *mnt, u64 goal) {
    u64 first = allocator_first_data_block(mnt);
    if (goal < first || goal >= mnt->blocks_count || ext4_block_is_journal(mnt, goal)) {
        goal = mnt->allocator_next_block;
    }
    if (goal < first || goal >= mnt->blocks_count || ext4_block_is_journal(mnt, goal)) goal = first;
    return goal;
}

static void allocator_note_success(ext4_mount_t *mnt, u64 block) {
    if (!mnt) return;
    u64 first = allocator_first_data_block(mnt);
    u64 next = block + 1u;
    if (next >= mnt->blocks_count || ext4_block_is_journal(mnt, next)) next = first;
    mnt->allocator_next_block = next;
}

static ext4_status_t alloc_block_goal(ext4_mount_t *mnt, u64 goal, bool zero_fill, u64 *block_out) {
    if (!mnt || !block_out || mnt->blocks_per_group == 0 || mnt->group_count == 0) return EXT4_ERR_RANGE;
    goal = allocator_normalize_goal(mnt, goal);
    u32 start_group = (u32)(goal / mnt->blocks_per_group);
    u32 start_bit = (u32)(goal % mnt->blocks_per_group);
    u64 first_data = allocator_first_data_block(mnt);
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t final = EXT4_ERR_NO_MEMORY;
    for (u32 pass = 0; pass < mnt->group_count; ++pass) {
        u32 group = start_group + pass;
        if (group >= mnt->group_count) {
            group -= mnt->group_count;
            ++g_ext4_perf_stats.allocator_wraps;
        }
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { final = st; goto out; }
        if (gd_free_blocks(&gd) == 0) continue;
        u64 bitmap_block = group_block_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) { final = st; goto out; }
        u32 valid = group_valid_blocks(mnt, group);
        if (valid == 0) continue;
        u32 bit0 = (group == start_group) ? start_bit : 0u;
        for (u32 scan = 0; scan < valid; ++scan) {
            u32 bit = bit0 + scan;
            if (bit >= valid) bit -= valid;
            ++g_ext4_perf_stats.allocator_scans;
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < first_data || abs >= mnt->blocks_count) continue;
            if (ext4_block_is_journal(mnt, abs)) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) { final = st; goto out; }
            u32 gfree = gd_free_blocks(&gd);
            if (gfree) gd_set_free_blocks(&gd, gfree - 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) { final = st; goto out; }
            u64 sfree = sb_free_blocks(mnt);
            if (sfree) sb_set_free_blocks(mnt, sfree - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) { final = st; goto out; }
            if (zero_fill) {
                u8 *zero = ext4_tmp_block(mnt);
                if (!zero) { final = EXT4_ERR_NO_MEMORY; goto out; }
                memset(zero, 0, (usize)mnt->block_size);
                st = write_data_block(mnt, abs, zero);
                ext4_tmp_block_free(zero);
                if (st != EXT4_OK) { final = st; goto out; }
                ++g_ext4_perf_stats.zero_block_writes;
            } else {
                ++g_ext4_perf_stats.zero_block_skips;
            }
            allocator_note_success(mnt, abs);
            *block_out = abs;
            final = EXT4_OK;
            goto out;
        }
    }
out:
    ext4_tmp_block_free(bm);
    return final;
}

static ext4_status_t alloc_block(ext4_mount_t *mnt, u64 *block_out) {
    u64 goal = mnt ? mnt->allocator_next_block : 0;
    return alloc_block_goal(mnt, goal, true, block_out);
}

static ext4_status_t alloc_data_block(ext4_mount_t *mnt, u64 goal, bool zero_fill, u64 *block_out) {
    return alloc_block_goal(mnt, goal, zero_fill, block_out);
}

static ext4_status_t free_block(ext4_mount_t *mnt, u64 block) {
    if (!mnt || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    write_cache_discard_block(mnt, block);
    data_cache_discard_block(mnt, block);
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    if (!bitmap_get(bm, bit)) { st = EXT4_ERR_CORRUPT; goto out; }
    bitmap_clear8(bm, bit);
    st = write_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    gd_set_free_blocks(&gd, gd_free_blocks(&gd) + 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) goto out;
    sb_set_free_blocks(mnt, sb_free_blocks(mnt) + 1u);
    st = write_super(mnt);
out:
    ext4_tmp_block_free(bm);
    return st;
}



static ext4_status_t reserve_block_if_free(ext4_mount_t *mnt, u64 block) {
    if (!mnt || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    if (bitmap_get(bm, bit)) { st = EXT4_OK; goto out; }
    bitmap_set8(bm, bit);
    st = raw_write_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    u32 gfree = gd_free_blocks(&gd);
    if (gfree) gd_set_free_blocks(&gd, gfree - 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) goto out;
    u64 sfree = sb_free_blocks(mnt);
    if (sfree) sb_set_free_blocks(mnt, sfree - 1u);
    st = write_super(mnt);
out:
    ext4_tmp_block_free(bm);
    return st;
}

ext4_status_t ext4_recover(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (mnt->journal_blocks >= 2u && mnt->block_size <= MAX_BLOCK_SIZE) {
        u8 *header_block = ext4_tmp_block(mnt);
        if (!header_block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, mnt->journal_first_block, header_block);
        if (st == EXT4_OK) {
            aurora_ext4_journal_record_t jr;
            memcpy(&jr, header_block, sizeof(jr));
            u32 want = journal_header_crc(&jr);
            if (jr.magic == AURORA_EXT4_JOURNAL_MAGIC && jr.version == 1u && jr.state == 1u &&
                jr.block_size == (u32)mnt->block_size && jr.header_crc == want && jr.target_block < mnt->blocks_count &&
                !ext4_block_is_journal(mnt, jr.target_block)) {
                u8 *payload = ext4_tmp_block(mnt);
                if (!payload) { ext4_tmp_block_free(header_block); return EXT4_ERR_NO_MEMORY; }
                st = read_block(mnt, mnt->journal_first_block + 1u, payload);
                if (st == EXT4_OK && crc32(payload, (usize)mnt->block_size) == jr.payload_crc) {
                    st = raw_write_block(mnt, jr.target_block, payload);
                    if (st == EXT4_OK) {
                        ++mnt->recovered_journal_replays;
                        if (report) ++report->journal_replays;
                    }
                }
                ext4_tmp_block_free(payload);
                if (st != EXT4_OK) { ext4_tmp_block_free(header_block); return st; }
            }
        }
        ext4_tmp_block_free(header_block);
        st = journal_clear(mnt);
        if (st != EXT4_OK) return st;
    }
    ext4_status_t ost = ext4_recover_orphans(mnt, report);
    if (ost != EXT4_OK) return ost;
    return EXT4_OK;
}

static ext4_status_t alloc_inode(ext4_mount_t *mnt, bool dir, u32 *ino_out) {
    if (!mnt || !ino_out) return EXT4_ERR_RANGE;
    u32 first = le32(mnt->sb.s_first_ino);
    if (first == 0) first = 11;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_NO_MEMORY;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) { result = st; goto out; }
        if (gd_free_inodes(&gd) == 0) continue;
        u64 bitmap_block = group_inode_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) { result = st; goto out; }
        for (u32 bit = 0; bit < mnt->inodes_per_group; ++bit) {
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino < first || ino > mnt->inodes_count) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) { result = st; goto out; }
            gd_set_free_inodes(&gd, gd_free_inodes(&gd) - 1u);
            if (dir) gd_set_used_dirs(&gd, gd_used_dirs(&gd) + 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) { result = st; goto out; }
            sb_set_free_inodes(mnt, sb_free_inodes(mnt) - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) { result = st; goto out; }
            *ino_out = ino;
            result = EXT4_OK;
            goto out;
        }
    }
out:
    ext4_tmp_block_free(bm);
    return result;
}

static ext4_status_t free_inode(ext4_mount_t *mnt, u32 ino, bool dir) {
    if (!mnt || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1u;
    u32 group = idx / mnt->inodes_per_group;
    u32 bit = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    if (!bitmap_get(bm, bit)) { st = EXT4_ERR_CORRUPT; goto out; }
    bitmap_clear8(bm, bit);
    st = write_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) goto out;
    gd_set_free_inodes(&gd, gd_free_inodes(&gd) + 1u);
    if (dir && gd_used_dirs(&gd)) gd_set_used_dirs(&gd, gd_used_dirs(&gd) - 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) goto out;
    sb_set_free_inodes(mnt, sb_free_inodes(mnt) + 1u);
    st = write_super(mnt);
out:
    ext4_tmp_block_free(bm);
    return st;
}

static u16 extent_inline_capacity(void) {
    return (u16)((EXT4_N_BLOCKS * sizeof(u32) - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_t));
}

static void ext4_init_inline_extent_tree(ext4_inode_disk_t *inode) {
    memset(inode->i_block, 0, sizeof(inode->i_block));
    inode->i_flags |= EXT4_EXTENTS_FL;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    hdr->eh_magic = EXT4_EXT_MAGIC;
    hdr->eh_entries = 0;
    hdr->eh_max = extent_inline_capacity();
    hdr->eh_depth = 0;
    hdr->eh_generation = 0;
}

static ext4_status_t extent_inline_validate(ext4_inode_disk_t *inode, ext4_extent_header_t **hdr_out, ext4_extent_t **ext_out) {
    if (!inode || !hdr_out || !ext_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 0) return EXT4_ERR_UNSUPPORTED;
    u16 cap = extent_inline_capacity();
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
        if (i && first < prev_end) return EXT4_ERR_CORRUPT;
        if (extent_start_block(&ext[i]) >= ((u64)1 << 48)) return EXT4_ERR_RANGE;
        prev_end = end;
    }
    *hdr_out = hdr;
    *ext_out = ext;
    return EXT4_OK;
}

static u16 extent_root_index_capacity(void) {
    return (u16)((EXT4_N_BLOCKS * sizeof(u32) - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_idx_t));
}

static u16 extent_leaf_capacity(ext4_mount_t *mnt) {
    return (u16)((mnt->block_size - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_t));
}

static u16 extent_index_node_capacity(ext4_mount_t *mnt) {
    return (u16)((mnt->block_size - sizeof(ext4_extent_header_t)) / sizeof(ext4_extent_idx_t));
}

static ext4_status_t extent_leaf_validate(ext4_mount_t *mnt, void *block, ext4_extent_header_t **hdr_out, ext4_extent_t **ext_out) {
    if (!mnt || !block || !hdr_out || !ext_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 0) return EXT4_ERR_CORRUPT;
    u16 cap = extent_leaf_capacity(mnt);
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    u32 prev_end = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = 0;
        if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
        if (i && first < prev_end) return EXT4_ERR_CORRUPT;
        if (extent_start_block(&ext[i]) >= ((u64)1 << 48)) return EXT4_ERR_RANGE;
        prev_end = end;
    }
    *hdr_out = hdr;
    *ext_out = ext;
    return EXT4_OK;
}

static ext4_status_t extent_index_validate(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t **hdr_out, ext4_extent_idx_t **idx_out) {
    if (!mnt || !inode || !hdr_out || !idx_out) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != 1u) return EXT4_ERR_UNSUPPORTED;
    u16 cap = extent_root_index_capacity();
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(hdr + 1);
    u32 prev = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 leaf = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (leaf >= mnt->blocks_count) return EXT4_ERR_RANGE;
        if (i && first <= prev) return EXT4_ERR_CORRUPT;
        prev = first;
    }
    *hdr_out = hdr;
    *idx_out = idx;
    return EXT4_OK;
}

static ext4_status_t extent_index_node_validate(ext4_mount_t *mnt, void *block, u16 depth, ext4_extent_header_t **hdr_out, ext4_extent_idx_t **idx_out) {
    if (!mnt || !block || !hdr_out || !idx_out || depth == 0) return EXT4_ERR_RANGE;
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)block;
    if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    if (le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    u16 cap = extent_index_node_capacity(mnt);
    u16 max_entries = le16(hdr->eh_max);
    u16 entries = le16(hdr->eh_entries);
    if (max_entries == 0 || max_entries > cap) return EXT4_ERR_CORRUPT;
    if (entries > max_entries) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(hdr + 1);
    u32 prev = 0;
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(idx[i].ei_block);
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        if (child >= mnt->blocks_count) return EXT4_ERR_RANGE;
        if (i && first <= prev) return EXT4_ERR_CORRUPT;
        prev = first;
    }
    *hdr_out = hdr;
    *idx_out = idx;
    return EXT4_OK;
}

static bool extent_pair_can_merge(const ext4_extent_t *a, const ext4_extent_t *b) {
    if (!a || !b) return false;
    u32 a_first = le32(a->ee_block);
    u32 a_len = extent_len_blocks(a);
    u32 b_first = le32(b->ee_block);
    u32 b_len = extent_len_blocks(b);
    u64 a_start = extent_start_block(a);
    u64 b_start = extent_start_block(b);
    return a_len != 0 && b_len != 0 && a_len + b_len <= 32768u &&
           extent_is_unwritten(a) == extent_is_unwritten(b) &&
           a_first + a_len == b_first && a_start + a_len == b_start;
}

static void extent_array_merge_neighbors(ext4_extent_header_t *hdr, ext4_extent_t *ext, u16 pos) {
    u16 entries = le16(hdr->eh_entries);
    if (entries == 0) return;
    if (pos >= entries) pos = entries - 1u;
    if (pos > 0 && extent_pair_can_merge(&ext[pos - 1u], &ext[pos])) {
        u32 left_len = extent_len_blocks(&ext[pos - 1u]);
        u32 right_len = extent_len_blocks(&ext[pos]);
        bool unwritten = extent_is_unwritten(&ext[pos - 1u]);
        extent_set_len_blocks_state(&ext[pos - 1u], left_len + right_len, unwritten);
        for (u16 i = pos; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
        memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
        hdr->eh_entries = --entries;
        --pos;
    }
    if (pos + 1u < entries && extent_pair_can_merge(&ext[pos], &ext[pos + 1u])) {
        u32 left_len = extent_len_blocks(&ext[pos]);
        u32 right_len = extent_len_blocks(&ext[pos + 1u]);
        bool unwritten = extent_is_unwritten(&ext[pos]);
        extent_set_len_blocks_state(&ext[pos], left_len + right_len, unwritten);
        for (u16 i = pos + 1u; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
        memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
        hdr->eh_entries = entries - 1u;
    }
}

static ext4_status_t extent_array_convert_unwritten_block(ext4_mount_t *mnt, ext4_extent_header_t *hdr, ext4_extent_t *ext, u16 pos, u32 logical, u64 *phys_out) {
    if (!mnt || !hdr || !ext || !phys_out) return EXT4_ERR_RANGE;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    if (pos >= entries || !extent_is_unwritten(&ext[pos])) return EXT4_ERR_CORRUPT;
    u32 first = le32(ext[pos].ee_block);
    u32 len = extent_len_blocks(&ext[pos]);
    u32 end = first + len;
    if (len == 0 || logical < first || logical >= end) return EXT4_ERR_CORRUPT;
    u64 start = extent_start_block(&ext[pos]);
    u64 phys = start + (u64)(logical - first);
    if (phys >= mnt->blocks_count) return EXT4_ERR_RANGE;

    ext4_extent_t pieces[3];
    memset(pieces, 0, sizeof(pieces));
    u16 n = 0;
    if (logical > first) {
        pieces[n].ee_block = first;
        extent_set_start_block(&pieces[n], start);
        extent_set_len_blocks_state(&pieces[n], logical - first, true);
        ++n;
    }
    pieces[n].ee_block = logical;
    extent_set_start_block(&pieces[n], phys);
    extent_set_len_blocks_state(&pieces[n], 1u, false);
    ++n;
    if (logical + 1u < end) {
        pieces[n].ee_block = logical + 1u;
        extent_set_start_block(&pieces[n], phys + 1u);
        extent_set_len_blocks_state(&pieces[n], end - logical - 1u, true);
        ++n;
    }
    u16 add = (u16)(n - 1u);
    if ((u32)entries + add > max_entries) return EXT4_ERR_UNSUPPORTED;
    for (u16 i = entries + add; i > pos + n - 1u; --i) ext[i] = ext[i - add];
    for (u16 i = 0; i < n; ++i) ext[pos + i] = pieces[i];
    hdr->eh_entries = entries + add;

    u8 *zero = ext4_tmp_block(mnt);
    if (!zero) return EXT4_ERR_NO_MEMORY;
    memset(zero, 0, (usize)mnt->block_size);
    ext4_status_t st = write_data_block(mnt, phys, zero);
    ext4_tmp_block_free(zero);
    if (st != EXT4_OK) return st;
    ++g_ext4_perf_stats.unwritten_conversions;
    ++g_ext4_perf_stats.unwritten_zero_fills;
    extent_array_merge_neighbors(hdr, ext, pos);
    *phys_out = phys;
    return EXT4_OK;
}

static ext4_status_t extent_array_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *hdr, ext4_extent_t *ext, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    u16 pos = 0;
    for (; pos < entries; ++pos) {
        u32 first = le32(ext[pos].ee_block);
        u32 len = extent_len_blocks(&ext[pos]);
        u32 end = first + len;
        if (logical >= first && logical < end) {
            u64 start = extent_start_block(&ext[pos]);
            if (!checked_add_u64(start, (u64)(logical - first), phys_out)) return EXT4_ERR_RANGE;
            if (*phys_out >= mnt->blocks_count) return EXT4_ERR_RANGE;
            if (extent_is_unwritten(&ext[pos])) {
                if (create_unwritten) return EXT4_OK;
                if (!allocate) return EXT4_ERR_RANGE;
                return extent_array_convert_unwritten_block(mnt, hdr, ext, pos, logical, phys_out);
            }
            return EXT4_OK;
        }
        if (logical < first) break;
    }
    if (!allocate) return EXT4_ERR_RANGE;
    u64 goal = mnt->allocator_next_block;
    if (pos > 0) {
        const ext4_extent_t *prev = &ext[pos - 1u];
        u32 prev_first = le32(prev->ee_block);
        u32 prev_len = extent_len_blocks(prev);
        u64 prev_start = extent_start_block(prev);
        if (prev_first + prev_len == logical) goal = prev_start + prev_len;
    } else if (pos < entries) {
        const ext4_extent_t *next = &ext[pos];
        u64 next_start = extent_start_block(next);
        if (le32(next->ee_block) == logical + 1u && next_start > allocator_first_data_block(mnt)) goal = next_start - 1u;
    }
    u64 nb = 0;
    ext4_status_t st = alloc_data_block(mnt, goal, zero_new && !create_unwritten, &nb);
    if (st != EXT4_OK) return st;
    if (create_unwritten) ++g_ext4_perf_stats.unwritten_allocations;

    bool merged_prev = false;
    if (pos > 0) {
        ext4_extent_t *prev = &ext[pos - 1u];
        u32 prev_first = le32(prev->ee_block);
        u32 prev_len = extent_len_blocks(prev);
        u64 prev_start = extent_start_block(prev);
        if (extent_is_unwritten(prev) == create_unwritten && prev_len < 32768u && prev_first + prev_len == logical && prev_start + prev_len == nb) {
            extent_set_len_blocks_state(prev, prev_len + 1u, create_unwritten);
            merged_prev = true;
        }
    }
    if (merged_prev) {
        if (pos < entries) {
            ext4_extent_t *prev = &ext[pos - 1u];
            ext4_extent_t *next = &ext[pos];
            if (extent_pair_can_merge(prev, next)) {
                u32 prev_len = extent_len_blocks(prev);
                u32 next_len = extent_len_blocks(next);
                extent_set_len_blocks_state(prev, prev_len + next_len, create_unwritten);
                for (u16 i = pos; i + 1u < entries; ++i) ext[i] = ext[i + 1u];
                memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
                hdr->eh_entries = entries - 1u;
            }
        }
        inode_add_blocks(mnt, inode, 1);
        *phys_out = nb;
        return EXT4_OK;
    }
    if (pos < entries) {
        ext4_extent_t *next = &ext[pos];
        u32 next_first = le32(next->ee_block);
        u32 next_len = extent_len_blocks(next);
        u64 next_start = extent_start_block(next);
        if (extent_is_unwritten(next) == create_unwritten && logical + 1u == next_first && nb + 1u == next_start && next_len < 32768u) {
            next->ee_block = logical;
            extent_set_start_block(next, nb);
            extent_set_len_blocks_state(next, next_len + 1u, create_unwritten);
            inode_add_blocks(mnt, inode, 1);
            *phys_out = nb;
            return EXT4_OK;
        }
    }
    if (entries >= max_entries) {
        (void)free_block(mnt, nb);
        return EXT4_ERR_UNSUPPORTED;
    }
    for (u16 i = entries; i > pos; --i) ext[i] = ext[i - 1u];
    memset(&ext[pos], 0, sizeof(ext[pos]));
    ext[pos].ee_block = logical;
    extent_set_len_blocks_state(&ext[pos], 1u, create_unwritten);
    extent_set_start_block(&ext[pos], nb);
    hdr->eh_entries = entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    *phys_out = nb;
    return EXT4_OK;
}

static ext4_status_t extent_promote_inline_to_index(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_t *old_ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &old_hdr, &old_ext);
    if (st != EXT4_OK) return st;
    u16 entries = le16(old_hdr->eh_entries);
    u64 leaf_block = 0;
    st = alloc_block(mnt, &leaf_block);
    if (st != EXT4_OK) return st;

    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) { (void)free_block(mnt, leaf_block); return EXT4_ERR_NO_MEMORY; }
    memset(leaf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *leaf_hdr = (ext4_extent_header_t *)leaf;
    leaf_hdr->eh_magic = EXT4_EXT_MAGIC;
    leaf_hdr->eh_entries = entries;
    leaf_hdr->eh_max = extent_leaf_capacity(mnt);
    leaf_hdr->eh_depth = 0;
    leaf_hdr->eh_generation = 0;
    if (entries > le16(leaf_hdr->eh_max)) {
        ext4_tmp_block_free(leaf);
        (void)free_block(mnt, leaf_block);
        return EXT4_ERR_CORRUPT;
    }
    ext4_extent_t *leaf_ext = (ext4_extent_t *)(leaf_hdr + 1);
    for (u16 i = 0; i < entries; ++i) leaf_ext[i] = old_ext[i];
    st = write_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) {
        ext4_tmp_block_free(leaf);
        (void)free_block(mnt, leaf_block);
        return st;
    }
    u32 first_logical = entries ? le32(leaf_ext[0].ee_block) : 0u;
    ext4_tmp_block_free(leaf);

    memset(inode->i_block, 0, sizeof(inode->i_block));
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    root->eh_magic = EXT4_EXT_MAGIC;
    root->eh_entries = 1u;
    root->eh_max = extent_root_index_capacity();
    root->eh_depth = 1u;
    root->eh_generation = 0;
    ext4_extent_idx_t *idx = (ext4_extent_idx_t *)(root + 1);
    idx[0].ei_block = first_logical;
    idx[0].ei_leaf_lo = (u32)leaf_block;
    idx[0].ei_leaf_hi = (u16)(leaf_block >> 32);
    idx[0].ei_unused = 0;
    inode_add_blocks(mnt, inode, 1);
    return EXT4_OK;
}

static void extent_idx_set_leaf(ext4_extent_idx_t *idx, u32 first_logical, u64 leaf_block) {
    idx->ei_block = first_logical;
    idx->ei_leaf_lo = (u32)leaf_block;
    idx->ei_leaf_hi = (u16)(leaf_block >> 32);
    idx->ei_unused = 0;
}

static ext4_status_t extent_index_choose_leaf(ext4_extent_header_t *root, ext4_extent_idx_t *idx, u32 logical, bool allocate, u16 *chosen_out) {
    if (!root || !idx || !chosen_out) return EXT4_ERR_RANGE;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return allocate ? EXT4_ERR_UNSUPPORTED : EXT4_ERR_RANGE;
    bool found = false;
    u16 chosen = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) { chosen = i; found = true; }
        else break;
    }
    if (!found) {
        if (!allocate) return EXT4_ERR_RANGE;
        chosen = 0;
    }
    *chosen_out = chosen;
    return EXT4_OK;
}

static ext4_status_t extent_index_split_leaf(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *root, ext4_extent_idx_t *idx, u16 chosen, u8 *leaf) {
    if (!mnt || !root || !idx || !leaf) return EXT4_ERR_RANGE;
    u16 root_entries = le16(root->eh_entries);
    u16 root_max = le16(root->eh_max);
    if (chosen >= root_entries) return EXT4_ERR_RANGE;
    if (root_entries >= root_max) return EXT4_ERR_UNSUPPORTED;

    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_t *old_ext = 0;
    ext4_status_t st = extent_leaf_validate(mnt, leaf, &old_hdr, &old_ext);
    if (st != EXT4_OK) return st;
    u16 old_entries = le16(old_hdr->eh_entries);
    if (old_entries < 2u) return EXT4_ERR_CORRUPT;

    u16 split = (u16)(old_entries / 2u);
    if (split == 0 || split >= old_entries) return EXT4_ERR_CORRUPT;

    u64 new_leaf_block = 0;
    st = alloc_block(mnt, &new_leaf_block);
    if (st != EXT4_OK) return st;

    u8 *new_leaf = ext4_tmp_block(mnt);
    if (!new_leaf) { (void)free_block(mnt, new_leaf_block); return EXT4_ERR_NO_MEMORY; }
    memset(new_leaf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *new_hdr = (ext4_extent_header_t *)new_leaf;
    ext4_extent_t *new_ext = (ext4_extent_t *)(new_hdr + 1);
    new_hdr->eh_magic = EXT4_EXT_MAGIC;
    new_hdr->eh_entries = (u16)(old_entries - split);
    new_hdr->eh_max = extent_leaf_capacity(mnt);
    new_hdr->eh_depth = 0;
    new_hdr->eh_generation = 0;
    for (u16 i = 0; i < new_hdr->eh_entries; ++i) new_ext[i] = old_ext[split + i];
    memset(&old_ext[split], 0, (usize)(old_entries - split) * sizeof(ext4_extent_t));
    old_hdr->eh_entries = split;

    st = extent_leaf_validate(mnt, leaf, &old_hdr, &old_ext);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); (void)free_block(mnt, new_leaf_block); return st; }
    st = extent_leaf_validate(mnt, new_leaf, &new_hdr, &new_ext);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); (void)free_block(mnt, new_leaf_block); return st; }

    u64 old_leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
    st = write_block(mnt, old_leaf_block, leaf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); (void)free_block(mnt, new_leaf_block); return st; }
    st = write_block(mnt, new_leaf_block, new_leaf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_leaf); (void)free_block(mnt, new_leaf_block); return st; }

    for (u16 i = root_entries; i > chosen + 1u; --i) idx[i] = idx[i - 1u];
    extent_idx_set_leaf(&idx[chosen], le32(old_ext[0].ee_block), old_leaf_block);
    extent_idx_set_leaf(&idx[chosen + 1u], le32(new_ext[0].ee_block), new_leaf_block);
    root->eh_entries = root_entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    ext4_tmp_block_free(new_leaf);
    return EXT4_OK;
}

static ext4_status_t extent_promote_index_to_depth2(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return EXT4_ERR_CORRUPT;
    u64 internal_block = 0;
    st = alloc_block(mnt, &internal_block);
    if (st != EXT4_OK) return st;

    u8 *internal = ext4_tmp_block(mnt);
    if (!internal) { (void)free_block(mnt, internal_block); return EXT4_ERR_NO_MEMORY; }
    memset(internal, 0, (usize)mnt->block_size);
    ext4_extent_header_t *ih = (ext4_extent_header_t *)internal;
    ext4_extent_idx_t *ii = (ext4_extent_idx_t *)(ih + 1);
    ih->eh_magic = EXT4_EXT_MAGIC;
    ih->eh_entries = entries;
    ih->eh_max = extent_index_node_capacity(mnt);
    ih->eh_depth = 1u;
    ih->eh_generation = 0;
    if (entries > le16(ih->eh_max)) { ext4_tmp_block_free(internal); (void)free_block(mnt, internal_block); return EXT4_ERR_CORRUPT; }
    for (u16 i = 0; i < entries; ++i) ii[i] = idx[i];
    st = write_block(mnt, internal_block, internal);
    if (st != EXT4_OK) { ext4_tmp_block_free(internal); (void)free_block(mnt, internal_block); return st; }
    u32 first = le32(ii[0].ei_block);
    ext4_tmp_block_free(internal);

    memset(inode->i_block, 0, sizeof(inode->i_block));
    root = (ext4_extent_header_t *)inode->i_block;
    root->eh_magic = EXT4_EXT_MAGIC;
    root->eh_entries = 1u;
    root->eh_max = extent_root_index_capacity();
    root->eh_depth = 2u;
    root->eh_generation = 0;
    idx = (ext4_extent_idx_t *)(root + 1);
    extent_idx_set_leaf(&idx[0], first, internal_block);
    inode_add_blocks(mnt, inode, 1);
    return EXT4_OK;
}

static ext4_status_t extent_depth2_split_internal(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *root, ext4_extent_idx_t *root_idx, u16 chosen, u64 old_block, u8 *old_buf) {
    u16 root_entries = le16(root->eh_entries);
    if (chosen >= root_entries) return EXT4_ERR_RANGE;
    if (root_entries >= le16(root->eh_max)) return EXT4_ERR_UNSUPPORTED;
    ext4_extent_header_t *old_hdr = 0;
    ext4_extent_idx_t *old_idx = 0;
    ext4_status_t st = extent_index_node_validate(mnt, old_buf, 1u, &old_hdr, &old_idx);
    if (st != EXT4_OK) return st;
    u16 old_entries = le16(old_hdr->eh_entries);
    if (old_entries < 2u) return EXT4_ERR_CORRUPT;
    u16 split = (u16)(old_entries / 2u);
    u64 new_block = 0;
    st = alloc_block(mnt, &new_block);
    if (st != EXT4_OK) return st;

    u8 *new_buf = ext4_tmp_block(mnt);
    if (!new_buf) { (void)free_block(mnt, new_block); return EXT4_ERR_NO_MEMORY; }
    memset(new_buf, 0, (usize)mnt->block_size);
    ext4_extent_header_t *nh = (ext4_extent_header_t *)new_buf;
    ext4_extent_idx_t *ni = (ext4_extent_idx_t *)(nh + 1);
    nh->eh_magic = EXT4_EXT_MAGIC;
    nh->eh_entries = (u16)(old_entries - split);
    nh->eh_max = extent_index_node_capacity(mnt);
    nh->eh_depth = 1u;
    nh->eh_generation = 0;
    for (u16 i = 0; i < nh->eh_entries; ++i) ni[i] = old_idx[split + i];
    memset(&old_idx[split], 0, (usize)(old_entries - split) * sizeof(ext4_extent_idx_t));
    old_hdr->eh_entries = split;
    st = write_block(mnt, old_block, old_buf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_buf); (void)free_block(mnt, new_block); return st; }
    st = write_block(mnt, new_block, new_buf);
    if (st != EXT4_OK) { ext4_tmp_block_free(new_buf); (void)free_block(mnt, new_block); return st; }
    for (u16 i = root_entries; i > chosen + 1u; --i) root_idx[i] = root_idx[i - 1u];
    extent_idx_set_leaf(&root_idx[chosen], le32(old_idx[0].ei_block), old_block);
    extent_idx_set_leaf(&root_idx[chosen + 1u], le32(ni[0].ei_block), new_block);
    root->eh_entries = root_entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    ext4_tmp_block_free(new_buf);
    return EXT4_OK;
}

static ext4_status_t extent_depth2_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC || le16(root->eh_depth) != 2u) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *root_idx = (ext4_extent_idx_t *)(root + 1);
    u8 *internal = ext4_tmp_block(mnt);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!internal || !leaf) {
        ext4_tmp_block_free(leaf);
        ext4_tmp_block_free(internal);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_ERR_UNSUPPORTED;
    for (u8 pass = 0; pass < 6u; ++pass) {
        u16 internal_chosen = 0;
        ext4_status_t st = extent_index_choose_leaf(root, root_idx, logical, allocate, &internal_chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 internal_block = le64_from_lo_hi(root_idx[internal_chosen].ei_leaf_lo, root_idx[internal_chosen].ei_leaf_hi);
        st = read_block(mnt, internal_block, internal);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *ih = 0;
        ext4_extent_idx_t *ii = 0;
        st = extent_index_node_validate(mnt, internal, 1u, &ih, &ii);
        if (st != EXT4_OK) { result = st; goto out; }
        u16 leaf_chosen = 0;
        st = extent_index_choose_leaf(ih, ii, logical, allocate, &leaf_chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 leaf_block = le64_from_lo_hi(ii[leaf_chosen].ei_leaf_lo, ii[leaf_chosen].ei_leaf_hi);
        st = read_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *lh = 0;
        ext4_extent_t *le = 0;
        st = extent_leaf_validate(mnt, leaf, &lh, &le);
        if (st != EXT4_OK) { result = st; goto out; }
        st = extent_array_block_for_write(mnt, inode, lh, le, logical, phys_out, allocate, zero_new, create_unwritten);
        if (st == EXT4_OK) {
            if (le16(lh->eh_entries)) ii[leaf_chosen].ei_block = le32(le[0].ee_block);
            if (le16(ih->eh_entries)) root_idx[internal_chosen].ei_block = le32(ii[0].ei_block);
            st = write_block(mnt, leaf_block, leaf);
            if (st != EXT4_OK) { result = st; goto out; }
            result = write_block(mnt, internal_block, internal);
            goto out;
        }
        if (st != EXT4_ERR_UNSUPPORTED || !allocate) { result = st; goto out; }
        if (le16(lh->eh_entries) >= le16(lh->eh_max)) {
            st = extent_index_split_leaf(mnt, inode, ih, ii, leaf_chosen, leaf);
            if (st == EXT4_OK) {
                if (le16(ih->eh_entries)) root_idx[internal_chosen].ei_block = le32(ii[0].ei_block);
                st = write_block(mnt, internal_block, internal);
                if (st != EXT4_OK) { result = st; goto out; }
                continue;
            }
            if (st != EXT4_ERR_UNSUPPORTED) { result = st; goto out; }
            st = extent_depth2_split_internal(mnt, inode, root, root_idx, internal_chosen, internal_block, internal);
            if (st != EXT4_OK) { result = st; goto out; }
            continue;
        }
        result = st;
        goto out;
    }
out:
    ext4_tmp_block_free(leaf);
    ext4_tmp_block_free(internal);
    return result;
}

static ext4_status_t extent_index_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;

    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_UNSUPPORTED;
    for (u8 attempt = 0; attempt < 3u; ++attempt) {
        u16 chosen = 0;
        st = extent_index_choose_leaf(root, idx, logical, allocate, &chosen);
        if (st != EXT4_OK) { result = st; goto out; }
        u64 leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
        st = read_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
        ext4_extent_header_t *leaf_hdr = 0;
        ext4_extent_t *leaf_ext = 0;
        st = extent_leaf_validate(mnt, leaf, &leaf_hdr, &leaf_ext);
        if (st != EXT4_OK) { result = st; goto out; }
        st = extent_array_block_for_write(mnt, inode, leaf_hdr, leaf_ext, logical, phys_out, allocate, zero_new, create_unwritten);
        if (st == EXT4_OK) {
            if (le16(leaf_hdr->eh_entries) > 0) idx[chosen].ei_block = le32(leaf_ext[0].ee_block);
            result = write_block(mnt, leaf_block, leaf);
            goto out;
        }
        if (st != EXT4_ERR_UNSUPPORTED || !allocate) { result = st; goto out; }
        if (le16(leaf_hdr->eh_entries) < le16(leaf_hdr->eh_max)) { result = st; goto out; }
        st = extent_index_split_leaf(mnt, inode, root, idx, chosen, leaf);
        if (st == EXT4_ERR_UNSUPPORTED) {
            st = extent_promote_index_to_depth2(mnt, inode);
            if (st != EXT4_OK) { result = st; goto out; }
            ext4_tmp_block_free(leaf);
            return extent_depth2_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        }
        if (st != EXT4_OK) { result = st; goto out; }
    }
out:
    ext4_tmp_block_free(leaf);
    return result;
}

static ext4_status_t extent_inline_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
    st = extent_array_block_for_write(mnt, inode, hdr, ext, logical, phys_out, allocate, zero_new, create_unwritten);
    if (st != EXT4_ERR_UNSUPPORTED || !allocate) return st;
    st = extent_promote_inline_to_index(mnt, inode);
    if (st != EXT4_OK) return st;
    return extent_index_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
}

static ext4_status_t extent_array_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, ext4_extent_header_t *hdr, ext4_extent_t *ext, u32 logical, bool *changed_out) {
    if (changed_out) *changed_out = false;
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = first + len;
        if (logical < first || logical >= end) continue;
        if (logical != first && logical != end - 1u && entries >= le16(hdr->eh_max)) return EXT4_ERR_UNSUPPORTED;
        bool unwritten = extent_is_unwritten(&ext[i]);
        u64 phys = extent_start_block(&ext[i]) + (u64)(logical - first);
        ext4_status_t st = free_block(mnt, phys);
        if (st != EXT4_OK) return st;
        if (len == 1u) {
            for (u16 j = i; j + 1u < entries; ++j) ext[j] = ext[j + 1u];
            memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
            hdr->eh_entries = entries - 1u;
        } else if (logical == first) {
            ext[i].ee_block = first + 1u;
            extent_set_start_block(&ext[i], extent_start_block(&ext[i]) + 1u);
            extent_set_len_blocks_state(&ext[i], len - 1u, unwritten);
        } else if (logical == end - 1u) {
            extent_set_len_blocks_state(&ext[i], len - 1u, unwritten);
        } else {
            u32 right_len = end - logical - 1u;
            u64 right_start = phys + 1u;
            extent_set_len_blocks_state(&ext[i], logical - first, unwritten);
            for (u16 j = entries; j > i + 1u; --j) ext[j] = ext[j - 1u];
            memset(&ext[i + 1u], 0, sizeof(ext4_extent_t));
            ext[i + 1u].ee_block = logical + 1u;
            extent_set_len_blocks_state(&ext[i + 1u], right_len, unwritten);
            extent_set_start_block(&ext[i + 1u], right_start);
            hdr->eh_entries = entries + 1u;
        }
        inode_add_blocks(mnt, inode, -1);
        if (changed_out) *changed_out = true;
        return EXT4_OK;
    }
    return EXT4_OK;
}

static ext4_status_t extent_inline_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
    return extent_array_free_logical_block(mnt, inode, hdr, ext, logical, 0);
}

static ext4_status_t extent_collect_inline_candidate(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 depth, ext4_extent_t *tmp, u16 cap, u16 *count, u32 *prev_end) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    u16 entries = le16(hdr->eh_entries);
    u16 max_entries = le16(hdr->eh_max);
    usize actual_cap = (node_bytes - sizeof(*hdr)) / (depth == 0 ? sizeof(ext4_extent_t) : sizeof(ext4_extent_idx_t));
    if (entries > max_entries || max_entries > actual_cap) return EXT4_ERR_CORRUPT;
    if (depth == 0) {
        const ext4_extent_t *ext = (const ext4_extent_t *)(hdr + 1);
        for (u16 i = 0; i < entries; ++i) {
            u32 first = le32(ext[i].ee_block);
            u32 len = extent_len_blocks(&ext[i]);
            u32 end = 0;
            if (len == 0 || __builtin_add_overflow(first, len, &end) || first < *prev_end) return EXT4_ERR_CORRUPT;
            if (*count >= cap) return EXT4_ERR_UNSUPPORTED;
            tmp[(*count)++] = ext[i];
            *prev_end = end;
        }
        return EXT4_OK;
    }
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < entries; ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = extent_collect_inline_candidate(mnt, block, (usize)mnt->block_size, depth - 1u, tmp, cap, count, prev_end);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

static ext4_status_t extent_free_metadata_tree(ext4_mount_t *mnt, ext4_inode_disk_t *inode, const void *node, usize node_bytes, u16 depth) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (depth == 0) return EXT4_OK;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    for (u16 i = 0; i < le16(hdr->eh_entries); ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        st = extent_free_metadata_tree(mnt, inode, block, (usize)mnt->block_size, depth - 1u);
        ext4_tmp_block_free(block);
        if (st != EXT4_OK) return st;
        st = free_block(mnt, child);
        if (st != EXT4_OK) return st;
        inode_add_blocks(mnt, inode, -1);
    }
    return EXT4_OK;
}

static ext4_status_t extent_try_demote_index_to_inline(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    if (!mnt || !inode) return EXT4_ERR_RANGE;
    const ext4_extent_header_t *root = (const ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
    u16 depth = le16(root->eh_depth);
    if (depth == 0) return EXT4_OK;
    if (depth > EXT4_MAX_EXTENT_DEPTH) return EXT4_ERR_UNSUPPORTED;
    u16 inline_cap = extent_inline_capacity();
    ext4_extent_t tmp[EXT4_N_BLOCKS];
    memset(tmp, 0, sizeof(tmp));
    u16 total = 0;
    u32 prev_end = 0;
    ext4_status_t st = extent_collect_inline_candidate(mnt, root, EXT4_N_BLOCKS * sizeof(u32), depth, tmp, inline_cap, &total, &prev_end);
    if (st == EXT4_ERR_UNSUPPORTED) return EXT4_OK;
    if (st != EXT4_OK) return st;
    st = extent_free_metadata_tree(mnt, inode, root, EXT4_N_BLOCKS * sizeof(u32), depth);
    if (st != EXT4_OK) return st;
    memset(inode->i_block, 0, sizeof(inode->i_block));
    ext4_extent_header_t *hdr = (ext4_extent_header_t *)inode->i_block;
    hdr->eh_magic = EXT4_EXT_MAGIC;
    hdr->eh_entries = total;
    hdr->eh_max = inline_cap;
    hdr->eh_depth = 0;
    hdr->eh_generation = 0;
    ext4_extent_t *ext = (ext4_extent_t *)(hdr + 1);
    for (u16 i = 0; i < total; ++i) ext[i] = tmp[i];
    return EXT4_OK;
}

static ext4_status_t extent_index_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    ext4_extent_header_t *root = 0;
    ext4_extent_idx_t *idx = 0;
    ext4_status_t st = extent_index_validate(mnt, inode, &root, &idx);
    if (st != EXT4_OK) return st;
    u16 entries = le16(root->eh_entries);
    if (entries == 0) return EXT4_OK;
    bool found = false;
    u16 chosen = 0;
    for (u16 i = 0; i < entries; ++i) {
        if (logical >= le32(idx[i].ei_block)) { chosen = i; found = true; }
        else break;
    }
    if (!found) return EXT4_OK;
    u64 leaf_block = le64_from_lo_hi(idx[chosen].ei_leaf_lo, idx[chosen].ei_leaf_hi);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!leaf) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    st = read_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *leaf_hdr = 0;
    ext4_extent_t *leaf_ext = 0;
    st = extent_leaf_validate(mnt, leaf, &leaf_hdr, &leaf_ext);
    if (st != EXT4_OK) { result = st; goto out; }
    bool changed = false;
    st = extent_array_free_logical_block(mnt, inode, leaf_hdr, leaf_ext, logical, &changed);
    if (st != EXT4_OK || !changed) { result = st; goto out; }
    if (le16(leaf_hdr->eh_entries) == 0) {
        st = free_block(mnt, leaf_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = chosen; i + 1u < entries; ++i) idx[i] = idx[i + 1u];
        memset(&idx[entries - 1u], 0, sizeof(ext4_extent_idx_t));
        root->eh_entries = entries - 1u;
        result = extent_try_demote_index_to_inline(mnt, inode);
        goto out;
    }
    idx[chosen].ei_block = le32(leaf_ext[0].ee_block);
    st = write_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    result = extent_try_demote_index_to_inline(mnt, inode);
out:
    ext4_tmp_block_free(leaf);
    return result;
}

static ext4_status_t extent_depth2_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    ext4_extent_header_t *root = (ext4_extent_header_t *)inode->i_block;
    if (le16(root->eh_magic) != EXT4_EXT_MAGIC || le16(root->eh_depth) != 2u) return EXT4_ERR_CORRUPT;
    ext4_extent_idx_t *root_idx = (ext4_extent_idx_t *)(root + 1);
    u16 root_entries = le16(root->eh_entries);
    if (root_entries == 0) return EXT4_OK;
    bool found_root = false;
    u16 ri = 0;
    for (u16 i = 0; i < root_entries; ++i) {
        if (logical >= le32(root_idx[i].ei_block)) { ri = i; found_root = true; }
        else break;
    }
    if (!found_root) return EXT4_OK;
    u8 *internal = ext4_tmp_block(mnt);
    u8 *leaf = ext4_tmp_block(mnt);
    if (!internal || !leaf) {
        ext4_tmp_block_free(leaf);
        ext4_tmp_block_free(internal);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_OK;
    u64 internal_block = le64_from_lo_hi(root_idx[ri].ei_leaf_lo, root_idx[ri].ei_leaf_hi);
    ext4_status_t st = read_block(mnt, internal_block, internal);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *ih = 0;
    ext4_extent_idx_t *ii = 0;
    st = extent_index_node_validate(mnt, internal, 1u, &ih, &ii);
    if (st != EXT4_OK) { result = st; goto out; }
    u16 internal_entries = le16(ih->eh_entries);
    bool found_leaf = false;
    u16 li = 0;
    for (u16 i = 0; i < internal_entries; ++i) {
        if (logical >= le32(ii[i].ei_block)) { li = i; found_leaf = true; }
        else break;
    }
    if (!found_leaf) goto out;
    u64 leaf_block = le64_from_lo_hi(ii[li].ei_leaf_lo, ii[li].ei_leaf_hi);
    st = read_block(mnt, leaf_block, leaf);
    if (st != EXT4_OK) { result = st; goto out; }
    ext4_extent_header_t *lh = 0;
    ext4_extent_t *le = 0;
    st = extent_leaf_validate(mnt, leaf, &lh, &le);
    if (st != EXT4_OK) { result = st; goto out; }
    bool changed = false;
    st = extent_array_free_logical_block(mnt, inode, lh, le, logical, &changed);
    if (st != EXT4_OK || !changed) { result = st; goto out; }
    if (le16(lh->eh_entries) == 0) {
        st = free_block(mnt, leaf_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = li; i + 1u < internal_entries; ++i) ii[i] = ii[i + 1u];
        memset(&ii[internal_entries - 1u], 0, sizeof(ext4_extent_idx_t));
        ih->eh_entries = internal_entries - 1u;
    } else {
        ii[li].ei_block = le32(le[0].ee_block);
        st = write_block(mnt, leaf_block, leaf);
        if (st != EXT4_OK) { result = st; goto out; }
    }
    if (le16(ih->eh_entries) == 0) {
        st = free_block(mnt, internal_block);
        if (st != EXT4_OK) { result = st; goto out; }
        inode_add_blocks(mnt, inode, -1);
        for (u16 i = ri; i + 1u < root_entries; ++i) root_idx[i] = root_idx[i + 1u];
        memset(&root_idx[root_entries - 1u], 0, sizeof(ext4_extent_idx_t));
        root->eh_entries = root_entries - 1u;
    } else {
        root_idx[ri].ei_block = le32(ii[0].ei_block);
        st = write_block(mnt, internal_block, internal);
        if (st != EXT4_OK) { result = st; goto out; }
    }
    result = extent_try_demote_index_to_inline(mnt, inode);
out:
    ext4_tmp_block_free(leaf);
    ext4_tmp_block_free(internal);
    return result;
}

static ext4_status_t inode_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate, bool zero_new, bool create_unwritten) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        u16 depth = le16(hdr->eh_depth);
        if (depth == 0) return extent_inline_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        if (depth == 1) return extent_index_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        if (depth == 2) return extent_depth2_block_for_write(mnt, inode, logical, phys_out, allocate, zero_new, create_unwritten);
        return EXT4_ERR_UNSUPPORTED;
    }
    if (logical < 12u) {
        u32 b = le32(inode->i_block[logical]);
        if (!b && allocate) {
            u64 nb = 0;
            ext4_status_t st = alloc_data_block(mnt, mnt->allocator_next_block, zero_new, &nb);
            if (st != EXT4_OK) return st;
            inode->i_block[logical] = (u32)nb;
            inode_add_blocks(mnt, inode, 1);
            b = (u32)nb;
        }
        if (!b) return EXT4_ERR_RANGE;
        *phys_out = b;
        return EXT4_OK;
    }
    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical >= 12u + per_block) return EXT4_ERR_UNSUPPORTED;
    u32 indirect = le32(inode->i_block[12]);
    if (!indirect && allocate) {
        u64 nb = 0;
        ext4_status_t st = alloc_block(mnt, &nb);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = (u32)nb;
        inode_add_blocks(mnt, inode, 1);
        indirect = (u32)nb;
    }
    if (!indirect) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b && allocate) {
        u64 nb = 0;
        st = alloc_data_block(mnt, mnt->allocator_next_block, zero_new, &nb);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        put32(&entries[idx], (u32)nb);
        st = write_block(mnt, indirect, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        inode_add_blocks(mnt, inode, 1);
        b = (u32)nb;
    }
    ext4_tmp_block_free(block);
    if (!b) return EXT4_ERR_RANGE;
    *phys_out = b;
    return EXT4_OK;
}


static ext4_status_t inode_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        u16 depth = le16(hdr->eh_depth);
        if (depth == 0) return extent_inline_free_logical_block(mnt, inode, logical);
        if (depth == 1) return extent_index_free_logical_block(mnt, inode, logical);
        if (depth == 2) return extent_depth2_free_logical_block(mnt, inode, logical);
        return EXT4_ERR_UNSUPPORTED;
    }
    if (logical < 12u) {
        u32 b = le32(inode->i_block[logical]);
        if (!b) return EXT4_OK;
        ext4_status_t st = free_block(mnt, b);
        if (st != EXT4_OK) return st;
        inode->i_block[logical] = 0;
        inode_add_blocks(mnt, inode, -1);
        return EXT4_OK;
    }
    u32 per_block = (u32)(mnt->block_size / 4u);
    if (logical >= 12u + per_block) return EXT4_ERR_UNSUPPORTED;
    u32 indirect = le32(inode->i_block[12]);
    if (!indirect) return EXT4_OK;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b) { ext4_tmp_block_free(block); return EXT4_OK; }
    st = free_block(mnt, b);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    put32(&entries[idx], 0);
    bool any = false;
    for (u32 i = 0; i < per_block; ++i) {
        if (le32(entries[i]) != 0) { any = true; break; }
    }
    if (any) {
        st = write_block(mnt, indirect, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    } else {
        st = free_block(mnt, indirect);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        inode->i_block[12] = 0;
        inode_add_blocks(mnt, inode, -1);
    }
    ext4_tmp_block_free(block);
    inode_add_blocks(mnt, inode, -1);
    return EXT4_OK;
}

static ext4_status_t inode_zero_tail(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u64 size) {
    if (size == 0 || (size % mnt->block_size) == 0) return EXT4_OK;
    u64 logical64 = size / mnt->block_size;
    if (logical64 > 0xffffffffull) return EXT4_ERR_RANGE;
    u32 logical = (u32)logical64;
    u32 off = (u32)(size % mnt->block_size);
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, inode, logical, &phys, false, false, false);
    if (st != EXT4_OK) return st == EXT4_ERR_RANGE ? EXT4_OK : st;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    st = read_data_block(mnt, phys, block);
    if (st == EXT4_OK) {
        memset(block + off, 0, (usize)mnt->block_size - off);
        st = write_data_block(mnt, phys, block);
    }
    ext4_tmp_block_free(block);
    return st;
}

ext4_status_t ext4_write_file(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 offset, const void *buffer, usize bytes, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    if (!ext4_inode_is_regular(inode) && !ext4_inode_is_symlink(inode)) return EXT4_ERR_UNSUPPORTED;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    if (bytes == 0) return EXT4_OK;
    u64 old_size = ext4_inode_size(inode);
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    if (offset > old_size) {
        ext4_status_t zs = inode_zero_tail(mnt, inode, old_size);
        if (zs != EXT4_OK) return zs;
    }
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    const u8 *in = (const u8 *)buffer;
    usize done = 0;
    ext4_status_t st = EXT4_OK;
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) { st = EXT4_ERR_RANGE; goto out; }
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) { st = EXT4_ERR_RANGE; goto out; }
        u32 logical = (u32)logical64;
        u32 block_off = (u32)(abs % mnt->block_size);
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        bool zero_new = (block_off != 0 || take != (usize)mnt->block_size);
        u64 phys = 0;
        st = inode_block_for_write(mnt, inode, logical, &phys, true, zero_new, false);
        if (st != EXT4_OK) goto out;
        if (block_off != 0 || take != (usize)mnt->block_size) {
            if (logical64 >= old_blocks64) {
                memset(block, 0, (usize)mnt->block_size);
            } else {
                st = read_data_block(mnt, phys, block);
                if (st != EXT4_OK) goto out;
            }
        } else memset(block, 0, (usize)mnt->block_size);
        memcpy(block + block_off, in + done, take);
        st = write_data_block(mnt, phys, block);
        if (st != EXT4_OK) goto out;
        done += take;
    }
    u64 end = 0;
    if (!checked_add_u64(offset, (u64)done, &end)) { st = EXT4_ERR_RANGE; goto out; }
    if (end > ext4_inode_size(inode)) inode_set_size(inode, end);
    st = write_inode(mnt, ino, inode);
    if (st == EXT4_OK && written_out) *written_out = done;
out:
    ext4_tmp_block_free(block);
    return st;
}



static ext4_status_t ext4_preallocate_file_loaded(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 size) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_regular(inode)) return EXT4_ERR_UNSUPPORTED;
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) == 0) return EXT4_ERR_UNSUPPORTED;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u64 old_size = ext4_inode_size(inode);
    if (size <= old_size) return EXT4_OK;
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    u64 new_blocks64 = div_round_up_u64(size, mnt->block_size);
    if (new_blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    ext4_status_t st = EXT4_OK;
    if (old_size && (old_size % mnt->block_size) != 0) {
        st = inode_zero_tail(mnt, inode, old_size);
        if (st != EXT4_OK) return st;
    }
    for (u64 l = old_blocks64; l < new_blocks64; ++l) {
        u64 phys = 0;
        st = inode_block_for_write(mnt, inode, (u32)l, &phys, true, false, true);
        if (st != EXT4_OK) return st;
    }
    inode_set_size(inode, size);
    return write_inode(mnt, ino, inode);
}

ext4_status_t ext4_preallocate_file_inode(ext4_mount_t *mnt, u32 ino, u64 size) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    return ext4_preallocate_file_loaded(mnt, ino, &inode, size);
}

ext4_status_t ext4_preallocate_file_path(ext4_mount_t *mnt, const char *path, u64 size) {
    if (!mnt || !path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, path, &inode, &ino);
    if (st != EXT4_OK) return st;
    return ext4_preallocate_file_loaded(mnt, ino, &inode, size);
}

static ext4_status_t ext4_truncate_file_loaded(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 new_size) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    if (ext4_inode_is_dir(inode)) return EXT4_ERR_UNSUPPORTED;
    if (!ext4_inode_is_regular(inode)) return EXT4_ERR_UNSUPPORTED;
    u64 old_size = ext4_inode_size(inode);
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    u64 new_blocks64 = div_round_up_u64(new_size, mnt->block_size);
    if (old_blocks64 > 0xffffffffull || new_blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) == 0 && new_blocks64 > 12ull + (mnt->block_size / 4u)) return EXT4_ERR_UNSUPPORTED;
    ext4_status_t st = EXT4_OK;
    if (new_blocks64 > old_blocks64) {
        st = inode_zero_tail(mnt, inode, old_size);
        if (st != EXT4_OK) return st;
    } else if (new_blocks64 < old_blocks64) {
        for (u64 l = old_blocks64; l > new_blocks64; --l) {
            st = inode_free_logical_block(mnt, inode, (u32)(l - 1u));
            if (st != EXT4_OK) return st;
        }
        if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
            const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)inode->i_block;
            if (le16(hdr->eh_magic) == EXT4_EXT_MAGIC && le16(hdr->eh_depth) == 1u) {
                st = extent_try_demote_index_to_inline(mnt, inode);
                if (st != EXT4_OK) return st;
            }
        }
    }
    st = inode_zero_tail(mnt, inode, new_size);
    if (st != EXT4_OK) return st;
    inode_set_size(inode, new_size);
    return write_inode(mnt, ino, inode);
}

ext4_status_t ext4_truncate_file_inode(ext4_mount_t *mnt, u32 ino, u64 new_size) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    return ext4_truncate_file_loaded(mnt, ino, &inode, new_size);
}

ext4_status_t ext4_truncate_file_path(ext4_mount_t *mnt, const char *path, u64 new_size) {
    if (!mnt || !path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, path, &inode, &ino);
    if (st != EXT4_OK) return st;
    return ext4_truncate_file_loaded(mnt, ino, &inode, new_size);
}

static bool split_parent_path(const char *path, char *parent, usize parent_cap, char *name, usize name_cap) {
    if (!path || !parent || !name || path[0] != '/') return false;
    usize len = strnlen(path, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX) return false;
    while (len > 1 && path[len - 1] == '/') --len;
    usize slash = len;
    while (slash > 0 && path[slash - 1] != '/') --slash;
    usize name_len = len - slash;
    if (name_len == 0 || name_len > EXT4_NAME_LEN || name_len >= name_cap) return false;
    memcpy(name, path + slash, name_len);
    name[name_len] = 0;
    if (slash <= 1) {
        if (parent_cap < 2) return false;
        parent[0] = '/'; parent[1] = 0;
    } else {
        usize plen = slash - 1;
        if (plen >= parent_cap) return false;
        memcpy(parent, path, plen);
        parent[plen] = 0;
    }
    return true;
}

static void make_dirent_at(u8 *block, usize off, u32 ino, u16 rec_len, u8 type, const char *name) {
    usize n = strlen(name);
    ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + off);
    de->inode = ino;
    de->rec_len = rec_len;
    de->name_len = (u8)n;
    de->file_type = type;
    memcpy(de->name, name, n);
}

static u32 htree_hash_name(const char *name, usize len) {
    u32 h = 2166136261u;
    for (usize i = 0; i < len; ++i) {
        h ^= (u8)name[i];
        h *= 16777619u;
    }
    return h ? h : 1u;
}

static u16 htree_capacity(ext4_mount_t *mnt) {
    return (u16)((mnt->block_size - sizeof(aurora_htree_header_t)) / sizeof(aurora_htree_entry_t));
}

static u32 htree_checksum(ext4_mount_t *mnt, aurora_htree_header_t *hdr, aurora_htree_entry_t *entries) {
    u32 saved = hdr->checksum;
    hdr->checksum = 0;
    u32 c = crc32(&mnt->sb.s_uuid, sizeof(mnt->sb.s_uuid));
    c = crc32_update(c, hdr, sizeof(*hdr));
    c = crc32_update(c, entries, (usize)le16(hdr->entries) * sizeof(*entries));
    hdr->checksum = saved;
    return c;
}

static ext4_status_t htree_load(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, u8 *block, aurora_htree_header_t **hdr_out, aurora_htree_entry_t **entries_out) {
    if (!mnt || !dir || !block || !hdr_out || !entries_out) return EXT4_ERR_RANGE;
    if ((le32(dir->i_flags) & EXT4_INDEX_FL) == 0 || le32(dir->i_file_acl_lo) == 0) return EXT4_ERR_NOT_FOUND;
    ext4_status_t st = read_block(mnt, le32(dir->i_file_acl_lo), block);
    if (st != EXT4_OK) return st;
    aurora_htree_header_t *hdr = (aurora_htree_header_t *)block;
    aurora_htree_entry_t *entries = (aurora_htree_entry_t *)(hdr + 1);
    if (le32(hdr->magic) != AURORA_EXT4_HTREE_MAGIC || le16(hdr->version) != AURORA_EXT4_HTREE_VERSION || le16(hdr->entry_size) != sizeof(aurora_htree_entry_t)) return EXT4_ERR_CORRUPT;
    if (le16(hdr->entries) > le16(hdr->max_entries) || le16(hdr->max_entries) > htree_capacity(mnt)) return EXT4_ERR_CORRUPT;
    if (hdr->checksum && hdr->checksum != htree_checksum(mnt, hdr, entries)) return EXT4_ERR_CORRUPT;
    *hdr_out = hdr;
    *entries_out = entries;
    return EXT4_OK;
}

static ext4_status_t htree_commit(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u8 *block) {
    aurora_htree_header_t *hdr = (aurora_htree_header_t *)block;
    aurora_htree_entry_t *entries = (aurora_htree_entry_t *)(hdr + 1);
    hdr->checksum = htree_checksum(mnt, hdr, entries);
    return write_block(mnt, le32(dir->i_file_acl_lo), block);
}

static bool htree_entry_name_matches(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, const aurora_htree_entry_t *he, const char *name, usize name_len) {
    if (he->name_len != name_len) return false;
    u64 phys = 0;
    if (inode_block_for_write(mnt, (ext4_inode_disk_t *)dir, le16(he->logical_block), &phys, false, false, false) != EXT4_OK) return false;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return false;
    bool ok = false;
    if (read_block(mnt, phys, block) != EXT4_OK) goto out;
    u16 off = le16(he->offset);
    if ((usize)off + 8u > mnt->block_size) goto out;
    const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + off);
    u16 rec_len = le16(de->rec_len);
    if (rec_len < 8u || (usize)off + rec_len > mnt->block_size) goto out;
    ok = de->inode == he->inode && de->name_len == name_len && 8u + name_len <= rec_len && memcmp(de->name, name, name_len) == 0;
out:
    ext4_tmp_block_free(block);
    return ok;
}

static __attribute__((noinline)) ext4_status_t htree_lookup(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    usize name_len = strlen(name);
    u32 h = htree_hash_name(name, name_len);
    u8 *index_block = ext4_tmp_block(mnt);
    if (!index_block) return EXT4_ERR_NO_MEMORY;
    aurora_htree_header_t *hdr = 0;
    aurora_htree_entry_t *entries = 0;
    ext4_status_t result = htree_load(mnt, dir, index_block, &hdr, &entries);
    if (result != EXT4_OK) goto out;
    result = EXT4_ERR_NOT_FOUND;
    for (u16 i = 0; i < le16(hdr->entries); ++i) {
        if (le32(entries[i].hash) != h) continue;
        if (!htree_entry_name_matches(mnt, dir, &entries[i], name, name_len)) continue;
        *ino_out = le32(entries[i].inode);
        if (type_out) *type_out = entries[i].file_type;
        result = EXT4_OK;
        goto out;
    }
out:
    ext4_tmp_block_free(index_block);
    return result;
}

static void htree_insert_sorted(aurora_htree_entry_t *entries, u16 *count, aurora_htree_entry_t e) {
    u16 pos = 0;
    while (pos < *count && (le32(entries[pos].hash) < e.hash || (le32(entries[pos].hash) == e.hash && le16(entries[pos].offset) < e.offset))) ++pos;
    for (u16 i = *count; i > pos; --i) entries[i] = entries[i - 1u];
    entries[pos] = e;
    ++*count;
}

static __attribute__((noinline)) ext4_status_t htree_append_record(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u32 dir_ino, const char *name, u32 child_ino, u8 type, u32 logical, usize offset) {
    u8 *index_block = ext4_tmp_block(mnt);
    if (!index_block) return EXT4_ERR_NO_MEMORY;
    aurora_htree_header_t *hdr = 0;
    aurora_htree_entry_t *entries = 0;
    ext4_status_t result = htree_load(mnt, dir, index_block, &hdr, &entries);
    if (result != EXT4_OK) goto out;
    u16 count = le16(hdr->entries);
    if (count >= le16(hdr->max_entries)) { result = EXT4_ERR_UNSUPPORTED; goto out; }
    aurora_htree_entry_t e;
    memset(&e, 0, sizeof(e));
    e.hash = htree_hash_name(name, strlen(name));
    e.inode = child_ino;
    e.logical_block = (u16)logical;
    e.offset = (u16)offset;
    e.name_len = (u8)strlen(name);
    e.file_type = type;
    htree_insert_sorted(entries, &count, e);
    hdr->entries = count;
    hdr->dir_ino = dir_ino;
    result = htree_commit(mnt, dir, index_block);
out:
    ext4_tmp_block_free(index_block);
    return result;
}

static __attribute__((noinline)) ext4_status_t htree_build(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir) {
    if ((le32(dir->i_flags) & EXT4_INDEX_FL) != 0) return EXT4_OK;
    u16 cap = htree_capacity(mnt);
    if (cap == 0) return EXT4_ERR_UNSUPPORTED;
    u64 idx_block = 0;
    ext4_status_t st = alloc_block(mnt, &idx_block);
    if (st != EXT4_OK) return st;
    u8 *index_block = ext4_tmp_block(mnt);
    u8 *block = ext4_tmp_block(mnt);
    if (!index_block || !block) {
        ext4_tmp_block_free(block);
        ext4_tmp_block_free(index_block);
        (void)free_block(mnt, idx_block);
        return EXT4_ERR_NO_MEMORY;
    }
    ext4_status_t result = EXT4_OK;
    memset(index_block, 0, (usize)mnt->block_size);
    aurora_htree_header_t *hdr = (aurora_htree_header_t *)index_block;
    aurora_htree_entry_t *entries = (aurora_htree_entry_t *)(hdr + 1);
    hdr->magic = AURORA_EXT4_HTREE_MAGIC;
    hdr->version = AURORA_EXT4_HTREE_VERSION;
    hdr->entry_size = sizeof(aurora_htree_entry_t);
    hdr->max_entries = cap;
    hdr->dir_ino = dir_ino;
    u16 count = 0;
    u64 size = ext4_inode_size(dir);
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        st = inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto fail; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto fail; }
            if (de->inode && de->name_len && !(de->name_len == 1u && de->name[0] == '.') && !(de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.')) {
                if (count >= cap) { result = EXT4_ERR_UNSUPPORTED; goto fail; }
                aurora_htree_entry_t e;
                memset(&e, 0, sizeof(e));
                e.hash = htree_hash_name(de->name, de->name_len);
                e.inode = le32(de->inode);
                e.logical_block = (u16)(off / mnt->block_size);
                e.offset = (u16)p;
                e.name_len = de->name_len;
                e.file_type = de->file_type;
                htree_insert_sorted(entries, &count, e);
            }
            p += rec_len;
        }
    }
    hdr->entries = count;
    hdr->checksum = htree_checksum(mnt, hdr, entries);
    st = write_block(mnt, idx_block, index_block);
    if (st != EXT4_OK) { result = st; goto fail; }
    dir->i_file_acl_lo = (u32)idx_block;
    dir->i_flags |= EXT4_INDEX_FL;
    inode_add_blocks(mnt, dir, 1);
    result = write_inode(mnt, dir_ino, dir);
    goto out;
fail:
    (void)free_block(mnt, idx_block);
out:
    ext4_tmp_block_free(block);
    ext4_tmp_block_free(index_block);
    return result;
}

static ext4_status_t htree_note_add(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 child_ino, u8 type, u32 logical, usize offset) {
    if (!mnt || !dir || !name || ext4_name_is_dot_or_dotdot(name)) return EXT4_OK;
    if ((le32(dir->i_flags) & EXT4_INDEX_FL) == 0) {
        u32 count = 0;
        u64 size = ext4_inode_size(dir);
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        for (u64 off = 0; off < size; off += mnt->block_size) {
            u64 phys = 0;
            if (inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false, false, false) != EXT4_OK) continue;
            if (read_block(mnt, phys, block) != EXT4_OK) continue;
            usize p = 0;
            while (p + 8u <= mnt->block_size) {
                const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
                u16 rec_len = le16(de->rec_len);
                if (rec_len < 8u || p + rec_len > mnt->block_size) break;
                if (de->inode && de->name_len && !(de->name_len == 1u && de->name[0] == '.') && !(de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.')) ++count;
                p += rec_len;
            }
        }
        ext4_tmp_block_free(block);
        if (count < AURORA_EXT4_HTREE_MIN_ENTRIES) return EXT4_OK;
        ext4_status_t st = htree_build(mnt, dir_ino, dir);
        if (st != EXT4_OK) return st == EXT4_ERR_UNSUPPORTED ? EXT4_OK : st;
        return EXT4_OK;
    }
    return htree_append_record(mnt, dir, dir_ino, name, child_ino, type, logical, offset);
}

static ext4_status_t htree_remove_name(ext4_mount_t *mnt, ext4_inode_disk_t *dir, const char *name, u32 logical, usize offset) {
    if (!mnt || !dir || !name || (le32(dir->i_flags) & EXT4_INDEX_FL) == 0) return EXT4_OK;
    u8 *index_block = ext4_tmp_block(mnt);
    if (!index_block) return EXT4_ERR_NO_MEMORY;
    aurora_htree_header_t *hdr = 0;
    aurora_htree_entry_t *entries = 0;
    ext4_status_t result = EXT4_OK;
    ext4_status_t st = htree_load(mnt, dir, index_block, &hdr, &entries);
    if (st != EXT4_OK) { result = st == EXT4_ERR_NOT_FOUND ? EXT4_OK : st; goto out; }
    u32 h = htree_hash_name(name, strlen(name));
    u16 count = le16(hdr->entries);
    for (u16 i = 0; i < count; ++i) {
        if (le32(entries[i].hash) == h && le16(entries[i].logical_block) == logical && le16(entries[i].offset) == offset) {
            for (u16 j = i; j + 1u < count; ++j) entries[j] = entries[j + 1u];
            memset(&entries[count - 1u], 0, sizeof(entries[0]));
            hdr->entries = count - 1u;
            result = htree_commit(mnt, dir, index_block);
            goto out;
        }
    }
out:
    ext4_tmp_block_free(index_block);
    return result;
}


static ext4_status_t htree_count_live_entries(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u32 *count_out) {
    if (!mnt || !dir || !count_out) return EXT4_ERR_RANGE;
    *count_out = 0;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 size = ext4_inode_size(dir);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len &&
                !(de->name_len == 1u && de->name[0] == '.') &&
                !(de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.')) ++*count_out;
            p += rec_len;
        }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t htree_drop_index(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir) {
    if (!mnt || !dir) return EXT4_ERR_RANGE;
    u32 idx = le32(dir->i_file_acl_lo);
    if (idx) {
        bool allocated = false;
        if (block_bitmap_is_allocated(mnt, idx, &allocated) == EXT4_OK && allocated) {
            ext4_status_t st = free_block(mnt, idx);
            if (st != EXT4_OK) return st;
            if (le32(dir->i_blocks_lo) >= (u32)(mnt->block_size / 512u)) inode_add_blocks(mnt, dir, -1);
        } else {
            write_cache_discard_block(mnt, idx);
            data_cache_discard_block(mnt, idx);
        }
    }
    dir->i_file_acl_lo = 0;
    dir->i_flags &= ~EXT4_INDEX_FL;
    return write_inode(mnt, dir_ino, dir);
}

static ext4_status_t htree_repair_dir(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, ext4_fsck_report_t *report) {
    if (!mnt || !dir || !ext4_inode_is_dir(dir)) return EXT4_OK;
    u32 live = 0;
    ext4_status_t st = htree_count_live_entries(mnt, dir, &live);
    if (st != EXT4_OK) return st;
    bool had_index = (le32(dir->i_flags) & EXT4_INDEX_FL) != 0;
    bool rebuild = had_index || live >= AURORA_EXT4_HTREE_MIN_ENTRIES;
    if (!rebuild) return EXT4_OK;
    if (had_index) {
        st = htree_drop_index(mnt, dir_ino, dir);
        if (st != EXT4_OK) return st;
    }
    if (live >= AURORA_EXT4_HTREE_MIN_ENTRIES) {
        st = htree_build(mnt, dir_ino, dir);
        if (st == EXT4_ERR_UNSUPPORTED) st = EXT4_OK;
        if (st != EXT4_OK) return st;
    }
    if (report) ++report->repaired_htree;
    ++g_ext4_perf_stats.repair_htree_rebuilds;
    return EXT4_OK;
}


static bool dirent_live_entry_is_repairable(ext4_mount_t *mnt, const ext4_dir_entry_2_t *de, u16 rec_len) {
    if (!mnt || !de || de->inode == 0) return false;
    if (de->name_len == 0) return false;
    if (le32(de->inode) == 0 || le32(de->inode) > mnt->inodes_count) return false;
    if (de->file_type > 7u) return false;
    u16 need = ext4_rec_len(de->name_len);
    return need >= 8u && need <= rec_len;
}

static void dirent_copy_normalized(u8 *dst, u32 ino, u16 rec_len, u8 type, const char *name, u8 name_len) {
    ext4_dir_entry_2_t *out = (ext4_dir_entry_2_t *)dst;
    memset(out, 0, rec_len);
    out->inode = ino;
    out->rec_len = rec_len;
    out->name_len = name_len;
    out->file_type = type;
    if (name_len) memcpy(out->name, name, name_len);
}

static void dirent_make_free(u8 *dst, u16 rec_len) {
    ext4_dir_entry_2_t *out = (ext4_dir_entry_2_t *)dst;
    memset(out, 0, rec_len);
    out->rec_len = rec_len;
}

static ext4_status_t dirent_repair_dir(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, ext4_fsck_report_t *report) {
    if (!mnt || !dir || !ext4_inode_is_dir(dir)) return EXT4_OK;
    if (mnt->block_size == 0 || mnt->block_size > MAX_BLOCK_SIZE || mnt->block_size > 0xffffu) return EXT4_ERR_RANGE;
    u64 size = ext4_inode_size(dir);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    u8 *fixed = (u8 *)kmalloc((usize)mnt->block_size);
    if (!fixed) { ext4_tmp_block_free(block); return EXT4_ERR_NO_MEMORY; }
    bool changed_any = false;
    ext4_status_t final_st = EXT4_OK;
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { final_st = st; break; }

        memset(fixed, 0, (usize)mnt->block_size);
        usize in = 0;
        usize out = 0;
        bool parse_ok = true;
        bool changed = false;
        u32 live_entries = 0;

        while (in + 8u <= mnt->block_size) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + in);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || (rec_len & 3u) != 0 || in + rec_len > mnt->block_size) {
                parse_ok = false;
                changed = true;
                break;
            }

            if (dirent_live_entry_is_repairable(mnt, de, rec_len)) {
                u16 need = ext4_rec_len(de->name_len);
                if (out + need > mnt->block_size) {
                    parse_ok = false;
                    changed = true;
                    break;
                }
                dirent_copy_normalized(fixed + out, le32(de->inode), need, de->file_type, de->name, de->name_len);
                out += need;
                ++live_entries;
            } else {
                if (de->inode != 0 || de->name_len != 0 || de->file_type != 0) changed = true;
            }
            in += rec_len;
        }
        if (in != mnt->block_size) changed = true;

        if (out == 0) {
            dirent_make_free(fixed, (u16)mnt->block_size);
        } else {
            ext4_dir_entry_2_t *last = 0;
            usize scan = 0;
            for (u32 i = 0; i < live_entries; ++i) {
                last = (ext4_dir_entry_2_t *)(fixed + scan);
                scan += le16(last->rec_len);
            }
            if (!last || scan != out) {
                final_st = EXT4_ERR_CORRUPT;
                break;
            }
            last->rec_len = (u16)(le16(last->rec_len) + (u16)(mnt->block_size - out));
        }

        if (!parse_ok || memcmp(block, fixed, (usize)mnt->block_size) != 0) changed = true;
        if (changed) {
            st = write_block(mnt, phys, fixed);
            if (st != EXT4_OK) { final_st = st; break; }
            changed_any = true;
        }
    }
    kfree(fixed);
    ext4_tmp_block_free(block);
    if (final_st != EXT4_OK) return final_st;
    if (changed_any) {
        if ((le32(dir->i_flags) & EXT4_INDEX_FL) != 0) {
            ext4_status_t st = htree_repair_dir(mnt, dir_ino, dir, report);
            if (st != EXT4_OK) return st;
        } else {
            ext4_status_t st = write_inode(mnt, dir_ino, dir);
            if (st != EXT4_OK) return st;
        }
        if (report) ++report->repaired_dirents;
        ++g_ext4_perf_stats.repair_dirent_fixes;
    }
    return EXT4_OK;
}

ext4_status_t ext4_repair_metadata(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    if (report) memset(report, 0, sizeof(*report));
    ++g_ext4_perf_stats.repair_runs;
    ext4_status_t st = ext4_flush_and_invalidate_read_caches(mnt);
    if (st != EXT4_OK) return st;

    u8 *bm = ext4_tmp_block(mnt);
    if (!bm) return EXT4_ERR_NO_MEMORY;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) goto out_bm;
        st = read_block(mnt, group_inode_bitmap(&gd), bm);
        if (st != EXT4_OK) goto out_bm;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) continue;
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino == 0 || ino > mnt->inodes_count) continue;
            ext4_inode_disk_t inode;
            st = ext4_read_inode(mnt, ino, &inode);
            if (st != EXT4_OK) goto out_bm;
            if (inode.i_mode == 0 || !ext4_inode_is_dir(&inode)) continue;
            st = dirent_repair_dir(mnt, ino, &inode, report);
            if (st != EXT4_OK) goto out_bm;
            st = htree_repair_dir(mnt, ino, &inode, report);
            if (st != EXT4_OK) goto out_bm;
        }
    }
    ext4_tmp_block_free(bm);
    bm = 0;

    u64 old_free_blocks = sb_free_blocks(mnt);
    u32 old_free_inodes = sb_free_inodes(mnt);
    st = ext4_recount_free_counters(mnt);
    if (st != EXT4_OK) return st;
    if (old_free_blocks != sb_free_blocks(mnt) || old_free_inodes != sb_free_inodes(mnt)) {
        if (report) ++report->repaired_counters;
        ++g_ext4_perf_stats.repair_counter_fixes;
    }

    bool checksum_fixed = false;
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
        u16 want = group_desc_checksum(mnt, group, &gd);
        if (gd.bg_checksum != want) checksum_fixed = true;
        st = write_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
    }
    u32 old_super_sum = mnt->sb.s_checksum;
    st = write_super(mnt);
    if (st != EXT4_OK) return st;
    if (checksum_fixed || old_super_sum != mnt->sb.s_checksum) {
        if (report) ++report->repaired_checksums;
        ++g_ext4_perf_stats.repair_checksum_fixes;
    }
    st = ext4_sync_metadata(mnt);
    if (st != EXT4_OK) return st;
    if (!report) return EXT4_OK;
    u32 repaired_counters = report->repaired_counters;
    u32 repaired_checksums = report->repaired_checksums;
    u32 repaired_htree = report->repaired_htree;
    u32 repaired_dirents = report->repaired_dirents;
    ext4_fsck_report_t verify;
    st = ext4_validate_metadata(mnt, &verify);
    *report = verify;
    report->repaired_counters = repaired_counters;
    report->repaired_checksums = repaired_checksums;
    report->repaired_htree = repaired_htree;
    report->repaired_dirents = repaired_dirents;
    return st;
out_bm:
    ext4_tmp_block_free(bm);
    return st;
}

static ext4_status_t dir_add_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 child_ino, u8 type) {
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    usize name_len = strlen(name);
    if (name_len == 0 || name_len > EXT4_NAME_LEN) return EXT4_ERR_RANGE;
    u16 need = ext4_rec_len(name_len);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    u64 size = ext4_inode_size(dir);
    u32 blocks = (u32)div_round_up_u64(size ? size : mnt->block_size, mnt->block_size);
    for (u32 logical = 0; logical < blocks; ++logical) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode == 0 && rec_len >= need) {
                make_dirent_at(block, p, child_ino, rec_len, type, name);
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_note_add(mnt, dir_ino, dir, name, child_ino, type, logical, p);
                if (st != EXT4_OK) { result = st; goto out; }
                result = write_inode(mnt, dir_ino, dir);
                goto out;
            }
            u16 used = ext4_rec_len(de->name_len);
            if (de->inode != 0 && rec_len >= used + need) {
                de->rec_len = used;
                make_dirent_at(block, p + used, child_ino, rec_len - used, type, name);
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_note_add(mnt, dir_ino, dir, name, child_ino, type, logical, p + used);
                if (st != EXT4_OK) { result = st; goto out; }
                result = write_inode(mnt, dir_ino, dir);
                goto out;
            }
            p += rec_len;
        }
    }
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, blocks, &phys, true, false, false);
    if (st != EXT4_OK) { result = st; goto out; }
    memset(block, 0, (usize)mnt->block_size);
    make_dirent_at(block, 0, child_ino, (u16)mnt->block_size, type, name);
    st = write_block(mnt, phys, block);
    if (st != EXT4_OK) { result = st; goto out; }
    inode_set_size(dir, size + mnt->block_size);
    st = htree_note_add(mnt, dir_ino, dir, name, child_ino, type, blocks, 0);
    if (st != EXT4_OK) { result = st; goto out; }
    result = write_inode(mnt, dir_ino, dir);
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t dir_remove_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_NOT_FOUND;
    usize name_len = strlen(name);
    u64 size = ext4_inode_size(dir);
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false, false, false);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        ext4_dir_entry_2_t *prev = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, de->name_len) == 0) {
                st = htree_remove_name(mnt, dir, name, (u32)(off / mnt->block_size), p);
                if (st != EXT4_OK) { result = st; goto out; }
                if (ino_out) *ino_out = le32(de->inode);
                if (type_out) *type_out = de->file_type;
                if (prev) {
                    u16 prev_len = le16(prev->rec_len);
                    prev->rec_len = (u16)(prev_len + rec_len);
                } else {
                    dirent_make_free((u8 *)de, rec_len);
                }
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                result = write_inode(mnt, dir_ino, dir);
                goto out;
            }
            prev = de;
            p += rec_len;
        }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static bool dir_is_empty_cb(const ext4_dirent_t *entry, void *ctx) {
    bool *empty = (bool *)ctx;
    if (strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0) {
        *empty = false;
        return false;
    }
    return true;
}

static ext4_status_t free_extent_array_blocks(ext4_mount_t *mnt, const ext4_extent_header_t *hdr, const ext4_extent_t *ext) {
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u64 start = extent_start_block(&ext[i]);
        u32 len = extent_len_blocks(&ext[i]);
        for (u32 j = 0; j < len; ++j) {
            ext4_status_t st = free_block(mnt, start + j);
            if (st != EXT4_OK) return st;
        }
    }
    return EXT4_OK;
}

static ext4_status_t free_extent_tree_blocks(ext4_mount_t *mnt, const void *node, usize node_bytes, u16 depth) {
    const ext4_extent_header_t *hdr = (const ext4_extent_header_t *)node;
    if (node_bytes < sizeof(*hdr) || le16(hdr->eh_magic) != EXT4_EXT_MAGIC || le16(hdr->eh_depth) != depth) return EXT4_ERR_CORRUPT;
    if (depth == 0) return free_extent_array_blocks(mnt, hdr, (const ext4_extent_t *)(hdr + 1));
    const ext4_extent_idx_t *idx = (const ext4_extent_idx_t *)(hdr + 1);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u16 i = 0; i < le16(hdr->eh_entries); ++i) {
        u64 child = le64_from_lo_hi(idx[i].ei_leaf_lo, idx[i].ei_leaf_hi);
        ext4_status_t st = read_block(mnt, child, block);
        if (st != EXT4_OK) { result = st; goto out; }
        st = free_extent_tree_blocks(mnt, block, (usize)mnt->block_size, depth - 1u);
        if (st != EXT4_OK) { result = st; goto out; }
        st = free_block(mnt, child);
        if (st != EXT4_OK) { result = st; goto out; }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t free_inode_blocks(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    if (ext4_inode_is_symlink(inode) && ext4_inode_size(inode) <= sizeof(inode->i_block) && le32(inode->i_blocks_lo) == 0) {
        memset(inode->i_block, 0, sizeof(inode->i_block));
        inode_set_size(inode, 0);
        inode->i_flags = 0;
        return EXT4_OK;
    }
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        const ext4_extent_header_t *root_hdr = (const ext4_extent_header_t *)inode->i_block;
        if (le16(root_hdr->eh_magic) != EXT4_EXT_MAGIC) return EXT4_ERR_CORRUPT;
        ext4_status_t st = free_extent_tree_blocks(mnt, root_hdr, EXT4_N_BLOCKS * sizeof(u32), le16(root_hdr->eh_depth));
        if (st != EXT4_OK) return st;
        memset(inode->i_block, 0, sizeof(inode->i_block));
        inode_set_size(inode, 0);
        inode->i_blocks_lo = 0;
        if (inode->i_file_acl_lo) { st = free_block(mnt, le32(inode->i_file_acl_lo)); if (st != EXT4_OK) return st; inode->i_file_acl_lo = 0; }
        inode->i_flags &= ~EXT4_INDEX_FL;
        return EXT4_OK;
    }
    for (u32 i = 0; i < 12u; ++i) {
        u32 b = le32(inode->i_block[i]);
        if (b) { ext4_status_t st = free_block(mnt, b); if (st != EXT4_OK) return st; inode->i_block[i] = 0; }
    }
    u32 indirect = le32(inode->i_block[12]);
    if (indirect) {
        u8 *block = ext4_tmp_block(mnt);
        if (!block) return EXT4_ERR_NO_MEMORY;
        ext4_status_t st = read_block(mnt, indirect, block);
        if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
        u32 *entries = (u32 *)block;
        u32 per = (u32)(mnt->block_size / 4u);
        for (u32 i = 0; i < per; ++i) {
            u32 b = le32(entries[i]);
            if (b) { st = free_block(mnt, b); if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; } }
        }
        ext4_tmp_block_free(block);
        st = free_block(mnt, indirect);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = 0;
    }
    if (inode->i_file_acl_lo) { ext4_status_t st = free_block(mnt, le32(inode->i_file_acl_lo)); if (st != EXT4_OK) return st; inode->i_file_acl_lo = 0; }
    inode->i_flags &= ~EXT4_INDEX_FL;
    inode_set_size(inode, 0);
    inode->i_blocks_lo = 0;
    return EXT4_OK;
}

static ext4_status_t orphan_push(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    inode->i_dtime = mnt->sb.s_last_orphan;
    ext4_status_t st = write_inode(mnt, ino, inode);
    if (st != EXT4_OK) return st;
    mnt->sb.s_last_orphan = ino;
    return write_super(mnt);
}

static ext4_status_t orphan_remove(ext4_mount_t *mnt, u32 ino) {
    if (!mnt || ino == 0) return EXT4_ERR_RANGE;
    u32 cur = le32(mnt->sb.s_last_orphan);
    u32 prev = 0;
    while (cur) {
        ext4_inode_disk_t inode;
        ext4_status_t st = ext4_read_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        u32 next = le32(inode.i_dtime);
        if (cur == ino) {
            if (prev == 0) {
                mnt->sb.s_last_orphan = next;
                st = write_super(mnt);
                if (st != EXT4_OK) return st;
            } else {
                ext4_inode_disk_t pinode;
                st = ext4_read_inode(mnt, prev, &pinode);
                if (st != EXT4_OK) return st;
                pinode.i_dtime = next;
                st = write_inode(mnt, prev, &pinode);
                if (st != EXT4_OK) return st;
            }
            inode.i_dtime = 0;
            return write_inode(mnt, cur, &inode);
        }
        prev = cur;
        cur = next;
    }
    return EXT4_OK;
}

static ext4_status_t ext4_recover_orphans(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt) return EXT4_ERR_RANGE;
    u32 cur = le32(mnt->sb.s_last_orphan);
    u32 guard = 0;
    while (cur) {
        if (++guard > mnt->inodes_count) return EXT4_ERR_CORRUPT;
        ext4_inode_disk_t inode;
        ext4_status_t st = ext4_read_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        u32 next = le32(inode.i_dtime);
        bool is_dir = ext4_inode_is_dir(&inode);
        st = free_inode_blocks(mnt, &inode);
        if (st != EXT4_OK) return st;
        memset(&inode, 0, sizeof(inode));
        st = write_inode(mnt, cur, &inode);
        if (st != EXT4_OK) return st;
        st = free_inode(mnt, cur, is_dir);
        if (st != EXT4_OK && st != EXT4_ERR_CORRUPT) return st;
        ++mnt->recovered_orphans;
        if (report) ++report->orphan_recovered;
        cur = next;
    }
    if (mnt->sb.s_last_orphan) {
        mnt->sb.s_last_orphan = 0;
        return write_super(mnt);
    }
    return EXT4_OK;
}

static ext4_status_t create_node(ext4_mount_t *mnt, const char *path, bool dir, const void *data, usize size) {
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&parent)) return EXT4_ERR_NOT_DIR;
    u32 tmp_ino = 0;
    if (ext4_find_in_dir(mnt, &parent, name, &tmp_ino, 0) == EXT4_OK) return EXT4_ERR_EXIST;
    u32 ino = 0;
    st = alloc_inode(mnt, dir, &ino);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = (u16)((dir ? EXT4_IFDIR : EXT4_IFREG) | (dir ? 0755 : 0644));
    inode.i_links_count = dir ? 2u : 1u;
    ext4_init_inline_extent_tree(&inode);
    bool linked = false;
    u16 parent_links_before = le16(parent.i_links_count);
    if (dir) {
        u64 b = 0;
        st = inode_block_for_write(mnt, &inode, 0, &b, true, false, false);
        if (st != EXT4_OK) goto fail_inode;
        inode_set_size(&inode, mnt->block_size);
        u8 *blk = (u8 *)kmalloc((usize)mnt->block_size);
        if (!blk) { st = EXT4_ERR_NO_MEMORY; goto fail_blocks; }
        memset(blk, 0, (usize)mnt->block_size);
        u16 dot = ext4_rec_len(1);
        make_dirent_at(blk, 0, ino, dot, 2, ".");
        make_dirent_at(blk, dot, parent_ino, (u16)(mnt->block_size - dot), 2, "..");
        st = write_block(mnt, b, blk);
        kfree(blk);
        if (st != EXT4_OK) goto fail_blocks;
    }
    st = write_inode(mnt, ino, &inode);
    if (st != EXT4_OK) goto fail_blocks;
    st = dir_add_entry(mnt, parent_ino, &parent, name, ino, dir ? 2u : 1u);
    if (st != EXT4_OK) goto fail_blocks;
    linked = true;
    if (dir) {
        parent.i_links_count = (u16)(parent_links_before + 1u);
        st = write_inode(mnt, parent_ino, &parent);
        if (st != EXT4_OK) {
            parent.i_links_count = parent_links_before;
            goto fail_linked;
        }
    }
    if (!dir && size) {
        st = ext4_write_file(mnt, ino, &inode, 0, data, size, 0);
        if (st != EXT4_OK) goto fail_linked;
    }
    return EXT4_OK;
fail_linked:
    if (linked) (void)dir_remove_entry(mnt, parent_ino, &parent, name, 0, 0);
fail_blocks:
    (void)free_inode_blocks(mnt, &inode);
fail_inode:
    (void)free_inode(mnt, ino, dir);
    return st;
}

ext4_status_t ext4_create_file(ext4_mount_t *mnt, const char *path, const void *data, usize size) {
    if (size && !data) return EXT4_ERR_RANGE;
    return create_node(mnt, path, false, data, size);
}

ext4_status_t ext4_mkdir(ext4_mount_t *mnt, const char *path) {
    return create_node(mnt, path, true, 0, 0);
}


ext4_status_t ext4_symlink(ext4_mount_t *mnt, const char *target, const char *link_path) {
    if (!mnt || !target || !target[0] || !link_path) return EXT4_ERR_RANGE;
    usize len = strnlen(target, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX) return EXT4_ERR_RANGE;
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(link_path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&parent)) return EXT4_ERR_NOT_DIR;
    u32 tmp_ino = 0;
    st = ext4_find_in_dir(mnt, &parent, name, &tmp_ino, 0);
    if (st == EXT4_OK) return EXT4_ERR_EXIST;
    if (st != EXT4_ERR_NOT_FOUND) return st;

    u32 ino = 0;
    st = alloc_inode(mnt, false, &ino);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = (u16)(EXT4_IFLNK | 0777u);
    inode.i_links_count = 1u;
    inode_set_size(&inode, len);
    bool linked = false;
    if (len <= sizeof(inode.i_block)) {
        memcpy(inode.i_block, target, len);
        inode.i_blocks_lo = 0;
        inode.i_flags = 0;
        st = write_inode(mnt, ino, &inode);
        if (st != EXT4_OK) goto fail_inode;
    } else {
        ext4_init_inline_extent_tree(&inode);
        st = write_inode(mnt, ino, &inode);
        if (st != EXT4_OK) goto fail_blocks;
        st = ext4_write_file(mnt, ino, &inode, 0, target, len, 0);
        if (st != EXT4_OK) goto fail_blocks;
    }
    st = dir_add_entry(mnt, parent_ino, &parent, name, ino, 7u);
    if (st != EXT4_OK) goto fail_blocks;
    linked = true;
    (void)linked;
    return EXT4_OK;
fail_blocks:
    if (linked) (void)dir_remove_entry(mnt, parent_ino, &parent, name, 0, 0);
    (void)free_inode_blocks(mnt, &inode);
fail_inode:
    (void)free_inode(mnt, ino, false);
    return st;
}

ext4_status_t ext4_link(ext4_mount_t *mnt, const char *old_path, const char *new_path) {
    if (!mnt || !old_path || !new_path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 src_ino = 0;
    ext4_status_t st = ext4_lstat_path(mnt, old_path, &inode, &src_ino);
    if (st != EXT4_OK) return st;
    if (ext4_inode_is_dir(&inode)) return EXT4_ERR_UNSUPPORTED;
    if (!ext4_inode_is_regular(&inode) && !ext4_inode_is_symlink(&inode)) return EXT4_ERR_UNSUPPORTED;
    u16 links = le16(inode.i_links_count);
    if (links == 0 || links == 0xffffu) return EXT4_ERR_RANGE;

    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(new_path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&parent)) return EXT4_ERR_NOT_DIR;
    u32 tmp_ino = 0;
    st = ext4_find_in_dir(mnt, &parent, name, &tmp_ino, 0);
    if (st == EXT4_OK) return EXT4_ERR_EXIST;
    if (st != EXT4_ERR_NOT_FOUND) return st;

    u8 file_type = ext4_inode_is_symlink(&inode) ? 7u : 1u;
    st = dir_add_entry(mnt, parent_ino, &parent, name, src_ino, file_type);
    if (st != EXT4_OK) return st;
    inode.i_links_count = (u16)(links + 1u);
    st = write_inode(mnt, src_ino, &inode);
    if (st != EXT4_OK) {
        (void)dir_remove_entry(mnt, parent_ino, &parent, name, 0, 0);
        return st;
    }
    return EXT4_OK;
}

ext4_status_t ext4_unlink(ext4_mount_t *mnt, const char *path) {
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    u32 ino = 0;
    u8 type = 0;
    st = ext4_find_in_dir(mnt, &parent, name, &ino, &type);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode;
    st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    bool is_dir = ext4_inode_is_dir(&inode);
    if (is_dir) {
        bool empty = true;
        st = ext4_list_dir(mnt, &inode, dir_is_empty_cb, &empty);
        if (st != EXT4_OK) return st;
        if (!empty) return EXT4_ERR_NOT_EMPTY;
    }
    st = dir_remove_entry(mnt, parent_ino, &parent, name, 0, 0);
    if (st != EXT4_OK) return st;
    if (is_dir && le16(parent.i_links_count) > 0) {
        parent.i_links_count = (u16)(le16(parent.i_links_count) - 1u);
        st = write_inode(mnt, parent_ino, &parent);
        if (st != EXT4_OK) return st;
    }
    st = drop_removed_link(mnt, ino, &inode, is_dir);
    if (st != EXT4_OK) return st;
    return EXT4_OK;
}



static ext4_status_t free_removed_inode(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, bool is_dir) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    ext4_status_t st = orphan_push(mnt, ino, inode);
    if (st != EXT4_OK) return st;
    st = free_inode_blocks(mnt, inode);
    if (st != EXT4_OK) return st;
    memset(inode, 0, sizeof(*inode));
    st = write_inode(mnt, ino, inode);
    if (st != EXT4_OK) return st;
    st = free_inode(mnt, ino, is_dir);
    if (st != EXT4_OK) return st;
    st = orphan_remove(mnt, ino);
    if (st != EXT4_OK) return st;
    return EXT4_OK;
}

static ext4_status_t drop_removed_link(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, bool is_dir) {
    if (!mnt || !inode || ino == 0) return EXT4_ERR_RANGE;
    if (!is_dir) {
        u16 links = le16(inode->i_links_count);
        if (links > 1u) {
            inode->i_links_count = (u16)(links - 1u);
            return write_inode(mnt, ino, inode);
        }
    }
    inode->i_links_count = 0;
    return free_removed_inode(mnt, ino, inode, is_dir);
}

static ext4_status_t dir_replace_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 new_ino, u8 new_type, u32 *old_ino_out, u8 *old_type_out) {
    if (!mnt || !dir || !name || ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    usize name_len = strlen(name);
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_ERR_NOT_FOUND;
    u64 size = ext4_inode_size(dir);
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u32 logical = (u32)(off / mnt->block_size);
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false, false, false);
        if (st != EXT4_OK) { result = st; goto out; }
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) { result = st; goto out; }
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8u || (rec_len & 3u) != 0 || p + rec_len > mnt->block_size) { result = EXT4_ERR_CORRUPT; goto out; }
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, name_len) == 0) {
                if (old_ino_out) *old_ino_out = le32(de->inode);
                if (old_type_out) *old_type_out = de->file_type;
                st = htree_remove_name(mnt, dir, name, logical, p);
                if (st != EXT4_OK) { result = st; goto out; }
                de->inode = new_ino;
                de->file_type = new_type;
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) { result = st; goto out; }
                st = htree_note_add(mnt, dir_ino, dir, name, new_ino, new_type, logical, p);
                if (st != EXT4_OK) { result = st; goto out; }
                result = write_inode(mnt, dir_ino, dir);
                goto out;
            }
            p += rec_len;
        }
    }
out:
    ext4_tmp_block_free(block);
    return result;
}

static ext4_status_t dir_update_dotdot(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u32 new_parent_ino) {
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, 0, &phys, false, false, false);
    if (st != EXT4_OK) return st;
    u8 *block = ext4_tmp_block(mnt);
    if (!block) return EXT4_ERR_NO_MEMORY;
    st = read_block(mnt, phys, block);
    if (st != EXT4_OK) { ext4_tmp_block_free(block); return st; }
    usize p = 0;
    while (p + 8u <= mnt->block_size) {
        ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
        u16 rec_len = le16(de->rec_len);
        if (rec_len < 8u || p + rec_len > mnt->block_size) { ext4_tmp_block_free(block); return EXT4_ERR_CORRUPT; }
        if (de->inode && de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.') {
            de->inode = new_parent_ino;
            st = write_block(mnt, phys, block);
            ext4_tmp_block_free(block);
            return st;
        }
        p += rec_len;
    }
    ext4_tmp_block_free(block);
    return EXT4_ERR_CORRUPT;
}

ext4_status_t ext4_rename(ext4_mount_t *mnt, const char *old_path, const char *new_path) {
    if (!mnt || !old_path || !new_path || strcmp(old_path, "/") == 0 || strcmp(new_path, "/") == 0) return EXT4_ERR_RANGE;
    char old_parent_path[VFS_PATH_MAX];
    char new_parent_path[VFS_PATH_MAX];
    char old_name[EXT4_NAME_LEN + 1];
    char new_name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(old_path, old_parent_path, sizeof(old_parent_path), old_name, sizeof(old_name))) return EXT4_ERR_RANGE;
    if (!split_parent_path(new_path, new_parent_path, sizeof(new_parent_path), new_name, sizeof(new_name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(old_name) || ext4_name_is_dot_or_dotdot(new_name)) return EXT4_ERR_RANGE;
    if (strcmp(old_parent_path, new_parent_path) == 0 && strcmp(old_name, new_name) == 0) return EXT4_OK;

    ext4_inode_disk_t old_parent, new_parent, inode;
    u32 old_parent_ino = 0, new_parent_ino = 0, src_ino = 0, target_ino = 0;
    u8 src_type = 0, target_type = 0;
    ext4_status_t st = ext4_lookup_path(mnt, old_parent_path, &old_parent, &old_parent_ino);
    if (st != EXT4_OK) return st;
    st = ext4_lookup_path(mnt, new_parent_path, &new_parent, &new_parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&old_parent) || !ext4_inode_is_dir(&new_parent)) return EXT4_ERR_NOT_DIR;
    st = ext4_find_in_dir(mnt, &old_parent, old_name, &src_ino, &src_type);
    if (st != EXT4_OK) return st;
    st = ext4_read_inode(mnt, src_ino, &inode);
    if (st != EXT4_OK) return st;
    bool is_dir = ext4_inode_is_dir(&inode);
    if (!is_dir && !ext4_inode_is_regular(&inode) && !ext4_inode_is_symlink(&inode)) return EXT4_ERR_UNSUPPORTED;
    if (is_dir) {
        usize old_len = strlen(old_path);
        if (strncmp(new_path, old_path, old_len) == 0 && (new_path[old_len] == '/' || new_path[old_len] == 0)) return EXT4_ERR_RANGE;
    }

    ext4_status_t target_st = ext4_find_in_dir(mnt, &new_parent, new_name, &target_ino, &target_type);
    if (target_st == EXT4_OK) {
        if (target_ino == src_ino) return EXT4_OK;
        ext4_inode_disk_t target_inode;
        st = ext4_read_inode(mnt, target_ino, &target_inode);
        if (st != EXT4_OK) return st;
        bool target_is_dir = ext4_inode_is_dir(&target_inode);
        if (target_is_dir != is_dir) return EXT4_ERR_EXIST;
        if (target_is_dir) {
            bool empty = true;
            st = ext4_list_dir(mnt, &target_inode, dir_is_empty_cb, &empty);
            if (st != EXT4_OK) return st;
            if (!empty) return EXT4_ERR_NOT_EMPTY;
        }
        u32 replaced_ino = 0;
        u8 replaced_type = 0;
        st = dir_replace_entry(mnt, new_parent_ino, &new_parent, new_name, src_ino, src_type, &replaced_ino, &replaced_type);
        if (st != EXT4_OK) return st;
        if (old_parent_ino == new_parent_ino) old_parent = new_parent;
        st = dir_remove_entry(mnt, old_parent_ino, &old_parent, old_name, 0, 0);
        if (st != EXT4_OK) {
            (void)dir_replace_entry(mnt, new_parent_ino, &new_parent, new_name, replaced_ino, replaced_type, 0, 0);
            return st;
        }
        if (is_dir && old_parent_ino != new_parent_ino) {
            if (le16(old_parent.i_links_count) > 0) old_parent.i_links_count = (u16)(le16(old_parent.i_links_count) - 1u);
            st = write_inode(mnt, old_parent_ino, &old_parent);
            if (st != EXT4_OK) return st;
            st = dir_update_dotdot(mnt, &inode, new_parent_ino);
            if (st != EXT4_OK) return st;
        }
        st = drop_removed_link(mnt, target_ino, &target_inode, target_is_dir);
        if (st != EXT4_OK) return st;
        return EXT4_OK;
    }
    if (target_st != EXT4_ERR_NOT_FOUND) return target_st;

    st = dir_add_entry(mnt, new_parent_ino, &new_parent, new_name, src_ino, src_type);
    if (st != EXT4_OK) return st;
    if (old_parent_ino == new_parent_ino) old_parent = new_parent;
    st = dir_remove_entry(mnt, old_parent_ino, &old_parent, old_name, 0, 0);
    if (st != EXT4_OK) {
        (void)dir_remove_entry(mnt, new_parent_ino, &new_parent, new_name, 0, 0);
        return st;
    }
    if (is_dir && old_parent_ino != new_parent_ino) {
        if (le16(old_parent.i_links_count) > 0) old_parent.i_links_count = (u16)(le16(old_parent.i_links_count) - 1u);
        new_parent.i_links_count = (u16)(le16(new_parent.i_links_count) + 1u);
        st = write_inode(mnt, old_parent_ino, &old_parent);
        if (st != EXT4_OK) return st;
        st = write_inode(mnt, new_parent_ino, &new_parent);
        if (st != EXT4_OK) return st;
        st = dir_update_dotdot(mnt, &inode, new_parent_ino);
        if (st != EXT4_OK) return st;
    }
    return EXT4_OK;
}

__attribute__((noinline)) ext4_status_t ext4_list_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_dir_iter_fn fn, void *ctx) {
    if (!mnt || !dir || !fn) return EXT4_ERR_IO;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 size = ext4_inode_size(dir);
    u8 *block = (u8 *)kmalloc((usize)mnt->block_size);
    if (!block) return EXT4_ERR_NO_MEMORY;
    ext4_status_t result = EXT4_OK;
    for (u64 off = 0; off < size; off += mnt->block_size) {
        usize got = 0;
        ext4_status_t st = ext4_read_file(mnt, dir, off, block, (usize)mnt->block_size, &got);
        if (st != EXT4_OK) { result = st; break; }
        usize p = 0;
        while (p + 8 <= got) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            usize next = 0;
            if (rec_len < 8 || !checked_add_usize(p, rec_len, &next) || next > got) { result = EXT4_ERR_CORRUPT; goto done; }
            if (de->inode != 0 && 8u + de->name_len <= rec_len) {
                ext4_dirent_t e;
                memset(&e, 0, sizeof(e));
                e.inode = le32(de->inode);
                e.file_type = de->file_type;
                memcpy(e.name, de->name, de->name_len);
                e.name[de->name_len] = 0;
                if (!fn(&e, ctx)) goto done;
            }
            p = next;
        }
    }
done:
    kfree(block);
    return result;
}

typedef struct ext4_find_ctx {
    const char *name;
    u32 ino;
    u8 type;
    bool found;
} ext4_find_ctx_t;

static bool find_dir_cb(const ext4_dirent_t *entry, void *ctxp) {
    ext4_find_ctx_t *ctx = (ext4_find_ctx_t *)ctxp;
    if (strcmp(entry->name, ctx->name) == 0) {
        ctx->ino = entry->inode;
        ctx->type = entry->file_type;
        ctx->found = true;
        return false;
    }
    return true;
}

ext4_status_t ext4_find_in_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    if (!mnt || !dir || !name || !ino_out) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    if ((le32(dir->i_flags) & EXT4_INDEX_FL) != 0) {
        ext4_status_t hs = htree_lookup(mnt, dir, name, ino_out, type_out);
        if (hs == EXT4_OK) return EXT4_OK;
        if (hs != EXT4_ERR_NOT_FOUND && hs != EXT4_ERR_CORRUPT) return hs;
    }
    ext4_find_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.name = name;
    ext4_status_t st = ext4_list_dir(mnt, dir, find_dir_cb, &ctx);
    if (st != EXT4_OK) return st;
    if (!ctx.found) return EXT4_ERR_NOT_FOUND;
    *ino_out = ctx.ino;
    if (type_out) *type_out = ctx.type;
    return EXT4_OK;
}

static bool ext4_str_append(char *dst, usize cap, const char *src) {
    if (!dst || !src || cap == 0) return false;
    usize dl = strlen(dst);
    usize sl = strlen(src);
    if (dl >= cap || sl >= cap - dl) return false;
    memcpy(dst + dl, src, sl + 1u);
    return true;
}

static ext4_status_t ext4_read_symlink_inode(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!mnt || !inode || (!buffer && size)) return EXT4_ERR_RANGE;
    if (!ext4_inode_is_symlink(inode)) return EXT4_ERR_UNSUPPORTED;
    u64 sz64 = ext4_inode_size(inode);
    if (sz64 > VFS_PATH_MAX - 1u) return EXT4_ERR_RANGE;
    usize len = (usize)sz64;
    usize take = len < size ? len : size;
    bool fast = len <= sizeof(inode->i_block) && le32(inode->i_blocks_lo) == 0 && !ext4_inode_uses_extents(inode);
    if (fast) {
        if (take) memcpy(buffer, inode->i_block, take);
    } else {
        ext4_status_t st = ext4_read_file(mnt, inode, 0, buffer, take, 0);
        if (st != EXT4_OK) return st;
    }
    if (read_out) *read_out = take;
    return EXT4_OK;
}

static bool ext4_compose_symlink_path(const char *base_dir, const char *target, const char *remaining, char *out, usize out_size) {
    if (!target || !target[0] || !out || out_size == 0) return false;
    char tmp[VFS_PATH_MAX];
    memset(tmp, 0, sizeof(tmp));
    if (target[0] == '/') {
        if (!ext4_str_append(tmp, sizeof(tmp), target)) return false;
    } else {
        const char *base = (base_dir && base_dir[0]) ? base_dir : "/";
        if (strcmp(base, "/") == 0) {
            if (!ext4_str_append(tmp, sizeof(tmp), "/")) return false;
        } else if (!ext4_str_append(tmp, sizeof(tmp), base) || !ext4_str_append(tmp, sizeof(tmp), "/")) {
            return false;
        }
        if (!ext4_str_append(tmp, sizeof(tmp), target)) return false;
    }
    if (remaining && remaining[0]) {
        usize len = strlen(tmp);
        if (len == 0 || tmp[len - 1u] != '/') {
            if (!ext4_str_append(tmp, sizeof(tmp), "/")) return false;
        }
        if (!ext4_str_append(tmp, sizeof(tmp), remaining)) return false;
    }
    return path_normalize(tmp, out, out_size);
}

static ext4_status_t ext4_lookup_path_ex(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out, bool follow_final, u32 depth) {
    if (!mnt || !path || !inode_out) return EXT4_ERR_RANGE;
    if (depth > 16u) return EXT4_ERR_RANGE;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return EXT4_ERR_RANGE;
    ext4_inode_disk_t cur;
    ext4_status_t st = ext4_read_inode(mnt, EXT4_ROOT_INO, &cur);
    if (st != EXT4_OK) return st;
    u32 cur_ino = EXT4_ROOT_INO;
    if (strcmp(norm, "/") == 0) {
        *inode_out = cur;
        if (ino_out) *ino_out = cur_ino;
        return EXT4_OK;
    }
    const char *cursor = norm;
    char comp[EXT4_NAME_LEN + 1];
    char cur_path[VFS_PATH_MAX];
    strcpy(cur_path, "/");
    while (path_next_component(&cursor, comp, sizeof(comp))) {
        if (strcmp(comp, ".") == 0) continue;
        u32 next_ino = 0;
        st = ext4_find_in_dir(mnt, &cur, comp, &next_ino, 0);
        if (st != EXT4_OK) return st;
        ext4_inode_disk_t next;
        st = ext4_read_inode(mnt, next_ino, &next);
        if (st != EXT4_OK) return st;
        bool has_more = *cursor != 0;
        if (ext4_inode_is_symlink(&next) && (follow_final || has_more)) {
            char target[VFS_PATH_MAX];
            usize got = 0;
            st = ext4_read_symlink_inode(mnt, &next, target, sizeof(target) - 1u, &got);
            if (st != EXT4_OK) return st;
            target[got] = 0;
            char combined[VFS_PATH_MAX];
            if (!ext4_compose_symlink_path(cur_path, target, cursor, combined, sizeof(combined))) return EXT4_ERR_RANGE;
            return ext4_lookup_path_ex(mnt, combined, inode_out, ino_out, follow_final, depth + 1u);
        }
        cur = next;
        cur_ino = next_ino;
        if (strcmp(cur_path, "/") != 0 && !ext4_str_append(cur_path, sizeof(cur_path), "/")) return EXT4_ERR_RANGE;
        if (!ext4_str_append(cur_path, sizeof(cur_path), comp)) return EXT4_ERR_RANGE;
    }
    *inode_out = cur;
    if (ino_out) *ino_out = cur_ino;
    return EXT4_OK;
}

ext4_status_t ext4_lookup_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out) {
    return ext4_lookup_path_ex(mnt, path, inode_out, ino_out, true, 0);
}

ext4_status_t ext4_lstat_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out) {
    return ext4_lookup_path_ex(mnt, path, inode_out, ino_out, false, 0);
}

ext4_status_t ext4_readlink(ext4_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    ext4_inode_disk_t inode;
    ext4_status_t st = ext4_lstat_path(mnt, path, &inode, 0);
    if (st != EXT4_OK) return st;
    return ext4_read_symlink_inode(mnt, &inode, buffer, size, read_out);
}

