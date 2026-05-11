#ifndef AURORA_VFS_H
#define AURORA_VFS_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define VFS_NAME_MAX 64u
#define VFS_PATH_MAX 256u
#define VFS_MAX_MOUNTS 16u
#define VFS_MAX_OPEN 64u

typedef enum vfs_status {
    VFS_OK = 0,
    VFS_ERR_NOENT = -1,
    VFS_ERR_NOMEM = -2,
    VFS_ERR_INVAL = -3,
    VFS_ERR_IO = -4,
    VFS_ERR_NOTDIR = -5,
    VFS_ERR_ISDIR = -6,
    VFS_ERR_EXIST = -7,
    VFS_ERR_PERM = -8,
    VFS_ERR_NOSPC = -9,
    VFS_ERR_NOTEMPTY = -10,
    VFS_ERR_UNSUPPORTED = -11,
} vfs_status_t;

typedef enum vfs_node_type {
    VFS_NODE_FILE = 1,
    VFS_NODE_DIR = 2,
    VFS_NODE_DEV = 3,
    VFS_NODE_SYMLINK = 4,
} vfs_node_type_t;

typedef struct vfs_stat {
    vfs_node_type_t type;
    u64 size;
    u32 mode;
    u32 inode;
    u32 fs_id;
} vfs_stat_t;

typedef struct vfs_dirent {
    char name[VFS_NAME_MAX];
    vfs_node_type_t type;
    u64 size;
    u32 inode;
} vfs_dirent_t;

typedef struct vfs_statvfs {
    u64 block_size;
    u64 total_blocks;
    u64 free_blocks;
    u64 avail_blocks;
    u64 total_inodes;
    u64 free_inodes;
    u64 fs_id;
    u32 flags;
    u32 max_name_len;
    char mount_path[VFS_PATH_MAX];
    char fs_name[VFS_NAME_MAX];
} vfs_statvfs_t;

#define VFS_STATVFS_FLAG_READONLY   0x00000001u
#define VFS_STATVFS_FLAG_PERSISTENT 0x00000002u
#define VFS_STATVFS_FLAG_JOURNALED  0x00000004u
#define VFS_STATVFS_FLAG_REPAIRABLE 0x00000008u

typedef bool (*vfs_dir_iter_fn)(const vfs_dirent_t *entry, void *ctx);

struct vfs_mount;
typedef struct vfs_mount vfs_mount_t;

typedef struct vfs_ops {
    vfs_status_t (*stat)(vfs_mount_t *mnt, const char *path, vfs_stat_t *out);
    vfs_status_t (*read)(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out);
    vfs_status_t (*write)(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out);
    vfs_status_t (*list)(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx);
    vfs_status_t (*mkdir)(vfs_mount_t *mnt, const char *path);
    vfs_status_t (*create)(vfs_mount_t *mnt, const char *path, const void *data, usize size);
    vfs_status_t (*unlink)(vfs_mount_t *mnt, const char *path);
    vfs_status_t (*truncate)(vfs_mount_t *mnt, const char *path, u64 size);
    vfs_status_t (*rename)(vfs_mount_t *mnt, const char *old_path, const char *new_path);
    vfs_status_t (*sync)(vfs_mount_t *mnt);
    vfs_status_t (*statvfs)(vfs_mount_t *mnt, vfs_statvfs_t *out);
} vfs_ops_t;

struct vfs_mount {
    char path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];
    const vfs_ops_t *ops;
    void *ctx;
    u32 fs_id;
    bool writable;
};

void vfs_init(void);
vfs_status_t vfs_mount(const char *path, const char *name, const vfs_ops_t *ops, void *ctx, bool writable);
vfs_status_t vfs_unmount(const char *path);
vfs_status_t vfs_stat(const char *path, vfs_stat_t *out);
vfs_status_t vfs_read(const char *path, u64 offset, void *buffer, usize size, usize *read_out);
vfs_status_t vfs_write(const char *path, u64 offset, const void *buffer, usize size, usize *written_out);
vfs_status_t vfs_list(const char *path, vfs_dir_iter_fn fn, void *ctx);
vfs_status_t vfs_mkdir(const char *path);
vfs_status_t vfs_create(const char *path, const void *data, usize size);
vfs_status_t vfs_unlink(const char *path);
vfs_status_t vfs_truncate(const char *path, u64 size);
vfs_status_t vfs_rename(const char *old_path, const char *new_path);
vfs_status_t vfs_sync_all(void);
vfs_status_t vfs_sync_path(const char *path);
vfs_status_t vfs_statvfs(const char *path, vfs_statvfs_t *out);
void vfs_dump_mounts(void);
bool vfs_rust_route_selftest(void);
const char *vfs_status_name(vfs_status_t st);

#if defined(__cplusplus)
}
#endif
#endif
