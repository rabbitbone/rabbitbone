#include <aurora/block.h>
#include <aurora/libc.h>

#define MAX_BLOCK_DEVS 8u
static block_device_t *devices[MAX_BLOCK_DEVS];
static usize device_count;

bool block_register(block_device_t *dev) {
    if (!dev || !dev->read || device_count >= MAX_BLOCK_DEVS) return false;
    if (dev->sector_count == 0 || dev->sector_size != BLOCKDEV_SECTOR_SIZE) return false;
    if (dev->buffer_alignment == 0) dev->buffer_alignment = 1;
    if (dev->max_transfer_sectors == 0) dev->max_transfer_sectors = 256;
    devices[device_count++] = dev;
    return true;
}

block_device_t *block_get(usize index) { return index < device_count ? devices[index] : 0; }
usize block_count(void) { return device_count; }

static block_status_t validate_range(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    if (!dev || !buffer || count == 0) return BLOCK_ERR_INVALID;
    if (dev->sector_size != BLOCKDEV_SECTOR_SIZE || dev->sector_count == 0) return BLOCK_ERR_INVALID;
    if (dev->buffer_alignment && (((usize)buffer) % dev->buffer_alignment) != 0) return BLOCK_ERR_INVALID;
    if (dev->max_transfer_sectors && count > dev->max_transfer_sectors) return BLOCK_ERR_RANGE;
    if (lba >= dev->sector_count) return BLOCK_ERR_RANGE;
    if ((u64)count > dev->sector_count - lba) return BLOCK_ERR_RANGE;
    return BLOCK_OK;
}

block_status_t block_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    if (!dev || !dev->read) return BLOCK_ERR_INVALID;
    block_status_t st = validate_range(dev, lba, count, buffer);
    if (st != BLOCK_OK) return st;
    return dev->read(dev, lba, count, buffer);
}

block_status_t block_write(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    if (!dev || !dev->write) return BLOCK_ERR_INVALID;
    block_status_t st = validate_range(dev, lba, count, buffer);
    if (st != BLOCK_OK) return st;
    return dev->write(dev, lba, count, buffer);
}

const char *block_status_name(block_status_t status) {
    switch (status) {
        case BLOCK_OK: return "ok";
        case BLOCK_ERR_TIMEOUT: return "timeout";
        case BLOCK_ERR_IO: return "io";
        case BLOCK_ERR_RANGE: return "range";
        case BLOCK_ERR_NO_DEVICE: return "no-device";
        case BLOCK_ERR_INVALID: return "invalid";
        default: return "unknown";
    }
}
