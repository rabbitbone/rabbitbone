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
static u32 next_fs_id;
static u32 next_mount_generation;
static bool ready;

