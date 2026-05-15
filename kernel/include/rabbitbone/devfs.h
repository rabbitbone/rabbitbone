#ifndef RABBITBONE_DEVFS_H
#define RABBITBONE_DEVFS_H
#include <rabbitbone/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

vfs_status_t devfs_mount(void);

#if defined(__cplusplus)
}
#endif
#endif
