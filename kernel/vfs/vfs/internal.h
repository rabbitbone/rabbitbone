#pragma once

#include <aurora/vfs.h>
#include <aurora/path.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/rust.h>
#include <aurora/spinlock.h>

static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static spinlock_t mounts_lock;
static u32 next_fs_id;
static u32 next_mount_generation;
static bool ready;

