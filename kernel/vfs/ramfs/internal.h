#pragma once

#include <rabbitbone/ramfs.h>
#include <rabbitbone/version.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/path.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/abi.h>
#include <rabbitbone/rust.h>

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

