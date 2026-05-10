#include <aurora/mbr.h>
#include <aurora/libc.h>

static u32 le32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

bool mbr_parse_sector(const u8 sector[512], mbr_table_t *out) {
    if (!sector || !out) return false;
    if (sector[510] != 0x55 || sector[511] != 0xaa) return false;
    memset(out, 0, sizeof(*out));
    for (usize i = 0; i < 4; ++i) {
        const u8 *p = sector + 446 + i * 16;
        out->part[i].bootable = p[0] == 0x80;
        out->part[i].type = p[4];
        out->part[i].lba_first = le32(p + 8);
        out->part[i].sector_count = le32(p + 12);
    }
    return true;
}

bool mbr_read(block_device_t *dev, mbr_table_t *out) {
    u8 sector[512];
    if (block_read(dev, 0, 1, sector) != BLOCK_OK) return false;
    return mbr_parse_sector(sector, out);
}

bool mbr_partition_valid(const block_device_t *dev, const mbr_partition_t *part) {
    if (!dev || !part || part->sector_count == 0) return false;
    if (dev->sector_count == 0) return true;
    u64 first = part->lba_first;
    u64 count = part->sector_count;
    if (first >= dev->sector_count) return false;
    if (count > dev->sector_count - first) return false;
    return true;
}

const mbr_partition_t *mbr_find_linux(const mbr_table_t *mbr) {
    if (!mbr) return 0;
    for (usize i = 0; i < 4; ++i) {
        if (mbr->part[i].type == 0x83 && mbr->part[i].sector_count) return &mbr->part[i];
    }
    return 0;
}

const mbr_partition_t *mbr_find_linux_on_device(const block_device_t *dev, const mbr_table_t *mbr) {
    if (!mbr) return 0;
    for (usize i = 0; i < 4; ++i) {
        if (mbr->part[i].type == 0x83 && mbr_partition_valid(dev, &mbr->part[i])) return &mbr->part[i];
    }
    return 0;
}
