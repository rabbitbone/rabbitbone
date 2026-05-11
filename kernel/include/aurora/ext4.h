#ifndef AURORA_EXT4_H
#define AURORA_EXT4_H
#include <aurora/types.h>
#include <aurora/block.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define EXT4_SUPER_MAGIC 0xef53u
#define EXT4_N_BLOCKS 15u
#define EXT4_NAME_LEN 255u
#define EXT4_ROOT_INO 2u

typedef enum ext4_status {
    EXT4_OK = 0,
    EXT4_ERR_IO = -1,
    EXT4_ERR_BAD_MAGIC = -2,
    EXT4_ERR_UNSUPPORTED = -3,
    EXT4_ERR_RANGE = -4,
    EXT4_ERR_CORRUPT = -5,
    EXT4_ERR_NOT_FOUND = -6,
    EXT4_ERR_NOT_DIR = -7,
    EXT4_ERR_NO_MEMORY = -8,
    EXT4_ERR_EXIST = -9,
    EXT4_ERR_NOT_EMPTY = -10,
} ext4_status_t;

typedef struct AURORA_PACKED ext4_superblock_disk {
    u32 s_inodes_count;
    u32 s_blocks_count_lo;
    u32 s_r_blocks_count_lo;
    u32 s_free_blocks_count_lo;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_cluster_size;
    u32 s_blocks_per_group;
    u32 s_clusters_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8 s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;
    u8 s_prealloc_blocks;
    u8 s_prealloc_dir_blocks;
    u16 s_reserved_gdt_blocks;
    u8 s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;
    u32 s_hash_seed[4];
    u8 s_def_hash_version;
    u8 s_jnl_backup_type;
    u16 s_desc_size;
    u32 s_default_mount_opts;
    u32 s_first_meta_bg;
    u32 s_mkfs_time;
    u32 s_jnl_blocks[17];
    u32 s_blocks_count_hi;
    u32 s_r_blocks_count_hi;
    u32 s_free_blocks_count_hi;
    u16 s_min_extra_isize;
    u16 s_want_extra_isize;
    u32 s_flags;
    u16 s_raid_stride;
    u16 s_mmp_interval;
    u64 s_mmp_block;
    u32 s_raid_stripe_width;
    u8 s_log_groups_per_flex;
    u8 s_checksum_type;
    u16 s_reserved_pad;
    u64 s_kbytes_written;
    u32 s_snapshot_inum;
    u32 s_snapshot_id;
    u64 s_snapshot_r_blocks_count;
    u32 s_snapshot_list;
    u32 s_error_count;
    u32 s_first_error_time;
    u32 s_first_error_ino;
    u64 s_first_error_block;
    u8 s_first_error_func[32];
    u32 s_first_error_line;
    u32 s_last_error_time;
    u32 s_last_error_ino;
    u32 s_last_error_line;
    u64 s_last_error_block;
    u8 s_last_error_func[32];
    u8 s_mount_opts[64];
    u32 s_usr_quota_inum;
    u32 s_grp_quota_inum;
    u32 s_overhead_clusters;
    u32 s_backup_bgs[2];
    u8 s_encrypt_algos[4];
    u8 s_encrypt_pw_salt[16];
    u32 s_lpf_ino;
    u32 s_prj_quota_inum;
    u32 s_checksum_seed;
    u8 s_wtime_hi;
    u8 s_mtime_hi;
    u8 s_mkfs_time_hi;
    u8 s_lastcheck_hi;
    u8 s_first_error_time_hi;
    u8 s_last_error_time_hi;
    u8 s_pad[2];
    u16 s_encoding;
    u16 s_encoding_flags;
    u32 s_reserved[95];
    u32 s_checksum;
} ext4_superblock_disk_t;

typedef struct AURORA_PACKED ext4_group_desc_disk {
    u32 bg_block_bitmap_lo;
    u32 bg_inode_bitmap_lo;
    u32 bg_inode_table_lo;
    u16 bg_free_blocks_count_lo;
    u16 bg_free_inodes_count_lo;
    u16 bg_used_dirs_count_lo;
    u16 bg_flags;
    u32 bg_exclude_bitmap_lo;
    u16 bg_block_bitmap_csum_lo;
    u16 bg_inode_bitmap_csum_lo;
    u16 bg_itable_unused_lo;
    u16 bg_checksum;
    u32 bg_block_bitmap_hi;
    u32 bg_inode_bitmap_hi;
    u32 bg_inode_table_hi;
    u16 bg_free_blocks_count_hi;
    u16 bg_free_inodes_count_hi;
    u16 bg_used_dirs_count_hi;
    u16 bg_itable_unused_hi;
    u32 bg_exclude_bitmap_hi;
    u16 bg_block_bitmap_csum_hi;
    u16 bg_inode_bitmap_csum_hi;
    u32 bg_reserved;
} ext4_group_desc_disk_t;

typedef struct AURORA_PACKED ext4_inode_disk {
    u16 i_mode;
    u16 i_uid;
    u32 i_size_lo;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks_lo;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[EXT4_N_BLOCKS];
    u32 i_generation;
    u32 i_file_acl_lo;
    u32 i_size_high;
    u32 i_obso_faddr;
    u8 i_osd2[12];
    u16 i_extra_isize;
    u16 i_checksum_hi;
    u32 i_ctime_extra;
    u32 i_mtime_extra;
    u32 i_atime_extra;
    u32 i_crtime;
    u32 i_crtime_extra;
    u32 i_version_hi;
    u32 i_projid;
} ext4_inode_disk_t;

typedef struct ext4_mount {
    block_device_t *dev;
    u64 partition_lba;
    u64 block_size;
    u64 blocks_count;
    u32 inodes_count;
    u32 inodes_per_group;
    u32 blocks_per_group;
    u16 inode_size;
    u16 group_desc_size;
    u32 group_count;
    ext4_superblock_disk_t sb;
} ext4_mount_t;

typedef struct ext4_dirent {
    u32 inode;
    u8 file_type;
    char name[EXT4_NAME_LEN + 1];
} ext4_dirent_t;

typedef bool (*ext4_dir_iter_fn)(const ext4_dirent_t *entry, void *ctx);

ext4_status_t ext4_mount(block_device_t *dev, u64 partition_lba, ext4_mount_t *out);
ext4_status_t ext4_read_inode(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *out);
ext4_status_t ext4_read_file(ext4_mount_t *mnt, const ext4_inode_disk_t *inode, u64 offset, void *buffer, usize bytes, usize *read_out);
ext4_status_t ext4_write_file(ext4_mount_t *mnt, u32 ino, ext4_inode_disk_t *inode, u64 offset, const void *buffer, usize bytes, usize *written_out);
ext4_status_t ext4_create_file(ext4_mount_t *mnt, const char *path, const void *data, usize size);
ext4_status_t ext4_mkdir(ext4_mount_t *mnt, const char *path);
ext4_status_t ext4_unlink(ext4_mount_t *mnt, const char *path);
ext4_status_t ext4_truncate_file_path(ext4_mount_t *mnt, const char *path, u64 size);
ext4_status_t ext4_rename(ext4_mount_t *mnt, const char *old_path, const char *new_path);
ext4_status_t ext4_list_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, ext4_dir_iter_fn fn, void *ctx);
ext4_status_t ext4_find_in_dir(ext4_mount_t *mnt, const ext4_inode_disk_t *dir, const char *name, u32 *ino_out, u8 *type_out);
ext4_status_t ext4_lookup_path(ext4_mount_t *mnt, const char *path, ext4_inode_disk_t *inode_out, u32 *ino_out);
const char *ext4_status_name(ext4_status_t status);
u64 ext4_inode_size(const ext4_inode_disk_t *inode);
bool ext4_inode_is_dir(const ext4_inode_disk_t *inode);
bool ext4_inode_is_regular(const ext4_inode_disk_t *inode);

#if defined(__cplusplus)
}
#endif
#endif
