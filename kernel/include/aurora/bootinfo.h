#ifndef AURORA_BOOTINFO_H
#define AURORA_BOOTINFO_H
#include <aurora/types.h>

#define AURORA_BOOT_MAGIC 0x4155524fu
#define AURORA_BOOT_VERSION 1u
#define AURORA_E820_USABLE 1u

typedef struct AURORA_PACKED aurora_e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} aurora_e820_entry_t;

typedef struct AURORA_PACKED aurora_bootinfo {
    u32 magic;
    u32 version;
    u16 e820_count;
    u16 reserved0;
    u32 reserved1;
    u64 boot_drive;
    u64 e820_addr;
    u64 kernel_lba;
    u64 kernel_sectors;
    u64 reserved2[2];
} aurora_bootinfo_t;

bool bootinfo_validate(const aurora_bootinfo_t *info);
const aurora_e820_entry_t *bootinfo_e820(const aurora_bootinfo_t *info, u16 index);
void bootinfo_dump(const aurora_bootinfo_t *info);

#endif
