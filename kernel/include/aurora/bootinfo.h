#ifndef AURORA_BOOTINFO_H
#define AURORA_BOOTINFO_H
#include <aurora/types.h>

#define AURORA_BOOT_MAGIC 0x4155524fu
#define AURORA_BOOT_VERSION 2u
#define AURORA_BOOT_FLAG_BIOS_LEGACY 0x1u
#define AURORA_E820_USABLE 1u
#define AURORA_BOOT_CMDLINE_MAX 256u

typedef struct AURORA_PACKED aurora_e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} aurora_e820_entry_t;

typedef struct AURORA_PACKED aurora_boot_module {
    u64 base;
    u64 size;
    u64 name_addr;
    u64 reserved;
} aurora_boot_module_t;

typedef struct AURORA_PACKED aurora_bootinfo {
    u32 magic;
    u32 version;
    u16 e820_count;
    u16 module_count;
    u32 flags;
    u64 boot_drive;
    u64 e820_addr;
    u64 kernel_lba;
    u64 kernel_sectors;
    u64 modules_addr;
    u64 root_lba;
    u64 root_sectors;
    u64 cmdline_addr;
    u32 cmdline_size;
    u32 reserved0;
    u64 reserved1[3];
} aurora_bootinfo_t;

bool bootinfo_validate(const aurora_bootinfo_t *info);
const aurora_e820_entry_t *bootinfo_e820(const aurora_bootinfo_t *info, u16 index);
const char *bootinfo_cmdline(const aurora_bootinfo_t *info);
void bootinfo_dump(const aurora_bootinfo_t *info);
void bootinfo_format_status(char *out, usize cap);
void bootinfo_remember(const aurora_bootinfo_t *info);

#endif
