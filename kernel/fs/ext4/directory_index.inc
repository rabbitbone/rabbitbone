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
    if (le32(hdr->dir_ino) == 0) return EXT4_ERR_CORRUPT;
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
    if (logical > 0xffffu || offset > 0xffffu) return EXT4_ERR_UNSUPPORTED;
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
    if (div_round_up_u64(ext4_inode_size(dir), mnt->block_size) > 0xffffu) return EXT4_ERR_UNSUPPORTED;
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
        u64 logical64 = off / mnt->block_size;
        if (logical64 > 0xffffffffull) { result = EXT4_ERR_RANGE; goto fail; }
        u64 phys = 0;
        st = inode_block_for_write(mnt, dir, (u32)logical64, &phys, false, false, false);
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
                u64 logical64 = off / mnt->block_size;
                if (logical64 > 0xffffu || p > 0xffffu) { result = EXT4_ERR_UNSUPPORTED; goto fail; }
                e.logical_block = (u16)logical64;
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
    u32 old_acl = dir->i_file_acl_lo;
    u32 old_flags = dir->i_flags;
    u32 old_blocks = dir->i_blocks_lo;
    dir->i_file_acl_lo = (u32)idx_block;
    dir->i_flags |= EXT4_INDEX_FL;
    inode_add_blocks(mnt, dir, 1);
    result = write_inode(mnt, dir_ino, dir);
    if (result != EXT4_OK) {
        dir->i_file_acl_lo = old_acl;
        dir->i_flags = old_flags;
        dir->i_blocks_lo = old_blocks;
        (void)free_block(mnt, idx_block);
    }
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
            u64 logical64 = off / mnt->block_size;
            if (logical64 > 0xffffffffull) { ext4_tmp_block_free(block); return EXT4_ERR_RANGE; }
            u64 phys = 0;
            if (inode_block_for_write(mnt, dir, (u32)logical64, &phys, false, false, false) != EXT4_OK) continue;
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
    if (logical > 0xffffu || offset > 0xffffu) return EXT4_ERR_UNSUPPORTED;
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
        u64 logical64 = off / mnt->block_size;
        if (logical64 > 0xffffffffull) { result = EXT4_ERR_RANGE; goto out; }
        u64 phys = 0;
        ext4_status_t st = inode_block_for_write(mnt, dir, (u32)logical64, &phys, false, false, false);
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


