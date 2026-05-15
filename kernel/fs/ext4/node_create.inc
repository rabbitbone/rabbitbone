static ext4_status_t create_node(ext4_mount_t *mnt, const char *path, bool dir, const void *data, usize size) {
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (!ext4_name_cstr_valid(name, false)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    if (!ext4_inode_is_dir(&parent)) return EXT4_ERR_NOT_DIR;
    u32 tmp_ino = 0;
    st = ext4_find_in_dir(mnt, &parent, name, &tmp_ino, 0);
    if (st == EXT4_OK) return EXT4_ERR_EXIST;
    if (st != EXT4_ERR_NOT_FOUND) return st;
    u16 parent_links_before = le16(parent.i_links_count);
    if (dir && parent_links_before == 0xffffu) return EXT4_ERR_RANGE;
    u32 ino = 0;
    st = alloc_inode(mnt, dir, &ino);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.i_mode = (u16)((dir ? EXT4_IFDIR : EXT4_IFREG) | (dir ? 0755 : 0644));
    inode.i_links_count = dir ? 2u : 1u;
    ext4_init_inline_extent_tree(&inode);
    bool linked = false;
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
        if (!ext4_links_inc_checked(&parent)) { st = EXT4_ERR_RANGE; goto fail_linked; }
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
    if (!ext4_symlink_target_valid(target, len)) return EXT4_ERR_RANGE;
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(link_path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (!ext4_name_cstr_valid(name, false)) return EXT4_ERR_RANGE;
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

