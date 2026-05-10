#ifndef AURORA_EXT4_VFS_H
#define AURORA_EXT4_VFS_H
#include <aurora/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

vfs_status_t ext4_vfs_mount_first_linux_partition(const char *path);

#if defined(__cplusplus)
}
#endif
#endif
