#ifndef RABBITBONE_VFS_H
#define RABBITBONE_VFS_H
#include <rabbitbone/types.h>
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
    VFS_ERR_BUSY = -12,
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
    u32 nlink;
    u32 uid;
    u32 gid;
} vfs_stat_t;

typedef struct vfs_node_ref {
    u32 fs_id;
    u32 inode;
    u32 mount_generation;
    u32 reserved;
    vfs_node_type_t type;
    u64 size;
    char path[VFS_PATH_MAX];
} vfs_node_ref_t;

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
    vfs_status_t (*lstat)(vfs_mount_t *mnt, const char *path, vfs_stat_t *out);
    vfs_status_t (*readlink)(vfs_mount_t *mnt, const char *path, char *buffer, usize size, usize *read_out);
    vfs_status_t (*symlink)(vfs_mount_t *mnt, const char *target, const char *link_path);
    vfs_status_t (*link)(vfs_mount_t *mnt, const char *old_path, const char *new_path);
    vfs_status_t (*read)(vfs_mount_t *mnt, const char *path, u64 offset, void *buffer, usize size, usize *read_out);
    vfs_status_t (*write)(vfs_mount_t *mnt, const char *path, u64 offset, const void *buffer, usize size, usize *written_out);
    vfs_status_t (*stat_inode)(vfs_mount_t *mnt, u32 inode, vfs_stat_t *out);
    vfs_status_t (*read_inode)(vfs_mount_t *mnt, u32 inode, u64 offset, void *buffer, usize size, usize *read_out);
    vfs_status_t (*write_inode)(vfs_mount_t *mnt, u32 inode, u64 offset, const void *buffer, usize size, usize *written_out);
    vfs_status_t (*truncate_inode)(vfs_mount_t *mnt, u32 inode, u64 size);
    vfs_status_t (*list)(vfs_mount_t *mnt, const char *path, vfs_dir_iter_fn fn, void *ctx);
    vfs_status_t (*list_inode)(vfs_mount_t *mnt, u32 inode, vfs_dir_iter_fn fn, void *ctx);
    vfs_status_t (*mkdir)(vfs_mount_t *mnt, const char *path);
    vfs_status_t (*create)(vfs_mount_t *mnt, const char *path, const void *data, usize size);
    vfs_status_t (*unlink)(vfs_mount_t *mnt, const char *path);
    vfs_status_t (*truncate)(vfs_mount_t *mnt, const char *path, u64 size);
    vfs_status_t (*rename)(vfs_mount_t *mnt, const char *old_path, const char *new_path);
    vfs_status_t (*preallocate)(vfs_mount_t *mnt, const char *path, u64 size);
    vfs_status_t (*preallocate_inode)(vfs_mount_t *mnt, u32 inode, u64 size);
    vfs_status_t (*chmod)(vfs_mount_t *mnt, const char *path, u32 mode);
    vfs_status_t (*chown)(vfs_mount_t *mnt, const char *path, u32 uid, u32 gid);
    vfs_status_t (*sync)(vfs_mount_t *mnt);
    vfs_status_t (*sync_inode)(vfs_mount_t *mnt, u32 inode, bool data_only);
    vfs_status_t (*statvfs)(vfs_mount_t *mnt, vfs_statvfs_t *out);
} vfs_ops_t;

struct vfs_mount {
    char path[VFS_PATH_MAX];
    char name[VFS_NAME_MAX];
    const vfs_ops_t *ops;
    void *ctx;
    u32 fs_id;
    u32 generation;
    volatile u32 active_refs;
    bool writable;
    bool unmounting;
};

void vfs_init(void);
vfs_status_t vfs_mount(const char *path, const char *name, const vfs_ops_t *ops, void *ctx, bool writable);
vfs_status_t vfs_unmount(const char *path);
vfs_status_t vfs_stat(const char *path, vfs_stat_t *out);
vfs_status_t vfs_lstat(const char *path, vfs_stat_t *out);
vfs_status_t vfs_readlink(const char *path, char *buffer, usize size, usize *read_out);
vfs_status_t vfs_symlink(const char *target, const char *link_path);
vfs_status_t vfs_link(const char *old_path, const char *new_path);
vfs_status_t vfs_get_ref(const char *path, vfs_node_ref_t *out);
bool vfs_retain_ref(const vfs_node_ref_t *ref);
void vfs_release_ref(const vfs_node_ref_t *ref);
u32 vfs_ref_pin_count(const vfs_node_ref_t *ref);
bool vfs_ref_is_busy(const vfs_node_ref_t *ref);
vfs_status_t vfs_stat_ref(const vfs_node_ref_t *ref, vfs_stat_t *out);
vfs_status_t vfs_read(const char *path, u64 offset, void *buffer, usize size, usize *read_out);
vfs_status_t vfs_read_ref(const vfs_node_ref_t *ref, u64 offset, void *buffer, usize size, usize *read_out);
vfs_status_t vfs_write(const char *path, u64 offset, const void *buffer, usize size, usize *written_out);
vfs_status_t vfs_write_ref(const vfs_node_ref_t *ref, u64 offset, const void *buffer, usize size, usize *written_out);
vfs_status_t vfs_list(const char *path, vfs_dir_iter_fn fn, void *ctx);
vfs_status_t vfs_list_ref(const vfs_node_ref_t *ref, vfs_dir_iter_fn fn, void *ctx);
vfs_status_t vfs_mkdir(const char *path);
vfs_status_t vfs_create(const char *path, const void *data, usize size);
vfs_status_t vfs_unlink(const char *path);
vfs_status_t vfs_truncate(const char *path, u64 size);
vfs_status_t vfs_truncate_ref(const vfs_node_ref_t *ref, u64 size);
vfs_status_t vfs_rename(const char *old_path, const char *new_path);
vfs_status_t vfs_preallocate(const char *path, u64 size);
vfs_status_t vfs_preallocate_ref(const vfs_node_ref_t *ref, u64 size);
vfs_status_t vfs_chmod(const char *path, u32 mode);
vfs_status_t vfs_chown(const char *path, u32 uid, u32 gid);
vfs_status_t vfs_sync_all(void);
vfs_status_t vfs_sync_path(const char *path);
vfs_status_t vfs_sync_ref(const vfs_node_ref_t *ref, bool data_only);
vfs_status_t vfs_sync_parent_dir(const char *path);
vfs_status_t vfs_statvfs(const char *path, vfs_statvfs_t *out);
vfs_status_t vfs_install_commit(const char *staged_path, const char *final_path);
void vfs_dump_mounts(void);
bool vfs_rust_route_selftest(void);
const char *vfs_status_name(vfs_status_t st);

#if defined(__cplusplus)
}
#endif
#endif
