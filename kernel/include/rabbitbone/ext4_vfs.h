#ifndef RABBITBONE_EXT4_VFS_H
#define RABBITBONE_EXT4_VFS_H
#include <rabbitbone/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

vfs_status_t ext4_vfs_mount_first_linux_partition(const char *path);

#if defined(__cplusplus)
}
#endif
#endif
