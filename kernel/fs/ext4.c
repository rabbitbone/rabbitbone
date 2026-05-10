#include <aurora/ext4.h>
#include <aurora/libc.h>

#define EXT4_FEATURE_INCOMPAT_FILETYPE 0x0002u
#define EXT4_FEATURE_INCOMPAT_EXTENTS  0x0040u
#define EXT4_FEATURE_INCOMPAT_64BIT    0x0080u
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE 0x0008u
#define EXT4_EXTENTS_FL 0x00080000u
#define EXT4_IFDIR 0x4000u
#define EXT4_IFREG 0x8000u
#define EXT4_EXT_MAGIC 0xf30au
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
static u64 le64_from_lo_hi(u32 lo, u32 hi) { return ((u64)le32(hi) << 32) | le32(lo); }
static bool checked_add_u64(u64 a, u64 b, u64 *out) { return !__builtin_add_overflow(a, b, out); }
static bool checked_mul_u64(u64 a, u64 b, u64 *out) { return !__builtin_mul_overflow(a, b, out); }
static bool checked_add_usize(usize a, usize b, usize *out) { return !__builtin_add_overflow(a, b, out); }
static u64 div_round_up_u64(u64 a, u64 b) { return b ? (a + b - 1) / b : 0; }

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

static ext4_status_t read_block(ext4_mount_t *mnt, u64 block, void *buffer) {
    if (block >= mnt->blocks_count) return EXT4_ERR_RANGE;
    u64 byte_offset = 0;
    if (!checked_mul_u64(block, mnt->block_size, &byte_offset)) return EXT4_ERR_RANGE;
    return read_bytes(mnt->dev, mnt->partition_lba, byte_offset, buffer, (usize)mnt->block_size);
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
        default: return "unknown";
    }
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
    out->inodes_per_group = le32(sb.s_inodes_per_group);
    out->blocks_per_group = le32(sb.s_blocks_per_group);
    out->inode_size = le16(sb.s_inode_size) ? le16(sb.s_inode_size) : 128;
    out->group_desc_size = (incompat & EXT4_FEATURE_INCOMPAT_64BIT) ? le16(sb.s_desc_size) : 32;
    u64 first_data = le32(sb.s_first_data_block);
    if (!out->blocks_count || !out->blocks_per_group || !out->inodes_per_group) return EXT4_ERR_CORRUPT;
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
            u32 len = le16(ext[i].ee_len) & 0x7fffu;
            u32 end = 0;
            if (len == 0 || __builtin_add_overflow(first, len, &end)) return EXT4_ERR_CORRUPT;
            if (logical >= first && logical < end) {
                u64 start = le64_from_lo_hi(ext[i].ee_start_lo, ext[i].ee_start_hi);
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
    if (!mnt || !inode || !buffer) return EXT4_ERR_IO;
    u64 size = ext4_inode_size(inode);
    if (offset >= size) return EXT4_OK;
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
        u64 phys = 0;
        ext4_status_t st = map_inode_block(mnt, inode, logical, &phys);
        if (st != EXT4_OK) return st;
        st = read_block(mnt, phys, block);
        if (st != EXT4_OK) return st;
        usize take = (usize)mnt->block_size - block_off;
        if (take > bytes - done) take = bytes - done;
        memcpy(out + done, block + block_off, take);
        done += take;
    }
    if (read_out) *read_out = done;
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
