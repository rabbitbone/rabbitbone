#pragma once

#include <rabbitbone/tarfs.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/path.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/abi.h>

typedef struct tarfs_entry {
    char path[VFS_PATH_MAX];
    const u8 *data;
    usize size;
    vfs_node_type_t type;
    u32 inode;
} tarfs_entry_t;

struct tarfs {
    const u8 *image;
    usize size;
    tarfs_entry_t *entries;
    usize count;
};

typedef struct RABBITBONE_PACKED tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} tar_header_t;

