static bool node_is_ancestor(ramfs_node_t *ancestor, ramfs_node_t *n) {
    for (ramfs_node_t *p = n; p; p = p->parent) if (p == ancestor) return true;
    return false;
}

static vfs_status_t op_rename(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    u64 flags = spin_lock_irqsave(&fs->lock);
    ramfs_node_t *old_parent = 0;
    ramfs_node_t *new_parent = 0;
    char old_leaf[VFS_NAME_MAX];
    char new_leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, old_path, &old_parent, old_leaf, sizeof(old_leaf));
    if (st != VFS_OK) goto out;
    st = resolve_parent(fs, new_path, &new_parent, new_leaf, sizeof(new_leaf));
    if (st != VFS_OK) goto out;
    ramfs_node_t *prev = 0;
    ramfs_node_t *n = old_parent->obj->children;
    while (n && strcmp(n->name, old_leaf) != 0) { prev = n; n = n->next; }
    if (!n) { st = VFS_ERR_NOENT; goto out; }
    if (node_type(n) == VFS_NODE_DIR && node_is_ancestor(n, new_parent)) { st = VFS_ERR_INVAL; goto out; }
    ramfs_node_t *dst_prev = 0;
    ramfs_node_t *dst = new_parent->obj->children;
    while (dst && strcmp(dst->name, new_leaf) != 0) { dst_prev = dst; dst = dst->next; }
    if (dst == n) { st = VFS_OK; goto out; }
    bool dir_cross_parent = node_type(n) == VFS_NODE_DIR && old_parent != new_parent;
    bool dst_is_dir = dst && node_type(dst) == VFS_NODE_DIR;
    if (dir_cross_parent && !dst_is_dir && (!new_parent->obj || new_parent->obj->nlink == 0xffffffffu)) { st = VFS_ERR_NOSPC; goto out; }
    if (dst) {
        if (node_type(dst) != node_type(n)) { st = VFS_ERR_EXIST; goto out; }
        if (node_type(dst) == VFS_NODE_DIR && dst->obj->children) { st = VFS_ERR_NOTEMPTY; goto out; }
        if (dst_prev) dst_prev->next = dst->next;
        else new_parent->obj->children = dst->next;
        if (node_type(dst) == VFS_NODE_DIR && new_parent->obj->nlink > 0) --new_parent->obj->nlink;
        dst->next = 0;
        free_dentry(fs, dst);
    }
    if (prev) prev->next = n->next;
    else old_parent->obj->children = n->next;
    if (node_type(n) == VFS_NODE_DIR && old_parent != new_parent) {
        if (old_parent->obj->nlink > 0) --old_parent->obj->nlink;
        ++new_parent->obj->nlink;
    }
    n->next = 0;
    strncpy(n->name, new_leaf, sizeof(n->name) - 1u);
    n->name[sizeof(n->name) - 1u] = 0;
    attach_child(new_parent, n);
    st = VFS_OK;
out:
    spin_unlock_irqrestore(&fs->lock, flags);
    return st;
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .lstat = op_lstat,
    .readlink = op_readlink,
    .symlink = op_symlink,
    .link = op_link,
    .read = op_read,
    .write = op_write,
    .stat_inode = op_stat_inode,
    .read_inode = op_read_inode,
    .write_inode = op_write_inode,
    .truncate_inode = op_truncate_inode,
    .list = op_list,
    .list_inode = op_list_inode,
    .mkdir = op_mkdir,
    .create = op_create,
    .unlink = op_unlink,
    .truncate = op_truncate,
    .preallocate = op_preallocate,
    .preallocate_inode = op_preallocate_inode,
    .rename = op_rename,
    .chmod = op_chmod,
    .chown = op_chown,
    .sync = op_sync,
    .sync_inode = op_sync_inode,
};

const vfs_ops_t *ramfs_ops(void) { return &ops; }

vfs_status_t ramfs_mount_boot(void) {
    ramfs_t *fs = ramfs_create("bootram");
    if (!fs) return VFS_ERR_NOMEM;
    vfs_status_t st = ramfs_add_dir(fs, "/etc");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/bin");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/sbin");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/tmp");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/home");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/home/rabbitbone");
    static const char motd[] = RABBITBONE_VERSION_FULL "\n";
    static const char version[] = RABBITBONE_UNAME_TEXT "\n";
    static const char profile[] = "PATH=/disk0/bin:/disk0/sbin:/bin:/sbin\nSHELL=/disk0/bin/sh\n";
    static const char passwd[] = "root:x:0:0:root:/root:/bin/sh\nrabbitbone:x:1000:1000:Rabbitbone User:/home/rabbitbone:/bin/sh\nguest:x:1001:1000:Guest:/tmp:/bin/sh\n";
    static const char group[] = "root:x:0:\nusers:x:1000:rabbitbone,guest\nadmin:x:27:rabbitbone\n";
    static const char sudoers[] = "root ALL=(ALL) ALL\nrabbitbone ALL=(ALL) ALL\n";
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/motd", motd, sizeof(motd) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/version", version, sizeof(version) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/profile", profile, sizeof(profile) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/passwd", passwd, sizeof(passwd) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/group", group, sizeof(group) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/sudoers", sudoers, sizeof(sudoers) - 1u);
    static const char readme[] = "Rabbitbone /bin/sh /sbin/init\n";
    if (st == VFS_OK) st = ramfs_add_file(fs, "/bin/readme", readme, sizeof(readme) - 1u);
    if (st == VFS_OK) { (void)op_chmod(&(vfs_mount_t){ .ctx = fs }, "/tmp", 01777u); }
    if (st == VFS_OK) { (void)op_chown(&(vfs_mount_t){ .ctx = fs }, "/home/rabbitbone", RABBITBONE_UID_RABBITBONE, RABBITBONE_GID_USERS); (void)op_chmod(&(vfs_mount_t){ .ctx = fs }, "/home/rabbitbone", 0755u); }
    if (st != VFS_OK) { ramfs_destroy(fs); return st; }
    return vfs_mount("/", "ramfs", ramfs_ops(), fs, true);
}
