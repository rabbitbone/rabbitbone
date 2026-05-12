#pragma once

#include <aurora/ext4_vfs.h>
#include <aurora/ext4.h>
#include <aurora/block.h>
#include <aurora/mbr.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/libc.h>
#include <aurora/log.h>

typedef struct ext4_vfs_ctx {
    ext4_mount_t mount;
    char source[32];
} ext4_vfs_ctx_t;

