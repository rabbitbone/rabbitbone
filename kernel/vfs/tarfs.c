#include <aurora/tarfs.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>

typedef struct tarfs_entry {
    char path[VFS_PATH_MAX];
    const u8 *data;
    usize size;
    vfs_node_type_t type;
    u32 inode;
} tarfs_entry_t;

struct tarfs {
    const u8 *image;
    usize size;
    tarfs_entry_t *entries;
    usize count;
};

typedef struct AURORA_PACKED tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_header_t;

static bool tar_octal(const char *s, usize n, usize *out) {
    if (!s || !out) return false;
    usize v = 0;
    bool saw_digit = false;
    for (usize i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0 || c == ' ') {
            for (usize j = i + 1u; j < n; ++j) {
                unsigned char t = (unsigned char)s[j];
                if (t != 0 && t != ' ') return false;
            }
            *out = v;
            return true;
        }
        if (c < '0' || c > '7') return false;
        saw_digit = true;
        if (v > (((usize)-1) - (usize)(c - '0')) / 8u) return false;
        v = v * 8u + (usize)(c - '0');
    }
    if (!saw_digit) return false;
    *out = v;
    return true;
}

static bool tar_type_supported(char typeflag) {
    return typeflag == 0 || typeflag == '0' || typeflag == '5';
}

static bool tar_name_has_parent_ref(const char *s, usize n) {
    usize i = 0;
    while (i < n && s[i]) {
        while (i < n && s[i] == '/') ++i;
        usize start = i;
        while (i < n && s[i] && s[i] != '/') ++i;
        usize len = i - start;
        if (len == 2u && s[start] == '.' && s[start + 1u] == '.') return true;
    }
    return false;
}

static bool block_empty(const u8 *p) {
    for (usize i = 0; i < 512u; ++i) if (p[i]) return false;
    return true;
}

static bool checksum_ok(const tar_header_t *h) {
    const u8 *p = (const u8 *)h;
    usize sum = 0;
    for (usize i = 0; i < 512u; ++i) sum += p[i];
    for (usize i = 148; i < 156; ++i) sum = sum - p[i] + ' ';
    usize stored = 0;
    if (!tar_octal(h->chksum, sizeof(h->chksum), &stored)) return false;
    return stored != 0 && stored == sum;
}

static bool header_path(const tar_header_t *h, char *out, usize out_size) {
    if (!tar_type_supported(h->typeflag)) return false;
    char tmp[VFS_PATH_MAX];
    usize pos = 0;
    tmp[pos++] = '/';
    if (h->prefix[0]) {
        usize pl = strnlen(h->prefix, sizeof(h->prefix));
        if (tar_name_has_parent_ref(h->prefix, pl)) return false;
        if (pos + pl + 1u >= sizeof(tmp)) return false;
        memcpy(tmp + pos, h->prefix, pl);
        pos += pl;
        tmp[pos++] = '/';
    }
    usize nl = strnlen(h->name, sizeof(h->name));
    if (nl == 0 || tar_name_has_parent_ref(h->name, nl) || pos + nl + 1u >= sizeof(tmp)) return false;
    memcpy(tmp + pos, h->name, nl);
    pos += nl;
    tmp[pos] = 0;
    return path_normalize(tmp, out, out_size);
}

tarfs_t *tarfs_open(const void *image, usize size) {
    if (!image || size < 1024u) return 0;
    tarfs_t *fs = (tarfs_t *)kcalloc(1, sizeof(*fs));
    if (!fs) return 0;
    fs->image = (const u8 *)image;
    fs->size = size;
    usize count = 0;
    for (usize off = 0; off + 512u <= size;) {
        const tar_header_t *h = (const tar_header_t *)(fs->image + off);
        if (block_empty((const u8 *)h)) break;
        if (!checksum_ok(h) || !tar_type_supported(h->typeflag)) { kfree(fs); return 0; }
        usize fsize = 0;
        if (!tar_octal(h->size, sizeof(h->size), &fsize)) { kfree(fs); return 0; }
        usize aligned = AURORA_ALIGN_UP(fsize, 512u);
        if (aligned < fsize || off > size - 512u || aligned > size - off - 512u) { kfree(fs); return 0; }
        ++count;
        off += 512u + aligned;
    }
    fs->entries = (tarfs_entry_t *)kcalloc(count ? count : 1u, sizeof(tarfs_entry_t));
    if (!fs->entries) { kfree(fs); return 0; }
    fs->count = 0;
    u32 inode = 2;
    for (usize off = 0; off + 512u <= size;) {
        const tar_header_t *h = (const tar_header_t *)(fs->image + off);
        if (block_empty((const u8 *)h)) break;
        if (!checksum_ok(h) || !tar_type_supported(h->typeflag)) { kfree(fs->entries); kfree(fs); return 0; }
        usize fsize = 0;
        if (!tar_octal(h->size, sizeof(h->size), &fsize)) { kfree(fs->entries); kfree(fs); return 0; }
        usize aligned = AURORA_ALIGN_UP(fsize, 512u);
        if (aligned < fsize || off > size - 512u || aligned > size - off - 512u) { kfree(fs->entries); kfree(fs); return 0; }
        tarfs_entry_t *e = &fs->entries[fs->count++];
        if (!header_path(h, e->path, sizeof(e->path))) { kfree(fs->entries); kfree(fs); return 0; }
        e->data = fs->image + off + 512u;
        e->size = fsize;
        e->type = h->typeflag == '5' ? VFS_NODE_DIR : VFS_NODE_FILE;
        e->inode = inode++;
        off += 512u + aligned;
    }
    return fs;
}

static tarfs_entry_t *find_exact(tarfs_t *fs, const char *path) {
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return 0;
    for (usize i = 0; i < fs->count; ++i) if (strcmp(fs->entries[i].path, norm) == 0) return &fs->entries[i];
    return 0;
}

static bool is_direct_child(const char *dir, const char *path, const char **name_out) {
    if (strcmp(dir, "/") == 0) {
        if (path[0] != '/' || path[1] == 0) return false;
        const char *name = path + 1;
        if (strchr(name, '/')) return false;
        *name_out = name;
        return true;
    }
    usize dl = strlen(dir);
    if (strncmp(dir, path, dl) != 0 || path[dl] != '/') return false;
    const char *name = path + dl + 1u;
    if (!*name || strchr(name, '/')) return false;
    *name_out = name;
    return true;
}

static bool has_children(tarfs_t *fs, const char *dir) {
    for (usize i = 0; i < fs->count; ++i) {
        const char *name = 0;
        if (is_direct_child(dir, fs->entries[i].path, &name)) return true;
    }
    return false;
}

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    tarfs_t *fs = (tarfs_t *)mnt->ctx;
    if (strcmp(path, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->fs_id = mnt->fs_id;
        return VFS_OK;
    }
    tarfs_entry_t *e = find_exact(fs, path);
    if (!e) {
        char norm[VFS_PATH_MAX];
        if (path_normalize(path, norm, sizeof(norm)) && has_children(fs, norm)) {
            memset(out, 0, sizeof(*out));
            out->type = VFS_NODE_DIR;
            out->fs_id = mnt->fs_id;
            return VFS_OK;
        }
        return VFS_ERR_NOENT;
    }
    memset(out, 0, sizeof(*out));
    out->type = e->type;
    out->size = e->size;
    out->inode = e->inode;
    out->mode = e->type == VFS_NODE_DIR ? 0555u : 0444u;
    out->fs_id = mnt->fs_id;
    return VFS_OK;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    if (read_out) *read_out = 0;
    tarfs_entry_t *e = find_exact((tarfs_t *)mnt->ctx, path);
    if (!e) return VFS_ERR_NOENT;
    if (e->type != VFS_NODE_FILE) return VFS_ERR_ISDIR;
    if (offset >= e->size) return VFS_OK;
    usize take = e->size - (usize)offset;
    if (take > size) take = size;
    memcpy(buffer, e->data + offset, take);
    if (read_out) *read_out = take;
    return VFS_OK;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    tarfs_t *fs = (tarfs_t *)mnt->ctx;
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return VFS_ERR_INVAL;
    vfs_stat_t st;
    vfs_status_t s = op_stat(mnt, norm, &st);
    if (s != VFS_OK) return s;
    if (st.type != VFS_NODE_DIR) return VFS_ERR_NOTDIR;
    for (usize i = 0; i < fs->count; ++i) {
        const char *name = 0;
        if (!is_direct_child(norm, fs->entries[i].path, &name)) continue;
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strncpy(de.name, name, sizeof(de.name) - 1u);
        de.type = fs->entries[i].type;
        de.size = fs->entries[i].size;
        de.inode = fs->entries[i].inode;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .read = op_read,
    .list = op_list,
};

void tarfs_destroy(tarfs_t *fs) {
    if (!fs) return;
    kfree(fs->entries);
    kfree(fs);
}

const vfs_ops_t *tarfs_ops(void) { return &ops; }
