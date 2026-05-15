#include <rabbitbone/bootinfo.h>
#include <rabbitbone/block.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/console.h>

#define BOOTRAMDISK_MAX 4u

typedef struct bootramdisk {
    block_device_t block;
    u8 *base;
    u64 size;
} bootramdisk_t;

static bootramdisk_t g_bootramdisks[BOOTRAMDISK_MAX];
static usize g_bootramdisk_count;

static block_status_t bootramdisk_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    if (!dev || !dev->ctx || !buffer || count == 0) return BLOCK_ERR_INVALID;
    bootramdisk_t *rd = (bootramdisk_t *)dev->ctx;
    u64 off = 0;
    u64 bytes = 0;
    if (__builtin_mul_overflow(lba, (u64)BLOCKDEV_SECTOR_SIZE, &off) ||
        __builtin_mul_overflow((u64)count, (u64)BLOCKDEV_SECTOR_SIZE, &bytes)) return BLOCK_ERR_RANGE;
    if (off > rd->size || bytes > rd->size - off) return BLOCK_ERR_RANGE;
    memcpy(buffer, rd->base + off, (usize)bytes);
    return BLOCK_OK;
}

static block_status_t bootramdisk_write(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    if (!dev || !dev->ctx || !buffer || count == 0) return BLOCK_ERR_INVALID;
    bootramdisk_t *rd = (bootramdisk_t *)dev->ctx;
    u64 off = 0;
    u64 bytes = 0;
    if (__builtin_mul_overflow(lba, (u64)BLOCKDEV_SECTOR_SIZE, &off) ||
        __builtin_mul_overflow((u64)count, (u64)BLOCKDEV_SECTOR_SIZE, &bytes)) return BLOCK_ERR_RANGE;
    if (off > rd->size || bytes > rd->size - off) return BLOCK_ERR_RANGE;
    memcpy(rd->base + off, buffer, (usize)bytes);
    return BLOCK_OK;
}

void bootramdisk_init(const rabbitbone_bootinfo_t *bootinfo) {
    if (!bootinfo_validate(bootinfo) || bootinfo->module_count == 0 || bootinfo->modules_addr == 0) return;
    const rabbitbone_boot_module_t *mods = (const rabbitbone_boot_module_t *)(uptr)bootinfo->modules_addr;
    for (u16 i = 0; i < bootinfo->module_count && g_bootramdisk_count < BOOTRAMDISK_MAX; ++i) {
        const rabbitbone_boot_module_t *m = &mods[i];
        if (m->base == 0 || m->size < BLOCKDEV_SECTOR_SIZE || (m->size % BLOCKDEV_SECTOR_SIZE) != 0) continue;
        bootramdisk_t *rd = &g_bootramdisks[g_bootramdisk_count];
        memset(rd, 0, sizeof(*rd));
        rd->base = (u8 *)(uptr)m->base;
        rd->size = m->size;
        ksnprintf(rd->block.name, sizeof(rd->block.name), "ramdisk%llu", (unsigned long long)g_bootramdisk_count);
        rd->block.driver = "bootramdisk";
        rd->block.sector_count = m->size / BLOCKDEV_SECTOR_SIZE;
        rd->block.sector_size = BLOCKDEV_SECTOR_SIZE;
        rd->block.buffer_alignment = 1;
        rd->block.max_transfer_sectors = 128;
        rd->block.ctx = rd;
        rd->block.read = bootramdisk_read;
        rd->block.write = bootramdisk_write;
        if (block_register(&rd->block)) {
            const char *name = m->name_addr ? (const char *)(uptr)m->name_addr : "module";
            KLOG(LOG_INFO, "bootramdisk", "registered %s from %s base=%p size=%llu sectors=%llu",
                 rd->block.name, name, (void *)(uptr)m->base,
                 (unsigned long long)m->size, (unsigned long long)rd->block.sector_count);
            ++g_bootramdisk_count;
        }
    }
}
