static void test_boot_memory_vmm_heap(void) {
    suite_begin("bootinfo/memory/vmm/kmem");
    rabbitbone_e820_entry_t entries[2];
    entries[0].base = 0x100000;
    entries[0].length = 0x100000;
    entries[0].type = RABBITBONE_E820_USABLE;
    entries[0].acpi = 0;
    entries[1].base = 0x200000;
    entries[1].length = 0x100000;
    entries[1].type = 2;
    entries[1].acpi = 0;
    rabbitbone_bootinfo_t bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic = RABBITBONE_BOOT_MAGIC;
    bi.version = RABBITBONE_BOOT_VERSION;
    bi.flags = RABBITBONE_BOOT_FLAG_BIOS_LEGACY;
    bi.e820_count = 2;
    bi.e820_addr = (u64)(uptr)entries;
    check(bootinfo_validate(&bi), "bootinfo validates synthetic block");
    check(bootinfo_basic_usable(&bi), "bootinfo basic probe accepts synthetic block");
    check(strcmp(bootinfo_validate_reason(&bi), "ok") == 0, "bootinfo validation reason reports ok");
    check(bootinfo_e820(&bi, 0) == &entries[0] && bootinfo_e820(&bi, 1) == &entries[1], "bootinfo e820 accessor");
    bi.magic ^= 1u;
    check(!bootinfo_validate(&bi), "bootinfo rejects bad magic");
    check(!bootinfo_basic_usable(&bi), "bootinfo basic probe rejects bad magic");
    bi.magic ^= 1u;
    bi.e820_count = 129u;
    check(!bootinfo_validate(&bi) && !bootinfo_basic_usable(&bi), "bootinfo rejects too many e820 entries");
    bi.e820_count = 2u;
    bi.flags = RABBITBONE_BOOT_FLAG_BIOS_LEGACY | RABBITBONE_BOOT_FLAG_UEFI;
    check(!bootinfo_validate(&bi), "bootinfo rejects conflicting boot source flags");
    bi.flags = 0x80000000u | RABBITBONE_BOOT_FLAG_BIOS_LEGACY;
    check(!bootinfo_validate(&bi), "bootinfo rejects unknown boot flags");
    bi.flags = RABBITBONE_BOOT_FLAG_LIVE_RAMDISK | RABBITBONE_BOOT_FLAG_UEFI;
    check(!bootinfo_validate(&bi), "bootinfo rejects live ramdisk without module");
    bi.flags = RABBITBONE_BOOT_FLAG_BIOS_LEGACY;
    char module_name[64];
    memset(module_name, 0, sizeof(module_name));
    memcpy(module_name, "root.img", 9u);
    rabbitbone_boot_module_t modules[1];
    modules[0].base = (u64)(uptr)entries;
    modules[0].size = sizeof(entries);
    modules[0].name_addr = (u64)(uptr)module_name;
    modules[0].reserved = 0;
    bi.module_count = 1u;
    bi.modules_addr = (u64)(uptr)modules;
    check(bootinfo_validate(&bi), "bootinfo validates bounded module metadata");
    modules[0].name_addr = 0xfffffffffffffff0ull;
    check(!bootinfo_validate(&bi), "bootinfo rejects corrupted module metadata");
    modules[0].name_addr = (u64)(uptr)module_name;
    bi.module_count = 0u;

    memory_stats_t ms;
    memory_get_stats(&ms);
    check(ms.frame_count > 0u && ms.free_frames > 0u, "physical memory allocator has free frames");
    check(memory_selftest(), "physical page alloc/free selftest");

    kmem_stats_t before;
    kmem_get_stats(&before);
    check(kmem_selftest(), "kernel heap alloc/free/realloc selftest");
    void *p = kmalloc(1234u);
    check(p != 0, "kmalloc direct allocation");
    if (p) {
        memset(p, 0x7e, 1234u);
        p = krealloc(p, 4096u);
        check(p != 0, "krealloc grows block");
        kfree(p);
    } else {
        skip("krealloc grows block");
    }
    kmem_stats_t after;
    kmem_get_stats(&after);
    check(after.heap_bytes >= before.heap_bytes && after.failed_allocations == before.failed_allocations, "kmem stats stable after selftest");

    vmm_stats_t vs;
    vmm_get_stats(&vs);
    check(vs.pml4_physical != 0 && vs.identity_bytes >= 64ull * 1024ull * 1024ull, "vmm initialized identity map");
    uptr phys = 0;
    u64 flags = 0;
    check(vmm_translate((uptr)&test_boot_memory_vmm_heap, &phys, &flags) && phys != 0 && (flags & VMM_PRESENT), "vmm translates kernel text address");
    check(cpu_nxe_enabled(), "CPU EFER.NXE enabled for NX user pages");
    check(cpu_sse_enabled(), "CPU SSE/SSE2 context enabled for Rust-generated code");
    check(rabbitbone_rust_usercopy_selftest(), "Rust usercopy range/step arithmetic selftest");
    check(tty_selftest(), "TTY mode/info/readkey selftest");
    check(vmm_selftest(), "vmm map/translate/unmap 4K page");
    check(vmm_kernel_protection_selftest(), "vmm kernel image W^X/NX protection");
    suite_end();
}

