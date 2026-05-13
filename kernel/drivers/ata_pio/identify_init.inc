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
    spinlock_init(&primary_channel_lock);
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
    primary_master.driver = "ata_pio";
    primary_master.sector_count = sectors;
    primary_master.sector_size = 512;
    primary_master.read = ata_read;
    primary_master.write = ata_write;
    primary_master.flush = ata_block_flush;
    primary_master.buffer_alignment = 2;
    primary_master.max_transfer_sectors = 256;
    if (!block_register(&primary_master)) {
        KLOG(LOG_WARN, "ata", "ata0 identify returned unusable capacity or geometry sectors=%llu", (unsigned long long)sectors);
        return;
    }
    KLOG(LOG_INFO, "ata", "registered ata0 sectors=%llu lba48=%u", (unsigned long long)sectors, primary_supports_lba48 ? 1u : 0u);
}
