static vfs_node_type_t node_type(const ramfs_node_t *n) { return (n && n->obj) ? n->obj->type : 0; }

static bool str_append(char *dst, usize cap, const char *src) {
    if (!dst || !src || cap == 0) return false;
    usize dl = strlen(dst);
    usize sl = strlen(src);
    if (dl >= cap || sl >= cap - dl) return false;
    memcpy(dst + dl, src, sl + 1u);
    return true;
}

static bool str_set_join(char *dst, usize cap, const char *a, const char *b, const char *c) {
    if (!dst || cap == 0) return false;
    dst[0] = 0;
    return (!a || str_append(dst, cap, a)) &&
           (!b || str_append(dst, cap, b)) &&
           (!c || str_append(dst, cap, c));
}

static bool inode_exists_in(ramfs_node_t *n, u32 ino) {
    if (!n || !n->obj) return false;
    if (n->obj->inode == ino) return true;
    if (n->obj->type == VFS_NODE_DIR) {
        for (ramfs_node_t *c = n->obj->children; c; c = c->next) if (inode_exists_in(c, ino)) return true;
    }
    return false;
}

static ramfs_node_t *node_find_inode(ramfs_node_t *n, u32 ino) {
    if (!n || !n->obj || ino == 0) return 0;
    if (n->obj->inode == ino) return n;
    if (n->obj->type == VFS_NODE_DIR) {
        for (ramfs_node_t *c = n->obj->children; c; c = c->next) {
            ramfs_node_t *hit = node_find_inode(c, ino);
            if (hit) return hit;
        }
    }
    return 0;
}

static u32 ramfs_alloc_inode(ramfs_t *fs) {
    if (!fs) return 0;
    for (u32 tries = 0; tries < 0xffffffffu; ++tries) {
        u32 ino = fs->next_inode++;
        if (fs->next_inode == 0) fs->next_inode = 1;
        if (ino != 0 && !inode_exists_in(fs->root, ino)) return ino;
    }
    return 0;
}

static ramfs_object_t *object_create(ramfs_t *fs, vfs_node_type_t type) {
    ramfs_object_t *o = (ramfs_object_t *)kcalloc(1, sizeof(*o));
    if (!o) return 0;
    o->type = type;
    o->inode = ramfs_alloc_inode(fs);
    if (o->inode == 0) { kfree(o); return 0; }
    o->mode = type == VFS_NODE_DIR ? 0755u : (type == VFS_NODE_SYMLINK ? 0777u : 0644u);
    o->nlink = type == VFS_NODE_DIR ? 2u : 1u;
    o->uid = RABBITBONE_UID_ROOT;
    o->gid = RABBITBONE_GID_ROOT;
    return o;
}

static bool ramfs_can_add_node(ramfs_t *fs) {
    return fs && fs->node_count < (fs->max_nodes ? fs->max_nodes : RAMFS_MAX_NODES);
}

static bool ramfs_can_add_bytes(ramfs_t *fs, usize add) {
    if (!fs) return false;
    usize max = fs->max_bytes ? fs->max_bytes : RAMFS_MAX_TOTAL_BYTES;
    return add <= max && fs->bytes_used <= max - add;
}

static ramfs_node_t *dentry_create(ramfs_t *fs, const char *name, vfs_node_type_t type) {
    if (!ramfs_can_add_node(fs)) return 0;
    ramfs_node_t *n = (ramfs_node_t *)kcalloc(1, sizeof(*n));
    if (!n) return 0;
    n->obj = object_create(fs, type);
    if (!n->obj) { kfree(n); return 0; }
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1u);
    ++fs->node_count;
    return n;
}

static ramfs_node_t *dentry_alias_create(ramfs_t *fs, const char *name, ramfs_object_t *obj) {
    if (!ramfs_can_add_node(fs) || !obj || obj->type == VFS_NODE_DIR || obj->nlink == 0xffffffffu) return 0;
    ramfs_node_t *n = (ramfs_node_t *)kcalloc(1, sizeof(*n));
    if (!n) return 0;
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1u);
    n->obj = obj;
    ++obj->nlink;
    ++fs->node_count;
    return n;
}

static ramfs_node_t *find_child(ramfs_node_t *dir, const char *name) {
    if (!dir || node_type(dir) != VFS_NODE_DIR) return 0;
    for (ramfs_node_t *c = dir->obj->children; c; c = c->next) if (strcmp(c->name, name) == 0) return c;
    return 0;
}

static void attach_child(ramfs_node_t *dir, ramfs_node_t *child) {
    child->parent = dir;
    child->next = dir->obj->children;
    dir->obj->children = child;
}

static void object_free(ramfs_t *fs, ramfs_object_t *o) {
    if (!o) return;
    if (fs && o->data && fs->bytes_used >= o->capacity) fs->bytes_used -= o->capacity;
    else if (fs && o->data) fs->bytes_used = 0;
    kfree(o->data);
    kfree(o);
}

static void free_dentry(ramfs_t *fs, ramfs_node_t *n) {
    if (!n) return;
    ramfs_object_t *o = n->obj;
    if (o && o->type == VFS_NODE_DIR) {
        ramfs_node_t *c = o->children;
        while (c) {
            ramfs_node_t *next = c->next;
            free_dentry(fs, c);
            c = next;
        }
        o->children = 0;
        object_free(fs, o);
    } else if (o) {
        if (o->nlink > 0) --o->nlink;
        if (o->nlink == 0) object_free(fs, o);
    }
    if (fs && fs->node_count) --fs->node_count;
    kfree(n);
}

