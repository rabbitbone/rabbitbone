#include <rabbitbone/bootinfo.h>
#include <rabbitbone/console.h>
#include <rabbitbone/libc.h>

#define BOOTINFO_E820_MAX 128u
#define BOOTINFO_NAME_MAX 64u
#define BOOTINFO_MODULE_MAX 16u
#define BOOTINFO_IDENTITY_LIMIT (1024ull * 1024ull * 1024ull)
#define BOOTINFO_POINTER_MIN 0x1000ull
#define BOOTINFO_KNOWN_FLAGS (RABBITBONE_BOOT_FLAG_BIOS_LEGACY | RABBITBONE_BOOT_FLAG_UEFI | RABBITBONE_BOOT_FLAG_LIVE_RAMDISK)

static const rabbitbone_bootinfo_t *g_bootinfo;

static bool bootinfo_ptr_ok(const rabbitbone_bootinfo_t *info) {
#if defined(RABBITBONE_HOST_TEST)
    return info != 0;
#else
    uptr p = (uptr)info;
    return p >= BOOTINFO_POINTER_MIN && p <= (uptr)(BOOTINFO_IDENTITY_LIMIT - sizeof(*info));
#endif
}

static bool range_ok(u64 base, u64 bytes) {
    if (bytes == 0) return true;
    u64 end = 0;
    if (__builtin_add_overflow(base, bytes, &end)) return false;
#if !defined(RABBITBONE_HOST_TEST)
    if (base == 0 || end > BOOTINFO_IDENTITY_LIMIT) return false;
#else
    if (base == 0) return false;
#endif
    return true;
}

static bool bounded_cstr_ok(u64 addr, u32 max_len) {
    if (addr == 0 || max_len == 0 || !range_ok(addr, max_len)) return false;
    const char *s = (const char *)(uptr)addr;
    for (u32 i = 0; i < max_len; ++i) {
        if (s[i] == 0) return true;
    }
    return false;
}

static usize bounded_cstr_len(const char *s, usize max_len) {
    if (!s) return 0;
    usize n = 0;
    while (n < max_len && s[n]) ++n;
    return n;
}

static bool bootinfo_flags_ok(u32 flags) {
    if ((flags & ~BOOTINFO_KNOWN_FLAGS) != 0) return false;
    if ((flags & (RABBITBONE_BOOT_FLAG_BIOS_LEGACY | RABBITBONE_BOOT_FLAG_UEFI)) == 0) return false;
    if ((flags & RABBITBONE_BOOT_FLAG_BIOS_LEGACY) && (flags & RABBITBONE_BOOT_FLAG_UEFI)) return false;
    return true;
}

bool bootinfo_basic_usable(const rabbitbone_bootinfo_t *info) {
    if (!info || !bootinfo_ptr_ok(info)) return false;
    if (info->magic != RABBITBONE_BOOT_MAGIC || info->version != RABBITBONE_BOOT_VERSION) return false;
    if (!bootinfo_flags_ok(info->flags)) return false;
    if (info->e820_count > BOOTINFO_E820_MAX || info->module_count > BOOTINFO_MODULE_MAX) return false;
    if (info->cmdline_size > RABBITBONE_BOOT_CMDLINE_MAX) return false;
    return true;
}

const char *bootinfo_validate_reason(const rabbitbone_bootinfo_t *info) {
    if (!info) return "null bootinfo pointer";
    if (!bootinfo_ptr_ok(info)) return "bootinfo pointer outside early identity map";
    if (info->magic != RABBITBONE_BOOT_MAGIC) return "bad bootinfo magic";
    if (info->version != RABBITBONE_BOOT_VERSION) return "unsupported bootinfo version";
    if ((info->flags & ~BOOTINFO_KNOWN_FLAGS) != 0) return "unknown boot flags";
    if ((info->flags & (RABBITBONE_BOOT_FLAG_BIOS_LEGACY | RABBITBONE_BOOT_FLAG_UEFI)) == 0) return "missing boot source flag";
    if ((info->flags & RABBITBONE_BOOT_FLAG_BIOS_LEGACY) && (info->flags & RABBITBONE_BOOT_FLAG_UEFI)) return "conflicting boot source flags";
    if ((info->flags & RABBITBONE_BOOT_FLAG_LIVE_RAMDISK) && info->module_count == 0) return "live ramdisk flag without module";
    if (info->e820_count > BOOTINFO_E820_MAX) return "too many e820 entries";
    if (info->module_count > BOOTINFO_MODULE_MAX) return "too many boot modules";
    if (info->e820_count != 0) {
        u64 bytes = 0;
        if (__builtin_mul_overflow((u64)info->e820_count, (u64)sizeof(rabbitbone_e820_entry_t), &bytes)) return "e820 table size overflow";
        if (!range_ok(info->e820_addr, bytes)) return "e820 table outside early identity map";
        const rabbitbone_e820_entry_t *entries = (const rabbitbone_e820_entry_t *)(uptr)info->e820_addr;
        for (u16 i = 0; i < info->e820_count; ++i) {
            u64 end = 0;
            if (entries[i].length && __builtin_add_overflow(entries[i].base, entries[i].length, &end)) return "e820 range overflow";
        }
    }
    if (info->module_count != 0) {
        u64 bytes = 0;
        if (__builtin_mul_overflow((u64)info->module_count, (u64)sizeof(rabbitbone_boot_module_t), &bytes)) return "module table size overflow";
        if (!range_ok(info->modules_addr, bytes)) return "module table outside early identity map";
        const rabbitbone_boot_module_t *mods = (const rabbitbone_boot_module_t *)(uptr)info->modules_addr;
        for (u16 i = 0; i < info->module_count; ++i) {
            if (mods[i].size && !range_ok(mods[i].base, mods[i].size)) return "module payload outside early identity map";
            if (mods[i].name_addr && !bounded_cstr_ok(mods[i].name_addr, BOOTINFO_NAME_MAX)) return "module name is not a bounded string";
        }
    }
    if (info->cmdline_size > RABBITBONE_BOOT_CMDLINE_MAX) return "cmdline too large";
    if (info->cmdline_size != 0) {
        if (!range_ok(info->cmdline_addr, (u64)info->cmdline_size)) return "cmdline outside early identity map";
        const char *cmd = (const char *)(uptr)info->cmdline_addr;
        if (cmd[info->cmdline_size - 1u] != 0) return "cmdline is not NUL-terminated";
    }
    return "ok";
}

bool bootinfo_validate(const rabbitbone_bootinfo_t *info) {
    return bootinfo_validate_reason(info)[0] == 'o';
}

void bootinfo_remember(const rabbitbone_bootinfo_t *info) {
    if (bootinfo_validate(info)) g_bootinfo = info;
}

u64 bootinfo_acpi_rsdp(void) {
    if (!bootinfo_basic_usable(g_bootinfo)) return 0;
    return RABBITBONE_BOOT_ACPI_RSDP(g_bootinfo);
}

const rabbitbone_e820_entry_t *bootinfo_e820(const rabbitbone_bootinfo_t *info, u16 index) {
    if (!bootinfo_validate(info) || index >= info->e820_count) return 0;
    const rabbitbone_e820_entry_t *entries = (const rabbitbone_e820_entry_t *)(uptr)info->e820_addr;
    return &entries[index];
}

const char *bootinfo_cmdline(const rabbitbone_bootinfo_t *info) {
    if (!bootinfo_validate(info) || info->cmdline_size == 0) return "";
    return (const char *)(uptr)info->cmdline_addr;
}

void bootinfo_dump(const rabbitbone_bootinfo_t *info) {
    if (!bootinfo_validate(info)) {
        kprintf("bootinfo: invalid: %s\n", bootinfo_validate_reason(info));
        return;
    }
    const char *source = (info->flags & RABBITBONE_BOOT_FLAG_UEFI) ? "uefi" : "bios-int13";
    kprintf("boot: v=%u source=%s drive=%llu kernel_lba=%llu kernel_sectors=%llu root_lba=%llu root_sectors=%llu modules=%u flags=0x%x e820=%u\n",
            info->version,
            source,
            (unsigned long long)info->boot_drive,
            (unsigned long long)info->kernel_lba,
            (unsigned long long)info->kernel_sectors,
            (unsigned long long)info->root_lba,
            (unsigned long long)info->root_sectors,
            info->module_count,
            info->flags,
            info->e820_count);
    if (info->cmdline_size) kprintf("boot: cmdline='%s'\n", bootinfo_cmdline(info));
    if (info->module_count && info->modules_addr) {
        const rabbitbone_boot_module_t *mods = (const rabbitbone_boot_module_t *)(uptr)info->modules_addr;
        for (u16 i = 0; i < info->module_count; ++i) {
            const char *name = mods[i].name_addr ? (const char *)(uptr)mods[i].name_addr : "module";
            kprintf("  module[%u] base=%llx size=%llx name=", i,
                    (unsigned long long)mods[i].base,
                    (unsigned long long)mods[i].size);
            console_write_n(name, bounded_cstr_len(name, BOOTINFO_NAME_MAX));
            console_write("\n");
        }
    }
    for (u16 i = 0; i < info->e820_count; ++i) {
        const rabbitbone_e820_entry_t *e = bootinfo_e820(info, i);
        kprintf("  [%u] base=%llx len=%llx type=%u\n", i,
                (unsigned long long)e->base,
                (unsigned long long)e->length,
                e->type);
    }
}

void bootinfo_format_status(char *out, usize cap) {
    if (!out || cap == 0) return;
    if (!bootinfo_validate(g_bootinfo)) {
        ksnprintf(out, cap, "boot: unavailable (%s)\n", bootinfo_validate_reason(g_bootinfo));
        return;
    }
    ksnprintf(out, cap,
              "boot: contract=v%u source=%s drive=%llu flags=0x%x\n"
              "kernel: lba=%llu sectors=%llu load=0x10000..0x9f000\n"
              "root: device_lba=%llu sectors=%llu mount=/disk0 init=/disk0/sbin/init\n"
              "modules: count=%u addr=%p\n"
              "cmdline: %s\n"
              "acpi_rsdp: %p\n",
              g_bootinfo->version,
              (g_bootinfo->flags & RABBITBONE_BOOT_FLAG_UEFI) ? "uefi-live-iso" : "bios-int13",
              (unsigned long long)g_bootinfo->boot_drive,
              g_bootinfo->flags,
              (unsigned long long)g_bootinfo->kernel_lba,
              (unsigned long long)g_bootinfo->kernel_sectors,
              (unsigned long long)g_bootinfo->root_lba,
              (unsigned long long)g_bootinfo->root_sectors,
              g_bootinfo->module_count,
              (void *)(uptr)g_bootinfo->modules_addr,
              bootinfo_cmdline(g_bootinfo),
              (void *)(uptr)bootinfo_acpi_rsdp());
}
