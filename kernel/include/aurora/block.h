#ifndef AURORA_BLOCK_H
#define AURORA_BLOCK_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define BLOCKDEV_SECTOR_SIZE 512u

typedef enum block_status {
    BLOCK_OK = 0,
    BLOCK_ERR_TIMEOUT = -1,
    BLOCK_ERR_IO = -2,
    BLOCK_ERR_RANGE = -3,
    BLOCK_ERR_NO_DEVICE = -4,
    BLOCK_ERR_INVALID = -5,
} block_status_t;

struct block_device;
typedef block_status_t (*block_read_fn)(struct block_device *dev, u64 lba, u32 count, void *buffer);
typedef block_status_t (*block_write_fn)(struct block_device *dev, u64 lba, u32 count, const void *buffer);

typedef struct block_device {
    char name[16];
    u64 sector_count;
    u32 sector_size;
    void *ctx;
    block_read_fn read;
    block_write_fn write;
} block_device_t;

void block_register(block_device_t *dev);
block_device_t *block_get(usize index);
usize block_count(void);
block_status_t block_read(block_device_t *dev, u64 lba, u32 count, void *buffer);
block_status_t block_write(block_device_t *dev, u64 lba, u32 count, const void *buffer);
const char *block_status_name(block_status_t status);
void ata_pio_init(void);

#if defined(__cplusplus)
}
#endif
#endif
