#pragma once

#include <rabbitbone/vfs.h>
#include <rabbitbone/path.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/rust.h>
#include <rabbitbone/spinlock.h>

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static spinlock_t mounts_lock;
static spinlock_t vfs_op_lock;
static volatile u64 vfs_op_lock_acquires;
static volatile u64 vfs_op_lock_contention;
static u32 next_fs_id;
static u32 next_mount_generation;
static bool ready;

#define VFS_MAX_PINNED_REFS 512u

typedef struct vfs_ref_pin {
    bool used;
    u32 fs_id;
    u32 mount_generation;
    u32 inode;
    u32 refs;
} vfs_ref_pin_t;

static vfs_ref_pin_t ref_pins[VFS_MAX_PINNED_REFS];
static spinlock_t ref_pins_lock;

