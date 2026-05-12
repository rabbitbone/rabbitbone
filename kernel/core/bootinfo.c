#include <aurora/bootinfo.h>
#include <aurora/console.h>

#define BOOTINFO_E820_MAX 64u
#define BOOTINFO_IDENTITY_LIMIT (1024ull * 1024ull * 1024ull)

static bool range_ok(u64 base, u64 bytes) {
    if (bytes == 0) return true;
    u64 end = 0;
    if (__builtin_add_overflow(base, bytes, &end)) return false;
#if !defined(AURORA_HOST_TEST)
    if (base == 0 || end > BOOTINFO_IDENTITY_LIMIT) return false;
#endif
    return true;
}

bool bootinfo_validate(const aurora_bootinfo_t *info) {
    if (!info || info->magic != AURORA_BOOT_MAGIC || info->version != AURORA_BOOT_VERSION || info->e820_count > BOOTINFO_E820_MAX) return false;
    if (info->e820_count != 0) {
        u64 bytes = 0;
        if (__builtin_mul_overflow((u64)info->e820_count, (u64)sizeof(aurora_e820_entry_t), &bytes)) return false;
        if (!range_ok(info->e820_addr, bytes)) return false;
    }
    return true;
}

const aurora_e820_entry_t *bootinfo_e820(const aurora_bootinfo_t *info, u16 index) {
    if (!bootinfo_validate(info) || index >= info->e820_count) return 0;
    const aurora_e820_entry_t *entries = (const aurora_e820_entry_t *)(uptr)info->e820_addr;
    return &entries[index];
}

void bootinfo_dump(const aurora_bootinfo_t *info) {
    if (!bootinfo_validate(info)) {
        kprintf("bootinfo: invalid\n");
        return;
    }
    kprintf("boot: drive=%llu kernel_lba=%llu kernel_sectors=%llu e820=%u\n",
            (unsigned long long)info->boot_drive,
            (unsigned long long)info->kernel_lba,
            (unsigned long long)info->kernel_sectors,
            info->e820_count);
    for (u16 i = 0; i < info->e820_count; ++i) {
        const aurora_e820_entry_t *e = bootinfo_e820(info, i);
        kprintf("  [%u] base=%llx len=%llx type=%u\n", i,
                (unsigned long long)e->base,
                (unsigned long long)e->length,
                e->type);
    }
}
