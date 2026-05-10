#ifndef AURORA_TARFS_H
#define AURORA_TARFS_H
#include <aurora/vfs.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct tarfs tarfs_t;

tarfs_t *tarfs_open(const void *image, usize size);
void tarfs_destroy(tarfs_t *fs);
const vfs_ops_t *tarfs_ops(void);

#if defined(__cplusplus)
}
#endif
#endif
