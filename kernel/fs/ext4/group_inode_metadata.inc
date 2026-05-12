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

