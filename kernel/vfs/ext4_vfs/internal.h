#pragma once

#include <rabbitbone/ext4_vfs.h>
#include <rabbitbone/ext4.h>
#include <rabbitbone/block.h>
#include <rabbitbone/mbr.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/path.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>

typedef struct ext4_vfs_ctx {
    ext4_mount_t mount;
    char source[32];
} ext4_vfs_ctx_t;

