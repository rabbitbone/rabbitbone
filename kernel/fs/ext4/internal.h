#pragma once

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

#define EXT4_DEFERRED_FREE_MAX 256u
typedef struct ext4_deferred_free {
    u64 blocks[EXT4_DEFERRED_FREE_MAX];
    u16 count;
} ext4_deferred_free_t;

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
static u64 div_round_up_u64(u64 a, u64 b) { return b ? (a / b) + ((a % b) != 0) : 0; }
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

