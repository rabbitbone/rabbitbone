static block_status_t ata_write(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    u64 irq = spin_lock_irqsave(&primary_channel_lock);
    block_status_t st = BLOCK_ERR_INVALID;
    if (!dev || !buffer || count == 0) goto out;
    if (dev->sector_count && (lba >= dev->sector_count || (u64)count > dev->sector_count - lba)) { st = BLOCK_ERR_RANGE; goto out; }
    if (primary_supports_lba48 && (lba >= ATA_LBA48_LIMIT || (u64)count > ATA_LBA48_LIMIT - lba)) { st = BLOCK_ERR_RANGE; goto out; }
    if (lba < 0x10000000ull && (u64)count <= 0x10000000ull - lba) { st = ata_write28(dev, lba, count, buffer); goto out; }
    if (!primary_supports_lba48) { st = BLOCK_ERR_RANGE; goto out; }
    st = ata_write48(dev, lba, count, buffer);
out:
    spin_unlock_irqrestore(&primary_channel_lock, irq);
    return st;
}

static block_status_t ata_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    u64 irq = spin_lock_irqsave(&primary_channel_lock);
    block_status_t st = BLOCK_ERR_INVALID;
    if (!dev || !buffer || count == 0) goto out;
    if (dev->sector_count && (lba >= dev->sector_count || (u64)count > dev->sector_count - lba)) { st = BLOCK_ERR_RANGE; goto out; }
    if (primary_supports_lba48 && (lba >= ATA_LBA48_LIMIT || (u64)count > ATA_LBA48_LIMIT - lba)) { st = BLOCK_ERR_RANGE; goto out; }
    if (lba < 0x10000000ull && (u64)count <= 0x10000000ull - lba) { st = ata_read28(dev, lba, count, buffer); goto out; }
    if (!primary_supports_lba48) { st = BLOCK_ERR_RANGE; goto out; }
    st = ata_read48(dev, lba, count, buffer);
out:
    spin_unlock_irqrestore(&primary_channel_lock, irq);
    return st;
}
