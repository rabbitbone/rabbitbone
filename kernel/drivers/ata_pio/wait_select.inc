static void ata_400ns_delay(void) {
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
    (void)inb(ATA_PRIMARY_CTRL);
}

static bool ata_wait_not_bsy(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        if ((inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_SR_BSY) == 0) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool ata_wait_ready(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRDY)) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static bool ata_wait_drq(void) {
    for (u32 i = 0; i < 1000000; ++i) {
        u8 st = inb(ATA_PRIMARY_IO + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return false;
        if ((st & ATA_SR_BSY) == 0 && (st & ATA_SR_DRQ)) return true;
        __asm__ volatile("pause");
    }
    return false;
}

static void ata_select_master_lba28(u64 lba) {
    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, (u8)(0xe0u | ((lba >> 24) & 0x0fu)));
    ata_400ns_delay();
}

