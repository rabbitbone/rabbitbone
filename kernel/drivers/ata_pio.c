#include <aurora/block.h>
#include <aurora/arch/io.h>
#include <aurora/log.h>
#include <aurora/libc.h>

#define ATA_PRIMARY_IO 0x1f0u
#define ATA_PRIMARY_CTRL 0x3f6u
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_SECCOUNT0 2
#define ATA_REG_LBA0 3
#define ATA_REG_LBA1 4
#define ATA_REG_LBA2 5
#define ATA_REG_HDDEVSEL 6
#define ATA_REG_COMMAND 7
#define ATA_REG_STATUS 7
#define ATA_CMD_IDENTIFY 0xec
#define ATA_CMD_READ_PIO 0x20
#define ATA_CMD_READ_PIO_EXT 0x24
#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01
#define ATA_LBA48_LIMIT (1ull << 48)

static block_device_t primary_master;
static u16 identify_data[256];
static bool primary_supports_lba48;

static void ata_400ns_delay(void) {
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
}

static bool ata_wait_not_bsy(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        if ((inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY) == 0) return true;
    }
    return false;
}

static bool ata_wait_ready(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRDY)) return true;
    }
    return false;
}

static bool ata_wait_drq(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return true;
    }
    return false;
}

static void ata_select_master_lba28(u64 lba) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, (u8)(0xe0u | ((lba >> 24) & 0x0fu)));
    ata_400ns_delay();
}

static block_status_t ata_read28(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    (void)dev;
    if (count == 0 || !buffer) return BLOCK_ERR_INVALID;
    if (lba >= 0x10000000ull || (u64)count > 0x10000000ull - lba) return BLOCK_ERR_RANGE;
    u8 *out = (u8 *)buffer;
    while (count) {
        u16 chunk = count > 256u ? 256u : (u16)count;
        u8 sector_count_reg = chunk == 256u ? 0u : (u8)chunk;
        if (!ata_wait_not_bsy()) return BLOCK_ERR_TIMEOUT;
        ata_select_master_lba28(lba);
        if (!ata_wait_ready()) return BLOCK_ERR_IO;
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, sector_count_reg);
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (u8)(lba & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (u8)((lba >> 8) & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (u8)((lba >> 16) & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        for (u16 s = 0; s < chunk; ++s) {
            if (!ata_wait_drq()) return BLOCK_ERR_IO;
            for (u32 i = 0; i < 256; ++i) {
                u16 w = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
                out[0] = (u8)(w & 0xffu);
                out[1] = (u8)(w >> 8);
                out += 2;
            }
            ata_400ns_delay();
        }
        lba += chunk;
        count -= chunk;
    }
    return BLOCK_OK;
}

static block_status_t ata_read48(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    (void)dev;
    if (count == 0 || count > 65536u || !buffer) return BLOCK_ERR_INVALID;
    if (lba >= ATA_LBA48_LIMIT || (u64)count > ATA_LBA48_LIMIT - lba) return BLOCK_ERR_RANGE;
    u8 *out = (u8 *)buffer;
    while (count) {
        u16 chunk = count > 256u ? 256u : (u16)count;
        u16 sector_count_reg = chunk;
        if (!ata_wait_not_bsy()) return BLOCK_ERR_TIMEOUT;
        outb(ATA_PRIMARY_CTRL, 0x02);
        outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0x40);
        ata_400ns_delay();
        if (!ata_wait_ready()) return BLOCK_ERR_IO;
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, (u8)(sector_count_reg >> 8));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (u8)(lba >> 24));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (u8)(lba >> 32));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (u8)(lba >> 40));
        outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, (u8)(sector_count_reg & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (u8)(lba & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (u8)((lba >> 8) & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (u8)((lba >> 16) & 0xffu));
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
        for (u16 s = 0; s < chunk; ++s) {
            if (!ata_wait_drq()) return BLOCK_ERR_IO;
            for (u32 i = 0; i < 256; ++i) {
                u16 w = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
                out[0] = (u8)(w & 0xffu);
                out[1] = (u8)(w >> 8);
                out += 2;
            }
            ata_400ns_delay();
        }
        lba += chunk;
        count -= chunk;
    }
    return BLOCK_OK;
}

static block_status_t ata_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    if (!dev || !buffer || count == 0) return BLOCK_ERR_INVALID;
    if (primary_supports_lba48 && (lba >= ATA_LBA48_LIMIT || (u64)count > ATA_LBA48_LIMIT - lba)) return BLOCK_ERR_RANGE;
    if (lba < 0x10000000ull && (u64)count <= 0x10000000ull - lba) return ata_read28(dev, lba, count, buffer);
    if (!primary_supports_lba48) return BLOCK_ERR_RANGE;
    return ata_read48(dev, lba, count, buffer);
}

static bool ata_identify(void) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xa0);
    ata_400ns_delay();
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, 0);
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (st == 0) return false;
    if (!ata_wait_drq()) return false;
    for (u32 i = 0; i < 256; ++i) identify_data[i] = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
    ata_400ns_delay();
    return true;
}

void ata_pio_init(void) {
    if (!ata_identify()) {
        KLOG(LOG_WARN, "ata", "primary master not detected");
        return;
    }
    primary_supports_lba48 = (identify_data[83] & (1u << 10)) != 0;
    u64 sectors = 0;
    if (primary_supports_lba48) {
        sectors = ((u64)identify_data[103] << 48) | ((u64)identify_data[102] << 32) | ((u64)identify_data[101] << 16) | identify_data[100];
    }
    if (sectors == 0) sectors = ((u32)identify_data[61] << 16) | identify_data[60];
    memset(&primary_master, 0, sizeof(primary_master));
    strncpy(primary_master.name, "ata0", sizeof(primary_master.name) - 1);
    primary_master.sector_count = sectors;
    primary_master.sector_size = 512;
    primary_master.read = ata_read;
    block_register(&primary_master);
    KLOG(LOG_INFO, "ata", "registered ata0 sectors=%llu lba48=%u", (unsigned long long)sectors, primary_supports_lba48 ? 1u : 0u);
}
