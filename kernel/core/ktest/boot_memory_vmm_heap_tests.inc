static void test_boot_memory_vmm_heap(void) {
    suite_begin("bootinfo/memory/vmm/kmem");
    aurora_e820_entry_t entries[2];
    entries[0].base = 0x100000;
    entries[0].length = 0x100000;
    entries[0].type = AURORA_E820_USABLE;
    entries[0].acpi = 0;
    entries[1].base = 0x200000;
    entries[1].length = 0x100000;
    entries[1].type = 2;
    entries[1].acpi = 0;
    aurora_bootinfo_t bi;
    memset(&bi, 0, sizeof(bi));
    bi.magic = AURORA_BOOT_MAGIC;
    bi.version = AURORA_BOOT_VERSION;
    bi.e820_count = 2;
    bi.e820_addr = (u64)(uptr)entries;
    check(bootinfo_validate(&bi), "bootinfo validates synthetic block");
    check(bootinfo_e820(&bi, 0) == &entries[0] && bootinfo_e820(&bi, 1) == &entries[1], "bootinfo e820 accessor");
    bi.magic ^= 1u;
    check(!bootinfo_validate(&bi), "bootinfo rejects bad magic");

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
    check(aurora_rust_usercopy_selftest(), "Rust usercopy range/step arithmetic selftest");
    check(tty_selftest(), "TTY mode/info/readkey selftest");
    check(vmm_selftest(), "vmm map/translate/unmap 4K page");
    check(vmm_kernel_protection_selftest(), "vmm kernel image W^X/NX protection");
    suite_end();
}

