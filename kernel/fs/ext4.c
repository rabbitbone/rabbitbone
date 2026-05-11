#include <aurora/ext4.h>
#include <aurora/vfs.h>
#include <aurora/libc.h>

#define EXT4_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define EXT4_FEATURE_INCOMPAT_EXTENTS  0x0040u
#define EXT4_FEATURE_INCOMPAT_64BIT    0x0080u
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE 0x0008u
#define EXT4_EXTENTS_FL EXT4_INODE_FLAG_EXTENTS
#define EXT4_IFDIR 0x4000u
#define EXT4_IFREG 0x8000u
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
static void extent_set_len_blocks(ext4_extent_t *ex, u32 len) {
    ex->ee_len = (u16)(len == 32768u ? 0x8000u : len);
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

static ext4_status_t read_bytes(block_device_t *dev, u64 partition_lba, u64 byte_offset, void *buffer, usize bytes) {
    if (!dev || !buffer) return EXT4_ERR_IO;
    u8 *out = (u8 *)buffer;
    u8 sector[512];
    while (bytes) {
        u64 lba = 0;
        if (!checked_add_u64(partition_lba, byte_offset / 512u, &lba)) return EXT4_ERR_RANGE;
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


static ext4_status_t write_bytes(block_device_t *dev, u64 partition_lba, u64 byte_offset, const void *buffer, usize bytes) {
    if (!dev || !buffer) return EXT4_ERR_IO;
    const u8 *in = (const u8 *)buffer;
    u8 sector[512];
    while (bytes) {
        u64 lba = 0;
        if (!checked_add_u64(partition_lba, byte_offset / 512u, &lba)) return EXT4_ERR_RANGE;
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

static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    return read_bytes(mnt->dev, mnt->partition_lba, byte_offset, buffer, (usize)mnt->block_size);
}


static ext4_status_t write_block(ext4_mount_t *mnt, u64 block, const void *buffer) {
    if (!mnt || !buffer) return EXT4_ERR_IO;
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    return write_bytes(mnt->dev, mnt->partition_lba, byte_offset, buffer, (usize)mnt->block_size);
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
    return read_bytes(mnt->dev, mnt->partition_lba, off, out, desc_size);
}


static ext4_status_t write_group_desc(ext4_mount_t *mnt, u32 group, const ext4_group_desc_disk_t *in) {
    if (!mnt || !in || group >= mnt->group_count) return EXT4_ERR_RANGE;
    usize desc_size = mnt->group_desc_size;
    if (desc_size < 32u || desc_size > sizeof(*in)) return EXT4_ERR_UNSUPPORTED;
    u64 table = 0, group_off = 0, off = 0;
    if (!checked_mul_u64(group_desc_table_block(mnt), mnt->block_size, &table)) return EXT4_ERR_RANGE;
    if (!checked_mul_u64((u64)group, mnt->group_desc_size, &group_off)) return EXT4_ERR_RANGE;
    if (!checked_add_u64(table, group_off, &off)) return EXT4_ERR_RANGE;
    return write_bytes(mnt->dev, mnt->partition_lba, off, in, desc_size);
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
static ext4_status_t write_super(ext4_mount_t *mnt) { return write_bytes(mnt->dev, mnt->partition_lba, 1024, &mnt->sb, sizeof(mnt->sb)); }

static u64 group_inode_table(const ext4_group_desc_disk_t *gd) {
    return le64_from_lo_hi(gd->bg_inode_table_lo, gd->bg_inode_table_hi);
}

u64 ext4_inode_size(const ext4_inode_disk_t *inode) {
    if (!inode) return 0;
    return ((u64)le32(inode->i_size_high) << 32) | le32(inode->i_size_lo);
}

bool ext4_inode_is_dir(const ext4_inode_disk_t *inode) { return inode && ((le16(inode->i_mode) & 0xf000u) == EXT4_IFDIR); }
bool ext4_inode_is_regular(const ext4_inode_disk_t *inode) { return inode && ((le16(inode->i_mode) & 0xf000u) == EXT4_IFREG); }

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

ext4_status_t ext4_validate_metadata(ext4_mount_t *mnt, ext4_fsck_report_t *report) {
    if (!mnt || !report) return EXT4_ERR_RANGE;
    memset(report, 0, sizeof(*report));
    report->sb_free_blocks = sb_free_blocks(mnt);
    report->sb_free_inodes = sb_free_inodes(mnt);
    u8 bm[MAX_BLOCK_SIZE];
    u32 inode_table_blocks = (u32)div_round_up_u64((u64)mnt->inodes_per_group * mnt->inode_size, mnt->block_size);
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
        ++report->checked_groups;
        u64 block_bm = group_block_bitmap(&gd);
        u64 inode_bm = group_inode_bitmap(&gd);
        u64 inode_table = group_inode_table(&gd);
        if (block_bm >= mnt->blocks_count || inode_bm >= mnt->blocks_count || inode_table >= mnt->blocks_count) {
            ++report->errors;
            return EXT4_ERR_CORRUPT;
        }
        if (inode_table_blocks && inode_table + inode_table_blocks > mnt->blocks_count) {
            ++report->errors;
            return EXT4_ERR_CORRUPT;
        }
        st = read_block(mnt, block_bm, bm);
        if (st != EXT4_OK) return st;
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
        if (st != EXT4_OK) return st;
        u32 free_inodes = 0;
        u32 valid_inodes = group_valid_inodes(mnt, group);
        for (u32 bit = 0; bit < valid_inodes; ++bit) {
            if (!bitmap_get(bm, bit)) ++free_inodes;
        }
        report->bitmap_free_inodes += free_inodes;
        if (free_inodes != gd_free_inodes(&gd)) ++report->errors;
    }
    if (report->bitmap_free_blocks != report->sb_free_blocks) ++report->errors;
    if (report->bitmap_free_inodes != report->sb_free_inodes) ++report->errors;
    return report->errors ? EXT4_ERR_CORRUPT : EXT4_OK;
}

ext4_status_t ext4_mount(block_device_t *dev, u64 partition_lba, ext4_mount_t *out) {
    if (!dev || !out) return EXT4_ERR_IO;
    memset(out, 0, sizeof(*out));
    ext4_superblock_disk_t sb;
    ext4_status_t st = read_bytes(dev, partition_lba, 1024, &sb, sizeof(sb));
    if (st != EXT4_OK) return st;
    if (le16(sb.s_magic) != EXT4_SUPER_MAGIC) return EXT4_ERR_BAD_MAGIC;
    u32 log_block = le32(sb.s_log_block_size);
    if (log_block > 2u) return EXT4_ERR_UNSUPPORTED;
    u64 block_size = 1024ull << log_block;
    if (block_size < 1024 || block_size > MAX_BLOCK_SIZE) return EXT4_ERR_UNSUPPORTED;
    u32 incompat = le32(sb.s_feature_incompat);
    u32 supported = EXT4_FEATURE_INCOMPAT_FILETYPE | EXT4_FEATURE_INCOMPAT_EXTENTS | EXT4_FEATURE_INCOMPAT_64BIT;
    if ((incompat & ~supported) != 0) return EXT4_ERR_UNSUPPORTED;
    out->dev = dev;
    out->partition_lba = partition_lba;
    out->block_size = block_size;
    out->blocks_count = le64_from_lo_hi(sb.s_blocks_count_lo, sb.s_blocks_count_hi);
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
    return EXT4_OK;
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
    return read_bytes(mnt->dev, mnt->partition_lba, off, out, bytes);
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
    u8 block[MAX_BLOCK_SIZE];
    u64 child = le64_from_lo_hi(best->ei_leaf_lo, best->ei_leaf_hi);
    ext4_status_t st = read_block(mnt, child, block);
    if (st != EXT4_OK) return st;
    return map_extent_tree(mnt, block, (usize)mnt->block_size, logical, phys_out, depth - 1);
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
        u8 block[MAX_BLOCK_SIZE];
        ext4_status_t st = read_block(mnt, indirect_block, block);
        if (st != EXT4_OK) return st;
        u32 *entries = (u32 *)block;
        u32 b = le32(entries[logical - 12]);
        if (!b) return EXT4_ERR_RANGE;
        *phys_out = b;
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

ext4_status_t ext4_read_file(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u64 offset, void *buffer, usize bytes, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    u64 size = ext4_inode_size(inode);
    if (offset >= size || bytes == 0) return EXT4_OK;
    if (bytes > size - offset) bytes = (usize)(size - offset);
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    u8 *out = (u8 *)buffer;
    usize done = 0;
    u8 block[MAX_BLOCK_SIZE];
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) return EXT4_ERR_RANGE;
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) return EXT4_ERR_RANGE;
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
        if (st != EXT4_OK) return st;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) return st;
        memcpy(out + done, block + block_off, take);
        done += take;
    }
    if (read_out) *read_out = done;
    return EXT4_OK;
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
    return write_bytes(mnt->dev, mnt->partition_lba, off, in, bytes);
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

static ext4_status_t alloc_block(ext4_mount_t *mnt, u64 *block_out) {
    if (!mnt || !block_out) return EXT4_ERR_RANGE;
    u8 bm[MAX_BLOCK_SIZE];
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
        if (gd_free_blocks(&gd) == 0) continue;
        u64 bitmap_block = group_block_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) return st;
        for (u32 bit = 0; bit < mnt->blocks_per_group; ++bit) {
            u64 abs = (u64)group * mnt->blocks_per_group + bit;
            if (abs < le32(mnt->sb.s_first_data_block) || abs >= mnt->blocks_count) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) return st;
            u32 gfree = gd_free_blocks(&gd);
            if (gfree) gd_set_free_blocks(&gd, gfree - 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) return st;
            u64 sfree = sb_free_blocks(mnt);
            if (sfree) sb_set_free_blocks(mnt, sfree - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) return st;
            u8 zero[MAX_BLOCK_SIZE];
            memset(zero, 0, (usize)mnt->block_size);
            st = write_block(mnt, abs, zero);
            if (st != EXT4_OK) return st;
            *block_out = abs;
            return EXT4_OK;
        }
    }
    return EXT4_ERR_NO_MEMORY;
}

static ext4_status_t free_block(ext4_mount_t *mnt, u64 block) {
    if (!mnt || block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u32 group = (u32)(block / mnt->blocks_per_group);
    u32 bit = (u32)(block % mnt->blocks_per_group);
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 bm[MAX_BLOCK_SIZE];
    st = read_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) return st;
    if (!bitmap_get(bm, bit)) return EXT4_ERR_CORRUPT;
    bitmap_clear8(bm, bit);
    st = write_block(mnt, group_block_bitmap(&gd), bm);
    if (st != EXT4_OK) return st;
    gd_set_free_blocks(&gd, gd_free_blocks(&gd) + 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    sb_set_free_blocks(mnt, sb_free_blocks(mnt) + 1u);
    return write_super(mnt);
}

static ext4_status_t alloc_inode(ext4_mount_t *mnt, bool dir, u32 *ino_out) {
    if (!mnt || !ino_out) return EXT4_ERR_RANGE;
    u32 first = le32(mnt->sb.s_first_ino);
    if (first == 0) first = 11;
    u8 bm[MAX_BLOCK_SIZE];
    for (u32 group = 0; group < mnt->group_count; ++group) {
        ext4_group_desc_disk_t gd;
        ext4_status_t st = read_group_desc(mnt, group, &gd);
        if (st != EXT4_OK) return st;
        if (gd_free_inodes(&gd) == 0) continue;
        u64 bitmap_block = group_inode_bitmap(&gd);
        st = read_block(mnt, bitmap_block, bm);
        if (st != EXT4_OK) return st;
        for (u32 bit = 0; bit < mnt->inodes_per_group; ++bit) {
            u32 ino = group * mnt->inodes_per_group + bit + 1u;
            if (ino < first || ino > mnt->inodes_count) continue;
            if (bitmap_get(bm, bit)) continue;
            bitmap_set8(bm, bit);
            st = write_block(mnt, bitmap_block, bm);
            if (st != EXT4_OK) return st;
            gd_set_free_inodes(&gd, gd_free_inodes(&gd) - 1u);
            if (dir) gd_set_used_dirs(&gd, gd_used_dirs(&gd) + 1u);
            st = write_group_desc(mnt, group, &gd);
            if (st != EXT4_OK) return st;
            sb_set_free_inodes(mnt, sb_free_inodes(mnt) - 1u);
            st = write_super(mnt);
            if (st != EXT4_OK) return st;
            *ino_out = ino;
            return EXT4_OK;
        }
    }
    return EXT4_ERR_NO_MEMORY;
}

static ext4_status_t free_inode(ext4_mount_t *mnt, u32 ino, bool dir) {
    if (!mnt || ino == 0 || ino > mnt->inodes_count) return EXT4_ERR_RANGE;
    u32 idx = ino - 1u;
    u32 group = idx / mnt->inodes_per_group;
    u32 bit = idx % mnt->inodes_per_group;
    ext4_group_desc_disk_t gd;
    ext4_status_t st = read_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    u8 bm[MAX_BLOCK_SIZE];
    st = read_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) return st;
    if (!bitmap_get(bm, bit)) return EXT4_ERR_CORRUPT;
    bitmap_clear8(bm, bit);
    st = write_block(mnt, group_inode_bitmap(&gd), bm);
    if (st != EXT4_OK) return st;
    gd_set_free_inodes(&gd, gd_free_inodes(&gd) + 1u);
    if (dir && gd_used_dirs(&gd)) gd_set_used_dirs(&gd, gd_used_dirs(&gd) - 1u);
    st = write_group_desc(mnt, group, &gd);
    if (st != EXT4_OK) return st;
    sb_set_free_inodes(mnt, sb_free_inodes(mnt) + 1u);
    return write_super(mnt);
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
        if (extent_is_unwritten(&ext[i])) return EXT4_ERR_UNSUPPORTED;
        if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
        if (i && first < prev_end) return EXT4_ERR_CORRUPT;
        if (extent_start_block(&ext[i]) >= ((u64)1 << 48)) return EXT4_ERR_RANGE;
        prev_end = end;
    }
    *hdr_out = hdr;
    *ext_out = ext;
    return EXT4_OK;
}

static ext4_status_t extent_inline_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
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
            return EXT4_OK;
        }
        if (logical < first) break;
    }
    if (!allocate) return EXT4_ERR_RANGE;
    u64 nb = 0;
    st = alloc_block(mnt, &nb);
    if (st != EXT4_OK) return st;

    bool merged_prev = false;
    if (pos > 0) {
        ext4_extent_t *prev = &ext[pos - 1u];
        u32 prev_first = le32(prev->ee_block);
        u32 prev_len = extent_len_blocks(prev);
        u64 prev_start = extent_start_block(prev);
        if (prev_len < 32768u && prev_first + prev_len == logical && prev_start + prev_len == nb) {
            extent_set_len_blocks(prev, prev_len + 1u);
            merged_prev = true;
        }
    }
    if (merged_prev) {
        if (pos < entries) {
            ext4_extent_t *prev = &ext[pos - 1u];
            ext4_extent_t *next = &ext[pos];
            u32 prev_first = le32(prev->ee_block);
            u32 prev_len = extent_len_blocks(prev);
            u32 next_first = le32(next->ee_block);
            u32 next_len = extent_len_blocks(next);
            u64 prev_start = extent_start_block(prev);
            u64 next_start = extent_start_block(next);
            if (prev_first + prev_len == next_first && prev_start + prev_len == next_start && prev_len + next_len <= 32768u) {
                extent_set_len_blocks(prev, prev_len + next_len);
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
        if (logical + 1u == next_first && nb + 1u == next_start && next_len < 32768u) {
            next->ee_block = logical;
            extent_set_start_block(next, nb);
            extent_set_len_blocks(next, next_len + 1u);
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
    extent_set_len_blocks(&ext[pos], 1u);
    extent_set_start_block(&ext[pos], nb);
    hdr->eh_entries = entries + 1u;
    inode_add_blocks(mnt, inode, 1);
    *phys_out = nb;
    return EXT4_OK;
}

static ext4_status_t extent_inline_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    ext4_extent_header_t *hdr = 0;
    ext4_extent_t *ext = 0;
    ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
    if (st != EXT4_OK) return st;
    u16 entries = le16(hdr->eh_entries);
    for (u16 i = 0; i < entries; ++i) {
        u32 first = le32(ext[i].ee_block);
        u32 len = extent_len_blocks(&ext[i]);
        u32 end = first + len;
        if (logical < first || logical >= end) continue;
        if (logical != first && logical != end - 1u && entries >= le16(hdr->eh_max)) return EXT4_ERR_UNSUPPORTED;
        u64 phys = extent_start_block(&ext[i]) + (u64)(logical - first);
        st = free_block(mnt, phys);
        if (st != EXT4_OK) return st;
        if (len == 1u) {
            for (u16 j = i; j + 1u < entries; ++j) ext[j] = ext[j + 1u];
            memset(&ext[entries - 1u], 0, sizeof(ext4_extent_t));
            hdr->eh_entries = entries - 1u;
        } else if (logical == first) {
            ext[i].ee_block = first + 1u;
            extent_set_start_block(&ext[i], extent_start_block(&ext[i]) + 1u);
            extent_set_len_blocks(&ext[i], len - 1u);
        } else if (logical == end - 1u) {
            extent_set_len_blocks(&ext[i], len - 1u);
        } else {
            u32 right_len = end - logical - 1u;
            u64 right_start = phys + 1u;
            extent_set_len_blocks(&ext[i], logical - first);
            for (u16 j = entries; j > i + 1u; --j) ext[j] = ext[j - 1u];
            memset(&ext[i + 1u], 0, sizeof(ext4_extent_t));
            ext[i + 1u].ee_block = logical + 1u;
            extent_set_len_blocks(&ext[i + 1u], right_len);
            extent_set_start_block(&ext[i + 1u], right_start);
            hdr->eh_entries = entries + 1u;
        }
        inode_add_blocks(mnt, inode, -1);
        return EXT4_OK;
    }
    return EXT4_OK;
}

static ext4_status_t inode_block_for_write(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical, u64 *phys_out, bool allocate) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) return extent_inline_block_for_write(mnt, inode, logical, phys_out, allocate);
    if (logical < 12u) {
        u32 b = le32(inode->i_block[logical]);
        if (!b && allocate) {
            u64 nb = 0;
            ext4_status_t st = alloc_block(mnt, &nb);
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
    u8 block[MAX_BLOCK_SIZE];
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) return st;
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b && allocate) {
        u64 nb = 0;
        st = alloc_block(mnt, &nb);
        if (st != EXT4_OK) return st;
        put32(&entries[idx], (u32)nb);
        st = write_block(mnt, indirect, block);
        if (st != EXT4_OK) return st;
        inode_add_blocks(mnt, inode, 1);
        b = (u32)nb;
    }
    if (!b) return EXT4_ERR_RANGE;
    *phys_out = b;
    return EXT4_OK;
}


static ext4_status_t inode_free_logical_block(ext4_mount_t *mnt, ext4_inode_disk_t *inode, u32 logical) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) return extent_inline_free_logical_block(mnt, inode, logical);
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
    u8 block[MAX_BLOCK_SIZE];
    ext4_status_t st = read_block(mnt, indirect, block);
    if (st != EXT4_OK) return st;
    u32 *entries = (u32 *)block;
    u32 idx = logical - 12u;
    u32 b = le32(entries[idx]);
    if (!b) return EXT4_OK;
    st = free_block(mnt, b);
    if (st != EXT4_OK) return st;
    put32(&entries[idx], 0);
    bool any = false;
    for (u32 i = 0; i < per_block; ++i) {
        if (le32(entries[i]) != 0) { any = true; break; }
    }
    if (any) {
        st = write_block(mnt, indirect, block);
        if (st != EXT4_OK) return st;
    } else {
        st = free_block(mnt, indirect);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = 0;
        inode_add_blocks(mnt, inode, -1);
    }
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
    ext4_status_t st = inode_block_for_write(mnt, inode, logical, &phys, false);
    if (st != EXT4_OK) return st == EXT4_ERR_RANGE ? EXT4_OK : st;
    u8 block[MAX_BLOCK_SIZE];
    st = read_block(mnt, phys, block);
    if (st != EXT4_OK) return st;
    memset(block + off, 0, (usize)mnt->block_size - off);
    return write_block(mnt, phys, block);
}

ext4_status_t ext4_write_file(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 offset, const void *buffer, usize bytes, usize *written_out) {
    if (written_out) *written_out = 0;
    if (!mnt || !inode || (!buffer && bytes)) return EXT4_ERR_IO;
    if (!ext4_inode_is_regular(inode)) return EXT4_ERR_UNSUPPORTED;
    if (mnt->block_size == 0) return EXT4_ERR_CORRUPT;
    if (bytes == 0) return EXT4_OK;
    u64 old_size = ext4_inode_size(inode);
    if (offset > old_size) {
        ext4_status_t zs = inode_zero_tail(mnt, inode, old_size);
        if (zs != EXT4_OK) return zs;
    }
    const u8 *in = (const u8 *)buffer;
    usize done = 0;
    u8 block[MAX_BLOCK_SIZE];
    while (done < bytes) {
        u64 abs = 0;
        if (!checked_add_u64(offset, (u64)done, &abs)) return EXT4_ERR_RANGE;
        u64 logical64 = abs / mnt->block_size;
        if (logical64 > 0xffffffffull) return EXT4_ERR_RANGE;
        u32 logical = (u32)logical64;
        u32 block_off = (u32)(abs % mnt->block_size);
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, inode, logical, &phys, true);
        if (st != EXT4_OK) return st;
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        if (block_off != 0 || take != (usize)mnt->block_size) {
            st = read_block(mnt, phys, block);
            if (st != EXT4_OK) return st;
        } else memset(block, 0, (usize)mnt->block_size);
        memcpy(block + block_off, in + done, take);
        st = write_block(mnt, phys, block);
        if (st != EXT4_OK) return st;
        done += take;
    }
    u64 end = 0;
    if (!checked_add_u64(offset, (u64)done, &end)) return EXT4_ERR_RANGE;
    if (end > ext4_inode_size(inode)) inode_set_size(inode, end);
    ext4_status_t st = write_inode(mnt, ino, inode);
    if (st != EXT4_OK) return st;
    if (written_out) *written_out = done;
    return EXT4_OK;
}


ext4_status_t ext4_truncate_file_path(ext4_mount_t *mnt, const char *path, u64 new_size) {
    if (!mnt || !path) return EXT4_ERR_RANGE;
    ext4_inode_disk_t inode;
    u32 ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, path, &inode, &ino);
    if (st != EXT4_OK) return st;
    if (ext4_inode_is_dir(&inode)) return EXT4_ERR_UNSUPPORTED;
    if (!ext4_inode_is_regular(&inode)) return EXT4_ERR_UNSUPPORTED;
    u64 old_size = ext4_inode_size(&inode);
    u64 old_blocks64 = div_round_up_u64(old_size, mnt->block_size);
    u64 new_blocks64 = div_round_up_u64(new_size, mnt->block_size);
    if (old_blocks64 > 0xffffffffull || new_blocks64 > 0xffffffffull) return EXT4_ERR_RANGE;
    if ((le32(inode.i_flags) & EXT4_EXTENTS_FL) != 0) {
        if (new_blocks64 > 32768ull) return EXT4_ERR_UNSUPPORTED;
    } else if (new_blocks64 > 12ull + (mnt->block_size / 4u)) return EXT4_ERR_UNSUPPORTED;
    if (new_blocks64 > old_blocks64) {
        st = inode_zero_tail(mnt, &inode, old_size);
        if (st != EXT4_OK) return st;
    } else if (new_blocks64 < old_blocks64) {
        for (u64 l = old_blocks64; l > new_blocks64; --l) {
            st = inode_free_logical_block(mnt, &inode, (u32)(l - 1u));
            if (st != EXT4_OK) return st;
        }
    }
    st = inode_zero_tail(mnt, &inode, new_size);
    if (st != EXT4_OK) return st;
    inode_set_size(&inode, new_size);
    return write_inode(mnt, ino, &inode);
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

static ext4_status_t dir_add_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 child_ino, u8 type) {
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    usize name_len = strlen(name);
    if (name_len == 0 || name_len > EXT4_NAME_LEN) return EXT4_ERR_RANGE;
    u16 need = ext4_rec_len(name_len);
    u8 block[MAX_BLOCK_SIZE];
    u64 size = ext4_inode_size(dir);
    u32 blocks = (u32)div_round_up_u64(size ? size : mnt->block_size, mnt->block_size);
    for (u32 logical = 0; logical < blocks; ++logical) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, logical, &phys, false);
        if (st != EXT4_OK) continue;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) return st;
        usize p = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) return EXT4_ERR_CORRUPT;
            if (de->inode == 0 && rec_len >= need) {
                make_dirent_at(block, p, child_ino, rec_len, type, name);
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) return st;
                return write_inode(mnt, dir_ino, dir);
            }
            u16 used = ext4_rec_len(de->name_len);
            if (de->inode != 0 && rec_len >= used + need) {
                de->rec_len = used;
                make_dirent_at(block, p + used, child_ino, rec_len - used, type, name);
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) return st;
                return write_inode(mnt, dir_ino, dir);
            }
            p += rec_len;
        }
    }
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, blocks, &phys, true);
    if (st != EXT4_OK) return st;
    memset(block, 0, (usize)mnt->block_size);
    make_dirent_at(block, 0, child_ino, (u16)mnt->block_size, type, name);
    st = write_block(mnt, phys, block);
    if (st != EXT4_OK) return st;
    inode_set_size(dir, size + mnt->block_size);
    return write_inode(mnt, dir_ino, dir);
}

static ext4_status_t dir_remove_entry(ext4_mount_t *mnt, u32 dir_ino, ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out) {
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    u8 block[MAX_BLOCK_SIZE];
    usize name_len = strlen(name);
    u64 size = ext4_inode_size(dir);
    for (u64 off = 0; off < size; off += mnt->block_size) {
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, (u32)(off / mnt->block_size), &phys, false);
        if (st != EXT4_OK) return st;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) return st;
        usize p = 0;
        ext4_dir_entry_2_t *prev = 0;
        while (p + 8u <= mnt->block_size) {
            ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            if (rec_len < 8 || p + rec_len > mnt->block_size) return EXT4_ERR_CORRUPT;
            if (de->inode && de->name_len == name_len && memcmp(de->name, name, de->name_len) == 0) {
                if (ino_out) *ino_out = le32(de->inode);
                if (type_out) *type_out = de->file_type;
                if (prev) {
                    u16 prev_len = le16(prev->rec_len);
                    prev->rec_len = (u16)(prev_len + rec_len);
                } else {
                    de->inode = 0;
                }
                st = write_block(mnt, phys, block);
                if (st != EXT4_OK) return st;
                return write_inode(mnt, dir_ino, dir);
            }
            prev = de;
            p += rec_len;
        }
    }
    return EXT4_ERR_NOT_FOUND;
}

static bool dir_is_empty_cb(const ext4_dirent_t *entry, void *ctx) {
    bool *empty = (bool *)ctx;
    if (strcmp(entry->name, ".") != 0 && strcmp(entry->name, "..") != 0) {
        *empty = false;
        return false;
    }
    return true;
}

static ext4_status_t free_inode_blocks(ext4_mount_t *mnt, ext4_inode_disk_t *inode) {
    if ((le32(inode->i_flags) & EXT4_EXTENTS_FL) != 0) {
        ext4_extent_header_t *hdr = 0;
        ext4_extent_t *ext = 0;
        ext4_status_t st = extent_inline_validate(inode, &hdr, &ext);
        if (st != EXT4_OK) return st;
        u16 entries = le16(hdr->eh_entries);
        for (u16 i = 0; i < entries; ++i) {
            u64 start = extent_start_block(&ext[i]);
            u32 len = extent_len_blocks(&ext[i]);
            for (u32 j = 0; j < len; ++j) {
                st = free_block(mnt, start + j);
                if (st != EXT4_OK) return st;
            }
        }
        memset(inode->i_block, 0, sizeof(inode->i_block));
        inode_set_size(inode, 0);
        inode->i_blocks_lo = 0;
        return EXT4_OK;
    }
    for (u32 i = 0; i < 12u; ++i) {
        u32 b = le32(inode->i_block[i]);
        if (b) { ext4_status_t st = free_block(mnt, b); if (st != EXT4_OK) return st; inode->i_block[i] = 0; }
    }
    u32 indirect = le32(inode->i_block[12]);
    if (indirect) {
        u8 block[MAX_BLOCK_SIZE];
        ext4_status_t st = read_block(mnt, indirect, block);
        if (st != EXT4_OK) return st;
        u32 *entries = (u32 *)block;
        u32 per = (u32)(mnt->block_size / 4u);
        for (u32 i = 0; i < per; ++i) {
            u32 b = le32(entries[i]);
            if (b) { st = free_block(mnt, b); if (st != EXT4_OK) return st; }
        }
        st = free_block(mnt, indirect);
        if (st != EXT4_OK) return st;
        inode->i_block[12] = 0;
    }
    inode_set_size(inode, 0);
    inode->i_blocks_lo = 0;
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
    if (!dir) ext4_init_inline_extent_tree(&inode);
    bool linked = false;
    u16 parent_links_before = le16(parent.i_links_count);
    if (dir) {
        u64 b = 0;
        st = alloc_block(mnt, &b);
        if (st != EXT4_OK) goto fail_inode;
        inode.i_block[0] = (u32)b;
        inode_add_blocks(mnt, &inode, 1);
        inode_set_size(&inode, mnt->block_size);
        u8 blk[MAX_BLOCK_SIZE];
        memset(blk, 0, (usize)mnt->block_size);
        u16 dot = ext4_rec_len(1);
        make_dirent_at(blk, 0, ino, dot, 2, ".");
        make_dirent_at(blk, dot, parent_ino, (u16)(mnt->block_size - dot), 2, "..");
        st = write_block(mnt, b, blk);
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
    st = free_inode_blocks(mnt, &inode);
    if (st != EXT4_OK) return st;
    memset(&inode, 0, sizeof(inode));
    st = write_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    return free_inode(mnt, ino, is_dir);
}


static ext4_status_t dir_update_dotdot(ext4_mount_t *mnt, ext4_inode_disk_t *dir, u32 new_parent_ino) {
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 phys = 0;
    ext4_status_t st = inode_block_for_write(mnt, dir, 0, &phys, false);
    if (st != EXT4_OK) return st;
    u8 block[MAX_BLOCK_SIZE];
    st = read_block(mnt, phys, block);
    if (st != EXT4_OK) return st;
    usize p = 0;
    while (p + 8u <= mnt->block_size) {
        ext4_dir_entry_2_t *de = (ext4_dir_entry_2_t *)(block + p);
        u16 rec_len = le16(de->rec_len);
        if (rec_len < 8u || p + rec_len > mnt->block_size) return EXT4_ERR_CORRUPT;
        if (de->inode && de->name_len == 2u && de->name[0] == '.' && de->name[1] == '.') {
            de->inode = new_parent_ino;
            return write_block(mnt, phys, block);
        }
        p += rec_len;
    }
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
    u32 old_parent_ino = 0, new_parent_ino = 0, src_ino = 0, tmp_ino = 0;
    u8 src_type = 0;
    ext4_status_t st = ext4_lookup_path(mnt, old_parent_path, &old_parent, &old_parent_ino);
    if (st != EXT4_OK) return st;
    st = ext4_lookup_path(mnt, new_parent_path, &new_parent, &new_parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&old_parent) || !ext4_inode_is_dir(&new_parent)) return EXT4_ERR_NOT_DIR;
    st = ext4_find_in_dir(mnt, &old_parent, old_name, &src_ino, &src_type);
    if (st != EXT4_OK) return st;
    if (ext4_find_in_dir(mnt, &new_parent, new_name, &tmp_ino, 0) == EXT4_OK) return EXT4_ERR_EXIST;
    st = ext4_read_inode(mnt, src_ino, &inode);
    if (st != EXT4_OK) return st;
    bool is_dir = ext4_inode_is_dir(&inode);
    if (!is_dir && !ext4_inode_is_regular(&inode)) return EXT4_ERR_UNSUPPORTED;
    if (is_dir) {
        usize old_len = strlen(old_path);
        if (strncmp(new_path, old_path, old_len) == 0 && (new_path[old_len] == '/' || new_path[old_len] == 0)) return EXT4_ERR_RANGE;
    }

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

ext4_status_t ext4_list_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_dir_iter_fn fn, void *ctx) {
    if (!mnt || !dir || !fn) return EXT4_ERR_IO;
    if (!ext4_inode_is_dir(dir)) return EXT4_ERR_NOT_DIR;
    u64 size = ext4_inode_size(dir);
    u8 block[MAX_BLOCK_SIZE];
    for (u64 off = 0; off < size; off += mnt->block_size) {
        usize got = 0;
        ext4_status_t st = ext4_read_file(mnt, dir, off, block, (usize)mnt->block_size, &got);
        if (st != EXT4_OK) return st;
        usize p = 0;
        while (p + 8 <= got) {
            const ext4_dir_entry_2_t *de = (const ext4_dir_entry_2_t *)(block + p);
            u16 rec_len = le16(de->rec_len);
            usize next = 0;
            if (rec_len < 8 || !checked_add_usize(p, rec_len, &next) || next > got) return EXT4_ERR_CORRUPT;
            if (de->inode != 0 && 8u + de->name_len <= rec_len) {
                ext4_dirent_t e;
                memset(&e, 0, sizeof(e));
                e.inode = le32(de->inode);
                e.file_type = de->file_type;
                memcpy(e.name, de->name, de->name_len);
                e.name[de->name_len] = 0;
                if (!fn(&e, ctx)) return EXT4_OK;
            }
            p = next;
        }
    }
    return EXT4_OK;
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

ext4_status_t ext4_lookup_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out) {
    if (!mnt || !path || !inode_out) return EXT4_ERR_RANGE;
    ext4_inode_disk_t cur;
    ext4_status_t st = ext4_read_inode(mnt, EXT4_ROOT_INO, &cur);
    if (st != EXT4_OK) return st;
    u32 cur_ino = EXT4_ROOT_INO;
    while (*path == '/') ++path;
    if (*path == 0) {
        *inode_out = cur;
        if (ino_out) *ino_out = cur_ino;
        return EXT4_OK;
    }
    char comp[EXT4_NAME_LEN + 1];
    const char *p = path;
    while (*p) {
        while (*p == '/') ++p;
        const char *start = p;
        while (*p && *p != '/') ++p;
        usize len = (usize)(p - start);
        if (len == 0) break;
        if (len > EXT4_NAME_LEN) return EXT4_ERR_RANGE;
        memcpy(comp, start, len);
        comp[len] = 0;
        if (strcmp(comp, ".") == 0) continue;
        u32 next_ino = 0;
        st = ext4_find_in_dir(mnt, &cur, comp, &next_ino, 0);
        if (st != EXT4_OK) return st;
        st = ext4_read_inode(mnt, next_ino, &cur);
        if (st != EXT4_OK) return st;
        cur_ino = next_ino;
    }
    *inode_out = cur;
    if (ino_out) *ino_out = cur_ino;
    return EXT4_OK;
}
