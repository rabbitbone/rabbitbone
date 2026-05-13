static block_status_t ata_flush(bool ext) {
    if (!ata_wait_not_bsy()) return BLOCK_ERR_TIMEOUT;
    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ext ? ATA_CMD_CACHE_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    if (!ata_wait_not_bsy()) return BLOCK_ERR_TIMEOUT;
    u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
    if (st & (ATA_SR_ERR | ATA_SR_DF)) {
        (void)inb(ATA_PRIMARY_IO + ATA_REG_ERROR);
        return BLOCK_ERR_IO;
    }
    return BLOCK_OK;
}

static block_status_t ata_block_flush(block_device_t *dev) {
    (void)dev;
    return ata_flush(primary_supports_lba48);
}

static block_status_t ata_write28(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    (void)dev;
    if (count == 0 || !buffer) return BLOCK_ERR_INVALID;
    if (lba >= 0x10000000ull || (u64)count > 0x10000000ull - lba) return BLOCK_ERR_RANGE;
    const u8 *in = (const u8 *)buffer;
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
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        for (u16 s = 0; s < chunk; ++s) {
            if (!ata_wait_drq()) return BLOCK_ERR_IO;
            for (u32 i = 0; i < 256; ++i) {
                u16 w = (u16)in[0] | ((u16)in[1] << 8);
                outw(ATA_PRIMARY_IO + ATA_REG_DATA, w);
                in += 2;
            }
            ata_400ns_delay();
        }
        block_status_t fl = ata_flush(false);
        if (fl != BLOCK_OK) return fl;
        lba += chunk;
        count -= chunk;
    }
    return BLOCK_OK;
}

static block_status_t ata_write48(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    (void)dev;
    if (count == 0 || count > 65536u || !buffer) return BLOCK_ERR_INVALID;
    if (lba >= ATA_LBA48_LIMIT || (u64)count > ATA_LBA48_LIMIT - lba) return BLOCK_ERR_RANGE;
    const u8 *in = (const u8 *)buffer;
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
        outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
        for (u16 s = 0; s < chunk; ++s) {
            if (!ata_wait_drq()) return BLOCK_ERR_IO;
            for (u32 i = 0; i < 256; ++i) {
                u16 w = (u16)in[0] | ((u16)in[1] << 8);
                outw(ATA_PRIMARY_IO + ATA_REG_DATA, w);
                in += 2;
            }
            ata_400ns_delay();
        }
        block_status_t fl = ata_flush(true);
        if (fl != BLOCK_OK) return fl;
        lba += chunk;
        count -= chunk;
    }
    return BLOCK_OK;
}

