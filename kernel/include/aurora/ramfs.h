#ifndef AURORA_RAMFS_H
#define AURORA_RAMFS_H
#include <aurora/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ramfs ramfs_t;

ramfs_t *ramfs_create(const char *label);
void ramfs_destroy(ramfs_t *fs);
const vfs_ops_t *ramfs_ops(void);
vfs_status_t ramfs_add_file(ramfs_t *fs, const char *path, const void *data, usize size);
vfs_status_t ramfs_add_dir(ramfs_t *fs, const char *path);
vfs_status_t ramfs_mount_boot(void);

#if defined(__cplusplus)
}
#endif
#endif
