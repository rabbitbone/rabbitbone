#include <aurora/bootinfo.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/arch/cpu.h>
#include <aurora/arch/idt.h>
#include <aurora/arch/gdt.h>
#include <aurora/arch/io.h>
#include <aurora/drivers.h>
#include <aurora/memory.h>
#include <aurora/vmm.h>
#include <aurora/kmem.h>
#include <aurora/block.h>
#include <aurora/vfs.h>
#include <aurora/ramfs.h>
#include <aurora/devfs.h>
#include <aurora/ext4_vfs.h>
#include <aurora/syscall.h>
#include <aurora/task.h>
#include <aurora/process.h>
#include <aurora/scheduler.h>
#include <aurora/user_bins.h>
#include <aurora/version.h>
#include <aurora/tty.h>
#include <aurora/libc.h>

#ifdef AURORA_DEBUG_SHELL
extern void shell_run(void);
#endif
extern void aurora_cpp_api_selftest(void);

static void kernel_mount_filesystems(void) {
    vfs_init();
    vfs_status_t st = ramfs_mount_boot();
    if (st != VFS_OK) PANIC("boot ramfs mount failed: %s", vfs_status_name(st));
    st = devfs_mount();
    if (st != VFS_OK) KLOG(LOG_WARN, "kernel", "devfs mount failed: %s", vfs_status_name(st));
    user_bins_install();
    st = ext4_vfs_mount_first_linux_partition("/disk0");
    if (st != VFS_OK) KLOG(LOG_WARN, "kernel", "no ext4 root mounted at /disk0: %s", vfs_status_name(st));
}

void kernel_main(const aurora_bootinfo_t *bootinfo) {
    console_init();
    log_init();
    KLOG(LOG_INFO, "kernel", AURORA_KERNEL_BANNER);
    cpu_init();
    gdt_init();

    if (!bootinfo_validate(bootinfo)) {
        PANIC("invalid boot information block at %p", bootinfo);
    }
    bootinfo_dump(bootinfo);
    memory_init(bootinfo);
    vmm_init(1024ull * 1024ull * 1024ull);
    kmem_init();
    if (!gdt_install_dynamic_stacks(64u * 1024u, 8u * 1024u)) {
        PANIC("failed to install heap-backed TSS stacks");
    }

    pic_remap(32, 40);
    idt_init();
    pit_init(100);
    keyboard_init();
    tty_init();
    task_init();
    process_init();
    scheduler_init();
    pic_clear_mask(0);
    pic_clear_mask(1);
    cpu_sti();

    ata_pio_init();
    kernel_mount_filesystems();
    syscall_init();
    aurora_cpp_api_selftest();

    KLOG(LOG_INFO, "kernel", "initialized: block_devices=%llu", (unsigned long long)block_count());

#ifdef AURORA_DEBUG_SHELL
    shell_run();
#else
    const char *init_argv[] = { "/sbin/init" };
    u32 init_pid = 0;
    process_status_t init_status = process_spawn_async("/sbin/init", 1, init_argv, &init_pid);
    if (init_status != PROC_OK) {
        PANIC("failed to start /sbin/init: %s", process_status_name(init_status));
    }
    KLOG(LOG_INFO, "kernel", "started /sbin/init pid=%u", init_pid);
    process_scheduler_loop();
#endif
}
