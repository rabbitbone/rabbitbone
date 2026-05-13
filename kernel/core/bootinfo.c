#include <aurora/bootinfo.h>
#include <aurora/console.h>
#include <aurora/libc.h>

#define BOOTINFO_E820_MAX 64u
#define BOOTINFO_MODULE_MAX 16u
#define BOOTINFO_IDENTITY_LIMIT (1024ull * 1024ull * 1024ull)

static const aurora_bootinfo_t *g_bootinfo;

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
    if (!info || info->magic != AURORA_BOOT_MAGIC || info->version != AURORA_BOOT_VERSION ||
        info->e820_count > BOOTINFO_E820_MAX || info->module_count > BOOTINFO_MODULE_MAX) return false;
    if (info->e820_count != 0) {
        u64 bytes = 0;
        if (__builtin_mul_overflow((u64)info->e820_count, (u64)sizeof(aurora_e820_entry_t), &bytes)) return false;
        if (!range_ok(info->e820_addr, bytes)) return false;
    }
    if (info->module_count != 0) {
        u64 bytes = 0;
        if (__builtin_mul_overflow((u64)info->module_count, (u64)sizeof(aurora_boot_module_t), &bytes)) return false;
        if (!range_ok(info->modules_addr, bytes)) return false;
    }
    if (info->cmdline_size > AURORA_BOOT_CMDLINE_MAX) return false;
    if (info->cmdline_size != 0 && !range_ok(info->cmdline_addr, (u64)info->cmdline_size)) return false;
    return true;
}

void bootinfo_remember(const aurora_bootinfo_t *info) {
    if (bootinfo_validate(info)) g_bootinfo = info;
}

const aurora_e820_entry_t *bootinfo_e820(const aurora_bootinfo_t *info, u16 index) {
    if (!bootinfo_validate(info) || index >= info->e820_count) return 0;
    const aurora_e820_entry_t *entries = (const aurora_e820_entry_t *)(uptr)info->e820_addr;
    return &entries[index];
}

const char *bootinfo_cmdline(const aurora_bootinfo_t *info) {
    if (!bootinfo_validate(info) || info->cmdline_size == 0) return "";
    return (const char *)(uptr)info->cmdline_addr;
}

void bootinfo_dump(const aurora_bootinfo_t *info) {
    if (!bootinfo_validate(info)) {
        kprintf("bootinfo: invalid\n");
        return;
    }
    kprintf("boot: v=%u drive=%llu kernel_lba=%llu kernel_sectors=%llu root_lba=%llu root_sectors=%llu modules=%u flags=0x%x e820=%u\n",
            info->version,
            (unsigned long long)info->boot_drive,
            (unsigned long long)info->kernel_lba,
            (unsigned long long)info->kernel_sectors,
            (unsigned long long)info->root_lba,
            (unsigned long long)info->root_sectors,
            info->module_count,
            info->flags,
            info->e820_count);
    if (info->cmdline_size) kprintf("boot: cmdline='%s'\n", bootinfo_cmdline(info));
    for (u16 i = 0; i < info->e820_count; ++i) {
        const aurora_e820_entry_t *e = bootinfo_e820(info, i);
        kprintf("  [%u] base=%llx len=%llx type=%u\n", i,
                (unsigned long long)e->base,
                (unsigned long long)e->length,
                e->type);
    }
}

void bootinfo_format_status(char *out, usize cap) {
    if (!out || cap == 0) return;
    if (!bootinfo_validate(g_bootinfo)) {
        ksnprintf(out, cap, "boot: unavailable\n");
        return;
    }
    ksnprintf(out, cap,
              "boot: contract=v%u source=bios-int13 drive=%llu flags=0x%x\n"
              "kernel: lba=%llu sectors=%llu load=0x10000..0x9f000\n"
              "root: device_lba=%llu sectors=%llu mount=/disk0 init=/disk0/sbin/init\n"
              "modules: count=%u addr=%p\n"
              "cmdline: %s\n",
              g_bootinfo->version,
              (unsigned long long)g_bootinfo->boot_drive,
              g_bootinfo->flags,
              (unsigned long long)g_bootinfo->kernel_lba,
              (unsigned long long)g_bootinfo->kernel_sectors,
              (unsigned long long)g_bootinfo->root_lba,
              (unsigned long long)g_bootinfo->root_sectors,
              g_bootinfo->module_count,
              (void *)(uptr)g_bootinfo->modules_addr,
              bootinfo_cmdline(g_bootinfo));
}
