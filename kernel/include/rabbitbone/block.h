#ifndef RABBITBONE_BLOCK_H
#define RABBITBONE_BLOCK_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define BLOCKDEV_SECTOR_SIZE 512u
#define BLOCKDEV_FLAG_WRITE 0x00000001u
#define BLOCKDEV_FLAG_FLUSH 0x00000002u
#define BLOCKDEV_FLAG_DMA   0x00000004u

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
typedef block_status_t (*block_flush_fn)(struct block_device *dev);

typedef struct block_device {
    char name[16];
    const char *driver;
    u64 sector_count;
    u32 sector_size;
    u32 buffer_alignment;
    u32 max_transfer_sectors;
    u32 flags;
    void *ctx;
    block_read_fn read;
    block_write_fn write;
    block_flush_fn flush;
} block_device_t;

bool block_register(block_device_t *dev);
block_device_t *block_get(usize index);
usize block_count(void);
block_status_t block_read(block_device_t *dev, u64 lba, u32 count, void *buffer);
block_status_t block_write(block_device_t *dev, u64 lba, u32 count, const void *buffer);
block_status_t block_flush(block_device_t *dev);
const char *block_driver_name(const block_device_t *dev);
void block_log_devices(void);
const char *block_status_name(block_status_t status);
void ata_pio_init(void);

#if defined(__cplusplus)
}
#endif
#endif
