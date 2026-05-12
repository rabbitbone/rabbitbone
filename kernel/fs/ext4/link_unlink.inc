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
    if (!mnt || !path) return EXT4_ERR_RANGE;
    char parent_path[VFS_PATH_MAX];
    char name[EXT4_NAME_LEN + 1];
    if (!split_parent_path(path, parent_path, sizeof(parent_path), name, sizeof(name))) return EXT4_ERR_RANGE;
    if (ext4_name_is_dot_or_dotdot(name)) return EXT4_ERR_RANGE;
    ext4_inode_disk_t parent;
    u32 parent_ino = 0;
    ext4_status_t st = ext4_lookup_path(mnt, parent_path, &parent, &parent_ino);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t parent_before = parent;
    u32 ino = 0;
    u8 type = 0;
    st = ext4_find_in_dir(mnt, &parent, name, &ino, &type);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode;
    st = ext4_read_inode(mnt, ino, &inode);
    if (st != EXT4_OK) return st;
    ext4_inode_disk_t inode_before = inode;
    bool is_dir = ext4_inode_is_dir(&inode);
    if (is_dir) {
        bool empty = true;
        st = ext4_list_dir(mnt, &inode, dir_is_empty_cb, &empty);
        if (st != EXT4_OK) return st;
        if (!empty) return EXT4_ERR_NOT_EMPTY;
    }
    st = dir_remove_entry(mnt, parent_ino, &parent, name, 0, 0);
    if (st != EXT4_OK) return st;
    bool parent_nlink_changed = false;
    if (is_dir && le16(parent.i_links_count) > 0) {
        parent.i_links_count = (u16)(le16(parent.i_links_count) - 1u);
        st = write_inode(mnt, parent_ino, &parent);
        if (st != EXT4_OK) {
            (void)dir_add_entry(mnt, parent_ino, &parent_before, name, ino, type);
            return st;
        }
        parent_nlink_changed = true;
    }
    st = drop_removed_link(mnt, ino, &inode, is_dir);
    if (st != EXT4_OK) {
        if (parent_nlink_changed) (void)write_inode(mnt, parent_ino, &parent_before);
        (void)write_inode(mnt, ino, &inode_before);
        (void)dir_add_entry(mnt, parent_ino, &parent_before, name, ino, type);
        return st;
    }
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

