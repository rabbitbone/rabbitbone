#ifndef AURORA_DEVFS_H
#define AURORA_DEVFS_H
#include <aurora/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

vfs_status_t devfs_mount(void);

#if defined(__cplusplus)
}
#endif
#endif
