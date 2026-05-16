#include <rabbitbone/devfs.h>
#include <rabbitbone/abi.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/tty.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/net.h>

typedef enum dev_kind { DEV_NULL, DEV_ZERO, DEV_PRNG, DEV_KMSG, DEV_TTY } dev_kind_t;

typedef struct devfs_entry {
    const char *name;
    dev_kind_t kind;
    u32 inode;
    u32 mode;
} devfs_entry_t;

typedef struct devfs_state {
    u64 prng_state;
    spinlock_t prng_lock;
} devfs_state_t;

static const devfs_entry_t entries[] = {
    { "null", DEV_NULL, 1, 0666u },
    { "zero", DEV_ZERO, 2, 0444u },
    { "prng", DEV_PRNG, 3, 0444u },
    { "urandom_insecure", DEV_PRNG, 4, 0444u },
    { "kmsg", DEV_KMSG, 5, 0600u },
    { "tty", DEV_TTY, 6, 0666u },
};

static devfs_entry_t const *find_entry(const char *path) {
    if (!path) return 0;
    while (*path == '/') ++path;
    if (!*path) return 0;
    for (usize i = 0; i < RABBITBONE_ARRAY_LEN(entries); ++i) {
        if (strcmp(entries[i].name, path) == 0) return &entries[i];
    }
    return 0;
}

static bool parse_net_path(const char *path, usize *index_out) {
    if (index_out) *index_out = 0;
    if (!path) return false;
    while (*path == '/') ++path;
    if (path[0] != 'n' || path[1] != 'e' || path[2] != 't') return false;
    const char *p = path + 3;
    if (*p < '0' || *p > '9') return false;
    usize value = 0;
    while (*p >= '0' && *p <= '9') {
        usize next = value * 10u + (usize)(*p - '0');
        if (next < value) return false;
        value = next;
        ++p;
    }
    if (*p != 0) return false;
    if (value >= netdev_count()) return false;
    if (index_out) *index_out = value;
    return true;
}

static net_device_t *find_netdev_path(const char *path) {
    usize index = 0;
    if (!parse_net_path(path, &index)) return 0;
    return netdev_get(index);
}

static bool path_from_inode(u32 inode, char *out, usize out_len) {
    if (!out || out_len == 0) return false;
    out[0] = 0;
    for (usize i = 0; i < RABBITBONE_ARRAY_LEN(entries); ++i) {
        if (entries[i].inode == inode) {
            strlcpy(out, entries[i].name, out_len);
            return true;
        }
    }
    if (inode >= 100u) {
        usize index = (usize)(inode - 100u);
        if (index < netdev_count()) {
            ksnprintf(out, out_len, "net%llu", (unsigned long long)index);
            return true;
        }
    }
    return false;
}

static u64 xorshift(devfs_state_t *s) {
    u64 x = s->prng_state;
    if (!x) x = 0x9e3779b97f4a7c15ull ^ pit_ticks();
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s->prng_state = x;
    return x;
}

static vfs_status_t op_stat(vfs_mount_t *mnt, const char *path, vfs_stat_t *out) {
    (void)mnt;
    if (strcmp(path, "/") == 0) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DIR;
        out->mode = 0555; out->uid = RABBITBONE_UID_ROOT; out->gid = RABBITBONE_GID_ROOT;
        return VFS_OK;
    }
    const devfs_entry_t *e = find_entry(path);
    if (e) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DEV;
        out->mode = e->mode; out->uid = RABBITBONE_UID_ROOT; out->gid = RABBITBONE_GID_ROOT;
        out->inode = e->inode;
        out->fs_id = mnt->fs_id;
        return VFS_OK;
    }
    usize net_index = 0;
    if (parse_net_path(path, &net_index)) {
        memset(out, 0, sizeof(*out));
        out->type = VFS_NODE_DEV;
        out->mode = 0660u; out->uid = RABBITBONE_UID_ROOT; out->gid = RABBITBONE_GID_USERS;
        out->inode = 100u + (u32)net_index;
        out->fs_id = mnt->fs_id;
        return VFS_OK;
    }
    return VFS_ERR_NOENT;
}

static vfs_status_t op_read(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out) {
    (void)offset;
    if (read_out) *read_out = 0;
    if (size && !buffer) return VFS_ERR_INVAL;
    const devfs_entry_t *e = find_entry(path);
    if (!e) {
        net_device_t *ndev = find_netdev_path(path);
        if (!ndev) return VFS_ERR_NOENT;
        usize got = 0;
        net_status_t ns = netdev_read_frame(ndev, buffer, size, &got, 0);
        if (read_out) *read_out = got;
        return ns == NET_OK ? VFS_OK : VFS_ERR_IO;
    }
    u8 *out = (u8 *)buffer;
    switch (e->kind) {
        case DEV_NULL:
            return VFS_OK;
        case DEV_ZERO:
            if (size) memset(buffer, 0, size);
            if (read_out) *read_out = size;
            return VFS_OK;
        case DEV_PRNG: {
            devfs_state_t *s = (devfs_state_t *)mnt->ctx;
            usize done = 0;
            while (done < size) {
                usize chunk = size - done;
                if (chunk > 256u) chunk = 256u;
                u64 flags = spin_lock_irqsave(&s->prng_lock);
                for (usize i = 0; i < chunk; ++i) {
                    if (((done + i) & 7u) == 0) xorshift(s);
                    out[done + i] = (u8)(s->prng_state >> (((done + i) & 7u) * 8u));
                }
                spin_unlock_irqrestore(&s->prng_lock, flags);
                done += chunk;
            }
            if (read_out) *read_out = size;
            return VFS_OK;
        }
        case DEV_TTY: {
            usize n = 0;
            while (n < size) {
                char c = 0;
                if (!tty_read_char(&c)) break;
                out[n++] = (u8)c;
                if (c == '\n') break;
            }
            if (read_out) *read_out = n;
            return VFS_OK;
        }
        case DEV_KMSG: {
            static const char msg[] = "kmsg is exposed through the logs shell command\n";
            usize len = sizeof(msg) - 1u;
            if (offset >= len) return VFS_OK;
            usize take = len - (usize)offset;
            if (take > size) take = size;
            memcpy(buffer, msg + offset, take);
            if (read_out) *read_out = take;
            return VFS_OK;
        }
    }
    return VFS_ERR_INVAL;
}

static vfs_status_t op_write(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out) {
    (void)mnt; (void)offset;
    if (written_out) *written_out = 0;
    const devfs_entry_t *e = find_entry(path);
    if (!e) {
        net_device_t *ndev = find_netdev_path(path);
        if (!ndev) return VFS_ERR_NOENT;
        usize wrote = 0;
        net_status_t ns = netdev_write_frame(ndev, buffer, size, &wrote);
        if (written_out) *written_out = wrote;
        if (ns == NET_OK) return VFS_OK;
        if (ns == NET_ERR_BUSY) return VFS_ERR_BUSY;
        if (ns == NET_ERR_RANGE || ns == NET_ERR_INVAL) return VFS_ERR_INVAL;
        return VFS_ERR_IO;
    }
    if (e->kind == DEV_NULL) {
        if (written_out) *written_out = size;
        return VFS_OK;
    }
    if (e->kind == DEV_TTY) {
        if (size && !buffer) return VFS_ERR_INVAL;
        if (size) console_write_n((const char *)buffer, size);
        if (written_out) *written_out = size;
        return VFS_OK;
    }
    if (e->kind == DEV_KMSG) {
        if (size && !buffer) return VFS_ERR_INVAL;
        enum { DEV_KMSG_MAX = 256 };
        char *msg = (char *)kmalloc(DEV_KMSG_MAX);
        if (!msg) return VFS_ERR_NOMEM;
        usize n = size < DEV_KMSG_MAX - 1u ? size : DEV_KMSG_MAX - 1u;
        const unsigned char *src = (const unsigned char *)buffer;
        for (usize i = 0; i < n; ++i) {
            unsigned char c = src[i];
            msg[i] = (c >= 0x20u && c != 0x7fu) || c == '\t' ? (char)c : '?';
        }
        msg[n] = 0;
        KLOG(LOG_INFO, "kmsg", "%s", msg);
        kfree(msg);
        if (written_out) *written_out = size;
        return VFS_OK;
    }
    return VFS_ERR_PERM;
}

static vfs_status_t op_list(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx) {
    (void)mnt;
    if (!path || !fn) return VFS_ERR_INVAL;
    if (strcmp(path, "/") != 0) return VFS_ERR_NOTDIR;
    for (usize i = 0; i < RABBITBONE_ARRAY_LEN(entries); ++i) {
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        strlcpy(de.name, entries[i].name, sizeof(de.name));
        de.type = VFS_NODE_DEV;
        de.inode = entries[i].inode;
        if (!fn(&de, ctx)) return VFS_OK;
    }
    for (usize i = 0; i < netdev_count(); ++i) {
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        ksnprintf(de.name, sizeof(de.name), "net%llu", (unsigned long long)i);
        de.type = VFS_NODE_DEV;
        de.inode = 100u + (u32)i;
        if (!fn(&de, ctx)) break;
    }
    return VFS_OK;
}

static vfs_status_t op_stat_inode(vfs_mount_t *mnt, u32 inode, vfs_stat_t *out) {
    char path[32];
    if (!path_from_inode(inode, path, sizeof(path))) return VFS_ERR_NOENT;
    return op_stat(mnt, path, out);
}

static vfs_status_t op_read_inode(vfs_mount_t *mnt, u32 inode, u64 offset, void *buffer, usize size, usize *read_out) {
    char path[32];
    if (!path_from_inode(inode, path, sizeof(path))) {
        if (read_out) *read_out = 0;
        return VFS_ERR_NOENT;
    }
    return op_read(mnt, path, offset, buffer, size, read_out);
}

static vfs_status_t op_write_inode(vfs_mount_t *mnt, u32 inode, u64 offset, const void *buffer, usize size, usize *written_out) {
    char path[32];
    if (!path_from_inode(inode, path, sizeof(path))) {
        if (written_out) *written_out = 0;
        return VFS_ERR_NOENT;
    }
    return op_write(mnt, path, offset, buffer, size, written_out);
}

static const vfs_ops_t ops = {
    .stat = op_stat,
    .read = op_read,
    .write = op_write,
    .stat_inode = op_stat_inode,
    .read_inode = op_read_inode,
    .write_inode = op_write_inode,
    .list = op_list,
};

vfs_status_t devfs_mount(void) {
    devfs_state_t *state = (devfs_state_t *)kcalloc(1, sizeof(*state));
    if (!state) return VFS_ERR_NOMEM;
    spinlock_init(&state->prng_lock);
    state->prng_state = 0xA6E5A6E5D00D1234ull ^ pit_ticks();
    KLOG(LOG_WARN, "devfs", "/dev/prng and /dev/urandom_insecure are deterministic non-cryptographic demo PRNG devices; do not use for secrets");
    vfs_status_t st = vfs_mount("/dev", "devfs", &ops, state, true);
    if (st != VFS_OK) kfree(state);
    return st;
}
