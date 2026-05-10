#include <aurora/block.h>
#include <aurora/libc.h>

#define MAX_BLOCK_DEVS 8u
static block_device_t *devices[MAX_BLOCK_DEVS];
static usize device_count;

void block_register(block_device_t *dev) {
    if (!dev || !dev->read || device_count >= MAX_BLOCK_DEVS) return;
    devices[device_count++] = dev;
}

block_device_t *block_get(usize index) { return index < device_count ? devices[index] : 0; }
usize block_count(void) { return device_count; }

block_status_t block_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    if (!dev || !dev->read || !buffer || count == 0) return BLOCK_ERR_INVALID;
    if (dev->sector_size && dev->sector_size != BLOCKDEV_SECTOR_SIZE) return BLOCK_ERR_INVALID;
    if (dev->sector_count) {
        if (lba >= dev->sector_count) return BLOCK_ERR_RANGE;
        if ((u64)count > dev->sector_count - lba) return BLOCK_ERR_RANGE;
    }
    return dev->read(dev, lba, count, buffer);
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
