#include <aurora/ramfs.h>
#include <aurora/version.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>
#include <aurora/log.h>

#define RAMFS_MAX_FILE_SIZE (16ull * 1024ull * 1024ull)
#define RAMFS_SYMLINK_MAX_DEPTH 16u

typedef struct ramfs_node ramfs_node_t;

typedef struct ramfs_object {
    vfs_node_type_t type;
    u32 inode;
    u32 mode;
    u32 nlink;
    u8 *data;
    usize size;
    usize capacity;
    ramfs_node_t *children;
} ramfs_object_t;

struct ramfs_node {
    char name[VFS_NAME_MAX];
    ramfs_object_t *obj;
    ramfs_node_t *parent;
    ramfs_node_t *next;
};

struct ramfs {
    char label[VFS_NAME_MAX];
    ramfs_node_t *root;
    u32 next_inode;
};

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
    return o;
}

static ramfs_node_t *dentry_create(ramfs_t *fs, const char *name, vfs_node_type_t type) {
    ramfs_node_t *n = (ramfs_node_t *)kcalloc(1, sizeof(*n));
    if (!n) return 0;
    n->obj = object_create(fs, type);
    if (!n->obj) { kfree(n); return 0; }
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1u);
    return n;
}

static ramfs_node_t *dentry_alias_create(const char *name, ramfs_object_t *obj) {
    if (!obj || obj->type == VFS_NODE_DIR || obj->nlink == 0xffffffffu) return 0;
    ramfs_node_t *n = (ramfs_node_t *)kcalloc(1, sizeof(*n));
    if (!n) return 0;
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1u);
    n->obj = obj;
    ++obj->nlink;
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

static void object_free(ramfs_object_t *o) {
    if (!o) return;
    kfree(o->data);
    kfree(o);
}

static void free_dentry(ramfs_node_t *n) {
    if (!n) return;
    ramfs_object_t *o = n->obj;
    if (o && o->type == VFS_NODE_DIR) {
        ramfs_node_t *c = o->children;
        while (c) {
            ramfs_node_t *next = c->next;
            free_dentry(c);
            c = next;
        }
        o->children = 0;
        object_free(o);
    } else if (o) {
        if (o->nlink > 0) --o->nlink;
        if (o->nlink == 0) object_free(o);
    }
    kfree(n);
}

static bool compose_symlink_path(const char *base_dir, const char *target, const char *remaining, char *out, usize out_size) {
    if (!target || !target[0] || !out || out_size == 0) return false;
    char tmp[VFS_PATH_MAX];
    memset(tmp, 0, sizeof(tmp));
    if (target[0] == '/') {
        strncpy(tmp, target, sizeof(tmp) - 1u);
    } else {
        const char *base = (base_dir && base_dir[0]) ? base_dir : "/";
        if (strcmp(base, "/") == 0) {
            if (!str_set_join(tmp, sizeof(tmp), "/", target, 0)) return false;
        } else {
            if (!str_set_join(tmp, sizeof(tmp), base, "/", target)) return false;
        }
    }
    if (remaining && remaining[0]) {
        usize len = strlen(tmp);
        if (len + 1u + strlen(remaining) >= sizeof(tmp)) return false;
        if (len == 0 || tmp[len - 1u] != '/') { if (!str_append(tmp, sizeof(tmp), "/")) return false; }
        if (!str_append(tmp, sizeof(tmp), remaining)) return false;
    }
    return path_normalize(tmp, out, out_size);
}

static ramfs_node_t *resolve_inner(ramfs_t *fs, const char *path, bool follow_final, u32 depth) {
    if (!fs || !path || depth > RAMFS_SYMLINK_MAX_DEPTH) return 0;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return 0;
    if (strcmp(norm, "/") == 0) return fs->root;
    const char *cursor = norm;
    char comp[VFS_NAME_MAX];
    ramfs_node_t *cur = fs->root;
    char cur_path[VFS_PATH_MAX];
    strcpy(cur_path, "/");
    while (path_next_component(&cursor, comp, sizeof(comp))) {
        ramfs_node_t *next = find_child(cur, comp);
        if (!next) return 0;
        bool has_more = *cursor != 0;
        if (node_type(next) == VFS_NODE_SYMLINK && (follow_final || has_more)) {
            char target[VFS_PATH_MAX];
            usize tlen = next->obj->size;
            if (tlen >= sizeof(target)) return 0;
            memcpy(target, next->obj->data, tlen);
            target[tlen] = 0;
            char combined[VFS_PATH_MAX];
            if (!compose_symlink_path(cur_path, target, cursor, combined, sizeof(combined))) return 0;
            return resolve_inner(fs, combined, follow_final, depth + 1u);
        }
        cur = next;
        if (strcmp(cur_path, "/") != 0 && !str_append(cur_path, sizeof(cur_path), "/")) return 0;
        if (!str_append(cur_path, sizeof(cur_path), comp)) return 0;
    }
    return cur;
}

static ramfs_node_t *resolve(ramfs_t *fs, const char *path) { return resolve_inner(fs, path, true, 0); }
static ramfs_node_t *resolve_nofollow(ramfs_t *fs, const char *path) { return resolve_inner(fs, path, false, 0); }

static vfs_status_t resolve_parent(ramfs_t *fs, const char *path, ramfs_node_t **parent_out, char *leaf, usize leaf_size) {
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    if (strcmp(norm, "/") == 0) return VFS_ERR_INVAL;
    const char *base = path_basename(norm);
    if (!*base || strlen(base) >= leaf_size) return VFS_ERR_INVAL;
    strcpy(leaf, base);
    char parent_path[VFS_PATH_MAX];
    usize parent_len = (usize)(base - norm);
    if (parent_len == 0) parent_len = 1;
    if (parent_len >= sizeof(parent_path)) return VFS_ERR_INVAL;
    memcpy(parent_path, norm, parent_len);
    if (parent_len > 1 && parent_path[parent_len - 1u] == '/') --parent_len;
    parent_path[parent_len] = 0;
    ramfs_node_t *parent = resolve(fs, parent_path);
    if (!parent) return VFS_ERR_NOENT;
    if (node_type(parent) != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    *parent_out = parent;
    return VFS_OK;
}

ramfs_t *ramfs_create(const char *label) {
    ramfs_t *fs = (ramfs_t *)kcalloc(1, sizeof(*fs));
    if (!fs) return 0;
    strncpy(fs->label, label ? label : "ramfs", sizeof(fs->label) - 1u);
    fs->next_inode = 1;
    fs->root = dentry_create(fs, "", VFS_NODE_DIR);
    if (!fs->root) { kfree(fs); return 0; }
    return fs;
}

void ramfs_destroy(ramfs_t *fs) {
    if (!fs) return;
    free_dentry(fs->root);
    kfree(fs);
}

vfs_status_t ramfs_add_dir(ramfs_t *fs, const char *path) {
    if (!fs || !path) return VFS_ERR_INVAL;
    if (strcmp(path, "/") == 0) return VFS_OK;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    if (find_child(parent, leaf)) return VFS_ERR_EXIST;
    ramfs_node_t *n = dentry_create(fs, leaf, VFS_NODE_DIR);
    if (!n) return VFS_ERR_NOMEM;
    attach_child(parent, n);
    ++parent->obj->nlink;
    return VFS_OK;
}

vfs_status_t ramfs_add_file(ramfs_t *fs, const char *path, const void *data, usize size) {
    if (!fs || !path || (size && !data)) return VFS_ERR_INVAL;
    if (size > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    ramfs_node_t *n = find_child(parent, leaf);
    if (!n) {
        n = dentry_create(fs, leaf, VFS_NODE_FILE);
        if (!n) return VFS_ERR_NOMEM;
        attach_child(parent, n);
    } else if (node_type(n) != VFS_NODE_FILE) return VFS_ERR_ISDIR;
    u8 *copy = 0;
    if (size) {
        copy = (u8 *)kmalloc(size);
        if (!copy) return VFS_ERR_NOMEM;
        memcpy(copy, data, size);
    }
    kfree(n->obj->data);
    n->obj->data = copy;
    n->obj->size = size;
    n->obj->capacity = size;
    return VFS_OK;
}

static vfs_status_t stat_node(vfs_mount_t *mnt, ramfs_node_t *n, vfs_stat_t *out) {
    if (!mnt || !n || !n->obj || !out) return VFS_ERR_INVAL;
    memset(out, 0, sizeof(*out));
    out->type = n->obj->type;
    out->size = n->obj->size;
    out->mode = n->obj->mode;
    out->inode = n->obj->inode;
    out->fs_id = mnt->fs_id;
    out->nlink = n->obj->nlink;
    return VFS_OK;
}

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ramfs_node_t *n = resolve((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    return stat_node(mnt, n, out);
}

static vfs_status_t op_lstat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ramfs_node_t *n = resolve_nofollow((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    return stat_node(mnt, n, out);
}

static vfs_status_t op_stat_inode(vfs_mount_t *mnt, u32 ino, vfs_stat_t *out) {
    ramfs_node_t *n = node_find_inode(((ramfs_t *)mnt->ctx)->root, ino);
    if (!n) return VFS_ERR_NOENT;
    return stat_node(mnt, n, out);
}

static vfs_status_t read_node(ramfs_node_t *n, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    if (offset >= (u64)n->obj->size) return VFS_OK;
    usize off = (usize)offset;
    usize take = n->obj->size - off;
    if (take > size) take = size;
    memcpy(buffer, n->obj->data + off, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    return read_node(resolve((ramfs_t *)mnt->ctx, path), offset, buffer, size, read_out);
}

static vfs_status_t op_read_inode(vfs_mount_t *mnt, u32 ino, u64 offset, void *buffer, usize size, usize *read_out) {
    return read_node(node_find_inode(((ramfs_t *)mnt->ctx)->root, ino), offset, buffer, size, read_out);
}

static vfs_status_t write_node(ramfs_node_t *n, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (offset > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (size > RAMFS_MAX_FILE_SIZE - (usize)offset) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    usize off = (usize)offset;
    usize end = off + size;
    if (end < off || end > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (end > n->obj->capacity) {
        usize cap = n->obj->capacity ? n->obj->capacity : 64u;
        while (cap < end) {
            if (cap >= RAMFS_MAX_FILE_SIZE) return VFS_ERR_NOMEM;
            if (cap > ((usize)-1) / 2u) return VFS_ERR_NOMEM;
            cap *= 2u;
            if (cap > RAMFS_MAX_FILE_SIZE) cap = RAMFS_MAX_FILE_SIZE;
        }
        u8 *new_data = (u8 *)kmalloc(cap);
        if (!new_data) return VFS_ERR_NOMEM;
        if (n->obj->data) memcpy(new_data, n->obj->data, n->obj->size);
        if (cap > n->obj->size) memset(new_data + n->obj->size, 0, cap - n->obj->size);
        kfree(n->obj->data);
        n->obj->data = new_data;
        n->obj->capacity = cap;
    }
    if (size) memcpy(n->obj->data + off, buffer, size);
    if (end > n->obj->size) n->obj->size = end;
    if (written_out) *written_out = size;
    return VFS_OK;
}

static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    return write_node(resolve((ramfs_t *)mnt->ctx, path), offset, buffer, size, written_out);
}

static vfs_status_t op_write_inode(vfs_mount_t *mnt, u32 ino, u64 offset, const void *buffer, usize size, usize *written_out) {
    return write_node(node_find_inode(((ramfs_t *)mnt->ctx)->root, ino), offset, buffer, size, written_out);
}

static vfs_status_t list_node(ramfs_node_t *n, vfs_dir_iter_fn fn, void *ctx) {
    if (!fn) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    for (ramfs_node_t *c = n->obj->children; c; c = c->next) {
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, c->name, sizeof(de.name) - 1u);
        de.type = c->obj->type;
        de.size = c->obj->size;
        de.inode = c->obj->inode;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    return list_node(resolve((ramfs_t *)mnt->ctx, path), fn, ctx);
}

static vfs_status_t op_list_inode(vfs_mount_t *mnt, u32 ino, vfs_dir_iter_fn fn, void *ctx) {
    return list_node(node_find_inode(((ramfs_t *)mnt->ctx)->root, ino), fn, ctx);
}

static vfs_status_t op_mkdir(vfs_mount_t *mnt, const char *path) { return ramfs_add_dir((ramfs_t *)mnt->ctx, path); }
static vfs_status_t op_create(vfs_mount_t *mnt, const char *path, const void *data, usize size) { return ramfs_add_file((ramfs_t *)mnt->ctx, path, data, size); }

static vfs_status_t op_readlink(vfs_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (!buffer && size) return VFS_ERR_INVAL;
    ramfs_node_t *n = resolve_nofollow((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    if (node_type(n) != VFS_NODE_SYMLINK) return VFS_ERR_INVAL;
    usize take = n->obj->size;
    if (take > size) take = size;
    if (take) memcpy(buffer, n->obj->data, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_symlink(vfs_mount_t *mnt, const char *target, const char *link_path) {
    if (!target || !target[0]) return VFS_ERR_INVAL;
    usize len = strnlen(target, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX) return VFS_ERR_INVAL;
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, link_path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    if (find_child(parent, leaf)) return VFS_ERR_EXIST;
    ramfs_node_t *n = dentry_create(fs, leaf, VFS_NODE_SYMLINK);
    if (!n) return VFS_ERR_NOMEM;
    n->obj->data = (u8 *)kmalloc(len);
    if (!n->obj->data) { free_dentry(n); return VFS_ERR_NOMEM; }
    memcpy(n->obj->data, target, len);
    n->obj->size = len;
    n->obj->capacity = len;
    attach_child(parent, n);
    return VFS_OK;
}

static vfs_status_t op_link(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    ramfs_node_t *src = resolve_nofollow(fs, old_path);
    if (!src) return VFS_ERR_NOENT;
    if (node_type(src) == VFS_NODE_DIR) return VFS_ERR_PERM;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, new_path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    if (find_child(parent, leaf)) return VFS_ERR_EXIST;
    ramfs_node_t *alias = dentry_alias_create(leaf, src->obj);
    if (!alias) return VFS_ERR_NOMEM;
    attach_child(parent, alias);
    return VFS_OK;
}

static vfs_status_t op_unlink(vfs_mount_t *mnt, const char *path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    ramfs_node_t *prev = 0;
    for (ramfs_node_t *c = parent->obj->children; c; c = c->next) {
        if (strcmp(c->name, leaf) == 0) {
            if (node_type(c) == VFS_NODE_DIR && c->obj->children) return VFS_ERR_NOTEMPTY;
            if (prev) prev->next = c->next;
            else parent->obj->children = c->next;
            if (node_type(c) == VFS_NODE_DIR && parent->obj->nlink > 0) --parent->obj->nlink;
            c->next = 0;
            free_dentry(c);
            return VFS_OK;
        }
        prev = c;
    }
    return VFS_ERR_NOENT;
}

static vfs_status_t truncate_node(ramfs_node_t *n, u64 size64) {
    if (size64 > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (!n || !n->obj) return VFS_ERR_NOENT;
    if (n->obj->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (n->obj->type == VFS_NODE_SYMLINK) return VFS_ERR_UNSUPPORTED;
    usize size = (usize)size64;
    if (size > n->obj->capacity) {
        usize cap = n->obj->capacity ? n->obj->capacity : 64u;
        while (cap < size) {
            if (cap >= RAMFS_MAX_FILE_SIZE) return VFS_ERR_NOMEM;
            if (cap > ((usize)-1) / 2u) return VFS_ERR_NOMEM;
            cap *= 2u;
            if (cap > RAMFS_MAX_FILE_SIZE) cap = RAMFS_MAX_FILE_SIZE;
        }
        u8 *new_data = (u8 *)kmalloc(cap);
        if (!new_data) return VFS_ERR_NOMEM;
        if (n->obj->data) memcpy(new_data, n->obj->data, n->obj->size);
        if (cap > n->obj->size) memset(new_data + n->obj->size, 0, cap - n->obj->size);
        kfree(n->obj->data);
        n->obj->data = new_data;
        n->obj->capacity = cap;
    } else if (size < n->obj->size && n->obj->data) {
        memset(n->obj->data + size, 0, n->obj->size - size);
    }
    n->obj->size = size;
    return VFS_OK;
}

static vfs_status_t op_truncate(vfs_mount_t *mnt, const char *path, u64 size64) {
    return truncate_node(resolve((ramfs_t *)mnt->ctx, path), size64);
}

static vfs_status_t op_truncate_inode(vfs_mount_t *mnt, u32 ino, u64 size64) {
    return truncate_node(node_find_inode(((ramfs_t *)mnt->ctx)->root, ino), size64);
}

static vfs_status_t op_sync_inode(vfs_mount_t *mnt, u32 ino, bool data_only) {
    (void)data_only;
    return node_find_inode(((ramfs_t *)mnt->ctx)->root, ino) ? VFS_OK : VFS_ERR_NOENT;
}

static vfs_status_t op_sync(vfs_mount_t *mnt) {
    (void)mnt;
    return VFS_OK;
}

static bool node_is_ancestor(ramfs_node_t *ancestor, ramfs_node_t *n) {
    for (ramfs_node_t *p = n; p; p = p->parent) if (p == ancestor) return true;
    return false;
}

static vfs_status_t op_rename(vfs_mount_t *mnt, const char *old_path, const char *new_path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    ramfs_node_t *old_parent = 0;
    ramfs_node_t *new_parent = 0;
    char old_leaf[VFS_NAME_MAX];
    char new_leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, old_path, &old_parent, old_leaf, sizeof(old_leaf));
    if (st != VFS_OK) return st;
    st = resolve_parent(fs, new_path, &new_parent, new_leaf, sizeof(new_leaf));
    if (st != VFS_OK) return st;
    ramfs_node_t *prev = 0;
    ramfs_node_t *n = old_parent->obj->children;
    while (n && strcmp(n->name, old_leaf) != 0) { prev = n; n = n->next; }
    if (!n) return VFS_ERR_NOENT;
    if (node_type(n) == VFS_NODE_DIR && node_is_ancestor(n, new_parent)) return VFS_ERR_INVAL;
    ramfs_node_t *dst_prev = 0;
    ramfs_node_t *dst = new_parent->obj->children;
    while (dst && strcmp(dst->name, new_leaf) != 0) { dst_prev = dst; dst = dst->next; }
    if (dst == n) return VFS_OK;
    if (dst) {
        if (node_type(dst) != node_type(n)) return VFS_ERR_EXIST;
        if (node_type(dst) == VFS_NODE_DIR && dst->obj->children) return VFS_ERR_NOTEMPTY;
        if (dst_prev) dst_prev->next = dst->next;
        else new_parent->obj->children = dst->next;
        if (node_type(dst) == VFS_NODE_DIR && new_parent->obj->nlink > 0) --new_parent->obj->nlink;
        dst->next = 0;
        free_dentry(dst);
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
    return VFS_OK;
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
    .rename = op_rename,
    .sync = op_sync,
    .sync_inode = op_sync_inode,
};

const vfs_ops_t *ramfs_ops(void) { return &ops; }

vfs_status_t ramfs_mount_boot(void) {
    ramfs_t *fs = ramfs_create("bootram");
    if (!fs) return VFS_ERR_NOMEM;
    vfs_status_t st = ramfs_add_dir(fs, "/etc");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/bin");
    if (st == VFS_OK) st = ramfs_add_dir(fs, "/tmp");
    static const char motd[] = AURORA_VERSION_FULL " boot ramfs\nKernel-owned VFS root is online.\n";
    static const char version[] = AURORA_UNAME_TEXT "\n";
    static const char profile[] = "PATH=/bin\nSHELL=/sys/cli\n";
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/motd", motd, sizeof(motd) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/version", version, sizeof(version) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/etc/profile", profile, sizeof(profile) - 1u);
    if (st == VFS_OK) st = ramfs_add_file(fs, "/bin/readme", "kernel cli command set lives in core/shell.c\n", 43u);
    if (st != VFS_OK) { ramfs_destroy(fs); return st; }
    return vfs_mount("/", "ramfs", ramfs_ops(), fs, true);
}
