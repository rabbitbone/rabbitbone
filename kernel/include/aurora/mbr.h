#ifndef AURORA_MBR_H
#define AURORA_MBR_H
#include <aurora/types.h>
#include <aurora/block.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct AURORA_PACKED mbr_partition_raw {
    u8 bootable;
    u8 chs_first[3];
    u8 type;
    u8 chs_last[3];
    u32 lba_first;
    u32 sector_count;
} mbr_partition_raw_t;

typedef struct mbr_partition {
    bool bootable;
    u8 type;
    u32 lba_first;
    u32 sector_count;
} mbr_partition_t;

typedef struct mbr_table {
    mbr_partition_t part[4];
} mbr_table_t;

bool mbr_parse_sector(const u8 sector[512], mbr_table_t *out);
bool mbr_read(block_device_t *dev, mbr_table_t *out);
bool mbr_partition_valid(const block_device_t *dev, const mbr_partition_t *part);
const mbr_partition_t *mbr_find_linux(const mbr_table_t *mbr);
const mbr_partition_t *mbr_find_linux_on_device(const block_device_t *dev, const mbr_table_t *mbr);

#if defined(__cplusplus)
}
#endif
#endif
