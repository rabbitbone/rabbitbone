#include <rabbitbone/bootinfo.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/panic.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/arch/idt.h>
#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/block.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/ahci.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/ramfs.h>
#include <rabbitbone/devfs.h>
#include <rabbitbone/ext4_vfs.h>
#include <rabbitbone/syscall.h>
#include <rabbitbone/task.h>
#include <rabbitbone/process.h>
#include <rabbitbone/scheduler.h>
#if RABBITBONE_EMBED_USERLAND
#include <rabbitbone/user_bins.h>
#endif
#include <rabbitbone/version.h>
#include <rabbitbone/tty.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/hpet.h>
#include <rabbitbone/smp.h>

#ifdef RABBITBONE_DEBUG_SHELL
extern void shell_run(void);
#endif
extern void rabbitbone_cpp_api_selftest(void);

static void kernel_mount_filesystems(void) {
    vfs_init();
    vfs_status_t st = ramfs_mount_boot();
    if (st != VFS_OK) PANIC("boot ramfs mount failed: %s", vfs_status_name(st));
    st = devfs_mount();
    if (st != VFS_OK) KLOG(LOG_WARN, "kernel", "devfs mount failed: %s", vfs_status_name(st));
#if RABBITBONE_EMBED_USERLAND
    user_bins_install();
#endif
    st = ext4_vfs_mount_first_linux_partition("/disk0");
    if (st != VFS_OK) {
        KLOG(LOG_WARN, "kernel", "no ext4 root mounted at /disk0: %s", vfs_status_name(st));
        return;
    }
    static const char *const userland_aliases[] = {
        "hello", "fscheck", "writetest", "badptr", "badpath", "statcheck", "procstat",
        "spawncheck", "schedcheck", "preemptcheck", "fdcheck", "isolate", "fdleak",
        "forkcheck", "heapcheck", "mmapcheck", "mmapfilecheck", "mmapsharedcheck",
        "procctl", "execcheck", "execfdcheck", "execfdchild", "execvecheck", "exectarget",
        "pipecheck", "fdremapcheck", "pollcheck", "stdcat", "termcheck", "regtrash",
        "sh", "rbsh", 0
    };
    for (unsigned int i = 0; userland_aliases[i]; ++i) {
        char link_path[64];
        char target_path[96];
        ksnprintf(link_path, sizeof(link_path), "/bin/%s", userland_aliases[i]);
        ksnprintf(target_path, sizeof(target_path), "/disk0/bin/%s", userland_aliases[i]);
        (void)vfs_symlink(target_path, link_path);
    }
    (void)vfs_symlink("/disk0/sbin/init", "/sbin/init");
}

void kernel_main(const rabbitbone_bootinfo_t *bootinfo) {
    console_init();
    log_init();
    KLOG(LOG_INFO, "kernel", RABBITBONE_KERNEL_BANNER);
    cpu_init();
    gdt_init();

    bool bootinfo_basic = bootinfo_basic_usable(bootinfo);
    memory_init(bootinfo_basic ? bootinfo : 0);
    vmm_init(1024ull * 1024ull * 1024ull);
    if (bootinfo_basic && (bootinfo->flags & RABBITBONE_BOOT_FLAG_UEFI) && !vga_use_boot_framebuffer_early(bootinfo)) {
        KLOG(LOG_WARN, "fbcon", "UEFI framebuffer unavailable; serial console remains active");
    }
    if (!bootinfo_validate(bootinfo)) {
        PANIC("invalid boot information block at %p: %s", bootinfo, bootinfo_validate_reason(bootinfo));
    }
    bootinfo_remember(bootinfo);
    bootramdisk_init(bootinfo);
    kmem_init();
    serial_enable_heap_ring();
    log_enable_heap_ring();
    if (!gdt_install_dynamic_stacks(64u * 1024u, 16u * 1024u)) {
        PANIC("failed to install heap-backed TSS stacks");
    }

    acpi_init();
    apic_init();
    hpet_init();
    smp_init_groundwork();

    pic_remap(32, 40);
    idt_init();
    pit_init(100);
    KLOG(LOG_INFO, "kernel", "starting secondary CPUs");
    smp_start_all_aps();
    KLOG(LOG_INFO, "kernel", "secondary CPU startup path returned");
    keyboard_init();
    tty_init();
    task_init();
    process_init();
    scheduler_init();
    (void)apic_local_timer_enable((u8)RABBITBONE_APIC_TIMER_VECTOR, RABBITBONE_APIC_TIMER_INITIAL_COUNT, true);
    pic_clear_mask(0);
    pic_clear_mask(1);
    cpu_sti();
    timer_init_sources();

    pci_init();
    ahci_init();
    ata_pio_init();
    block_log_devices();
    kernel_mount_filesystems();
    syscall_init();
    rabbitbone_cpp_api_selftest();

    KLOG(LOG_INFO, "kernel", "initialized: block_devices=%llu", (unsigned long long)block_count());

#ifdef RABBITBONE_DEBUG_SHELL
    shell_run();
#else
    const char *init_argv[] = { "/disk0/sbin/init" };
    u32 init_pid = 0;
    process_status_t init_status = process_spawn_async("/disk0/sbin/init", 1, init_argv, &init_pid);
#if RABBITBONE_EMBED_USERLAND
    if (init_status != PROC_OK) {
        const char *fallback_argv[] = { "/sbin/init" };
        init_status = process_spawn_async("/sbin/init", 1, fallback_argv, &init_pid);
    }
#endif
    if (init_status != PROC_OK) {
        PANIC("failed to start disk-backed /disk0/sbin/init: %s", process_status_name(init_status));
    }
    KLOG(LOG_INFO, "kernel", "started /disk0/sbin/init pid=%u", init_pid);
    process_scheduler_loop();
#endif
}
