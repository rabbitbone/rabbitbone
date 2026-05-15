#ifndef RABBITBONE_BOOTINFO_H
#define RABBITBONE_BOOTINFO_H
#include <rabbitbone/types.h>

#define RABBITBONE_BOOT_MAGIC 0x52424f4eu
#define RABBITBONE_BOOT_VERSION 2u
#define RABBITBONE_BOOT_FLAG_BIOS_LEGACY 0x1u
#define RABBITBONE_BOOT_FLAG_UEFI 0x2u
#define RABBITBONE_BOOT_FLAG_LIVE_RAMDISK 0x4u
#define RABBITBONE_E820_USABLE 1u
#define RABBITBONE_BOOT_CMDLINE_MAX 256u
#define RABBITBONE_BOOT_FB_FORMAT_NONE 0u
#define RABBITBONE_BOOT_FB_FORMAT_RGBX 1u
#define RABBITBONE_BOOT_FB_FORMAT_BGRX 2u

#define RABBITBONE_BOOT_FB_BASE(info) ((info)->reserved1[0])
#define RABBITBONE_BOOT_FB_WIDTH(info) ((u32)(((info)->reserved1[1] >> 32) & 0xffffffffull))
#define RABBITBONE_BOOT_FB_HEIGHT(info) ((u32)((info)->reserved1[1] & 0xffffffffull))
#define RABBITBONE_BOOT_FB_PITCH_PIXELS(info) ((u32)(((info)->reserved1[2] >> 32) & 0xffffffffull))
#define RABBITBONE_BOOT_FB_FORMAT(info) ((u32)((info)->reserved1[2] & 0xffffffffull))

typedef struct RABBITBONE_PACKED rabbitbone_e820_entry {
    u64 base;
    u64 length;
    u32 type;
    u32 acpi;
} rabbitbone_e820_entry_t;

typedef struct RABBITBONE_PACKED rabbitbone_boot_module {
    u64 base;
    u64 size;
    u64 name_addr;
    u64 reserved;
} rabbitbone_boot_module_t;

typedef struct RABBITBONE_PACKED rabbitbone_bootinfo {
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
} rabbitbone_bootinfo_t;

bool bootinfo_validate(const rabbitbone_bootinfo_t *info);
const char *bootinfo_validate_reason(const rabbitbone_bootinfo_t *info);
bool bootinfo_basic_usable(const rabbitbone_bootinfo_t *info);
const rabbitbone_e820_entry_t *bootinfo_e820(const rabbitbone_bootinfo_t *info, u16 index);
const char *bootinfo_cmdline(const rabbitbone_bootinfo_t *info);
void bootinfo_dump(const rabbitbone_bootinfo_t *info);
void bootinfo_format_status(char *out, usize cap);
void bootinfo_remember(const rabbitbone_bootinfo_t *info);

#endif
