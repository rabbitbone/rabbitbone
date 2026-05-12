#pragma once

#include <aurora/ramfs.h>
#include <aurora/version.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/spinlock.h>
#include <aurora/abi.h>

#define RAMFS_MAX_FILE_SIZE (16ull * 1024ull * 1024ull)
#define RAMFS_MAX_TOTAL_BYTES (64ull * 1024ull * 1024ull)
#define RAMFS_MAX_NODES 16384u
#define RAMFS_SYMLINK_MAX_DEPTH 16u

typedef struct ramfs_node ramfs_node_t;

typedef struct ramfs_object {
    vfs_node_type_t type;
    u32 inode;
    u32 mode;
    u32 nlink;
    u32 uid;
    u32 gid;
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
    spinlock_t lock;
    u32 node_count;
    u32 max_nodes;
    usize bytes_used;
    usize max_bytes;
};

