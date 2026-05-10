#include <aurora/ramfs.h>
#include <aurora/version.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>
#include <aurora/log.h>

#define RAMFS_MAX_FILE_SIZE (16ull * 1024ull * 1024ull)

typedef struct ramfs_node {
    char name[VFS_NAME_MAX];
    vfs_node_type_t type;
    u32 inode;
    u32 mode;
    u8 *data;
    usize size;
    usize capacity;
    struct ramfs_node *parent;
    struct ramfs_node *children;
    struct ramfs_node *next;
} ramfs_node_t;

struct ramfs {
    char label[VFS_NAME_MAX];
    ramfs_node_t *root;
    u32 next_inode;
};

static bool inode_exists_in(ramfs_node_t *n, u32 ino) {
    if (!n) return false;
    if (n->inode == ino) return true;
    for (ramfs_node_t *c = n->children; c; c = c->next) if (inode_exists_in(c, ino)) return true;
    return false;
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

static ramfs_node_t *node_create(ramfs_t *fs, const char *name, vfs_node_type_t type) {
    ramfs_node_t *n = (ramfs_node_t *)kcalloc(1, sizeof(*n));
    if (!n) return 0;
    strncpy(n->name, name ? name : "", sizeof(n->name) - 1u);
    n->type = type;
    n->inode = ramfs_alloc_inode(fs);
    if (n->inode == 0) { kfree(n); return 0; }
    n->mode = type == VFS_NODE_DIR ? 0755u : 0644u;
    return n;
}

static ramfs_node_t *find_child(ramfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_NODE_DIR) return 0;
    for (ramfs_node_t *c = dir->children; c; c = c->next) if (strcmp(c->name, name) == 0) return c;
    return 0;
}

static void attach_child(ramfs_node_t *dir, ramfs_node_t *child) {
    child->parent = dir;
    child->next = dir->children;
    dir->children = child;
}

static ramfs_node_t *resolve(ramfs_t *fs, const char *path) {
    if (!fs || !path) return 0;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return 0;
    if (strcmp(norm, "/") == 0) return fs->root;
    const char *cursor = norm;
    char comp[VFS_NAME_MAX];
    ramfs_node_t *cur = fs->root;
    while (path_next_component(&cursor, comp, sizeof(comp))) {
        cur = find_child(cur, comp);
        if (!cur) return 0;
    }
    return cur;
}

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
    if (parent->type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    *parent_out = parent;
    return VFS_OK;
}

ramfs_t *ramfs_create(const char *label) {
    ramfs_t *fs = (ramfs_t *)kcalloc(1, sizeof(*fs));
    if (!fs) return 0;
    strncpy(fs->label, label ? label : "ramfs", sizeof(fs->label) - 1u);
    fs->next_inode = 1;
    fs->root = node_create(fs, "", VFS_NODE_DIR);
    if (!fs->root) { kfree(fs); return 0; }
    return fs;
}

static void free_node(ramfs_node_t *n) {
    if (!n) return;
    ramfs_node_t *c = n->children;
    while (c) {
        ramfs_node_t *next = c->next;
        free_node(c);
        c = next;
    }
    kfree(n->data);
    kfree(n);
}

void ramfs_destroy(ramfs_t *fs) {
    if (!fs) return;
    free_node(fs->root);
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
    ramfs_node_t *n = node_create(fs, leaf, VFS_NODE_DIR);
    if (!n) return VFS_ERR_NOMEM;
    attach_child(parent, n);
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
        n = node_create(fs, leaf, VFS_NODE_FILE);
        if (!n) return VFS_ERR_NOMEM;
        attach_child(parent, n);
    } else if (n->type != VFS_NODE_FILE) return VFS_ERR_ISDIR;
    u8 *copy = 0;
    if (size) {
        copy = (u8 *)kmalloc(size);
        if (!copy) return VFS_ERR_NOMEM;
        memcpy(copy, data, size);
    }
    kfree(n->data);
    n->data = copy;
    n->size = size;
    n->capacity = size;
    return VFS_OK;
}

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    ramfs_node_t *n = resolve((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    memset(out, 0, sizeof(*out));
    out->type = n->type;
    out->size = n->size;
    out->mode = n->mode;
    out->inode = n->inode;
    out->fs_id = mnt->fs_id;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    ramfs_node_t *n = resolve((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    if (n->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    if (offset >= (u64)n->size) return VFS_OK;
    usize off = (usize)offset;
    usize take = n->size - off;
    if (take > size) take = size;
    memcpy(buffer, n->data + off, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    if (written_out) *written_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    if (offset > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (size > RAMFS_MAX_FILE_SIZE - (usize)offset) return VFS_ERR_INVAL;
    ramfs_node_t *n = resolve((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    if (n->type == VFS_NODE_DIR) return VFS_ERR_ISDIR;
    usize off = (usize)offset;
    usize end = off + size;
    if (end < off || end > RAMFS_MAX_FILE_SIZE) return VFS_ERR_INVAL;
    if (end > n->capacity) {
        usize cap = n->capacity ? n->capacity : 64u;
        while (cap < end) {
            if (cap >= RAMFS_MAX_FILE_SIZE) return VFS_ERR_NOMEM;
            if (cap > ((usize)-1) / 2u) return VFS_ERR_NOMEM;
            cap *= 2u;
            if (cap > RAMFS_MAX_FILE_SIZE) cap = RAMFS_MAX_FILE_SIZE;
        }
        u8 *new_data = (u8 *)kmalloc(cap);
        if (!new_data) return VFS_ERR_NOMEM;
        if (n->data) memcpy(new_data, n->data, n->size);
        if (cap > n->size) memset(new_data + n->size, 0, cap - n->size);
        kfree(n->data);
        n->data = new_data;
        n->capacity = cap;
    }
    if (size) memcpy(n->data + off, buffer, size);
    if (end > n->size) n->size = end;
    if (written_out) *written_out = size;
    return VFS_OK;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    ramfs_node_t *n = resolve((ramfs_t *)mnt->ctx, path);
    if (!n) return VFS_ERR_NOENT;
    if (n->type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    for (ramfs_node_t *c = n->children; c; c = c->next) {
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, c->name, sizeof(de.name) - 1u);
        de.type = c->type;
        de.size = c->size;
        de.inode = c->inode;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static vfs_status_t op_mkdir(vfs_mount_t *mnt, const char *path) { return ramfs_add_dir((ramfs_t *)mnt->ctx, path); }
static vfs_status_t op_create(vfs_mount_t *mnt, const char *path, const void *data, usize size) { return ramfs_add_file((ramfs_t *)mnt->ctx, path, data, size); }

static vfs_status_t op_unlink(vfs_mount_t *mnt, const char *path) {
    ramfs_t *fs = (ramfs_t *)mnt->ctx;
    ramfs_node_t *parent = 0;
    char leaf[VFS_NAME_MAX];
    vfs_status_t st = resolve_parent(fs, path, &parent, leaf, sizeof(leaf));
    if (st != VFS_OK) return st;
    ramfs_node_t *prev = 0;
    for (ramfs_node_t *c = parent->children; c; c = c->next) {
        if (strcmp(c->name, leaf) == 0) {
            if (c->type == VFS_NODE_DIR && c->children) return VFS_ERR_NOTEMPTY;
            if (prev) prev->next = c->next;
            else parent->children = c->next;
            c->next = 0;
            free_node(c);
            return VFS_OK;
        }
        prev = c;
    }
    return VFS_ERR_NOENT;
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .read = op_read,
    .write = op_write,
    .list = op_list,
    .mkdir = op_mkdir,
    .create = op_create,
    .unlink = op_unlink,
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
