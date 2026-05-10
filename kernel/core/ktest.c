#include <aurora/ktest.h>
#include <aurora/bitmap.h>
#include <aurora/block.h>
#include <aurora/bootinfo.h>
#include <aurora/console.h>
#include <aurora/crc32.h>
#include <aurora/drivers.h>
#include <aurora/elf64.h>
#include <aurora/ext4.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/mbr.h>
#include <aurora/memory.h>
#include <aurora/path.h>
#include <aurora/ringbuf.h>
#include <aurora/syscall.h>
#include <aurora/task.h>
#include <aurora/scheduler.h>
#include <aurora/tarfs.h>
#include <aurora/timer.h>
#include <aurora/vfs.h>
#include <aurora/vmm.h>
#include <aurora/arch/gdt.h>
#include <aurora/arch/cpu.h>
#include <aurora/process.h>
#include <aurora/user_bins.h>
#include <aurora/rust.h>
#include <aurora/version.h>

static ktest_totals_t totals;
static const char *current_suite;
static u32 suite_failed;
static u32 suite_passed;
static u32 suite_skipped;

static void suite_begin(const char *name) {
    current_suite = name;
    suite_failed = 0;
    suite_passed = 0;
    suite_skipped = 0;
    ++totals.suites;
    kprintf("\n[ suite ] %s\n", name);
}

static void suite_end(void) {
    kprintf("[ result] %s: passed=%u failed=%u skipped=%u\n", current_suite, suite_passed, suite_failed, suite_skipped);
}

static void pass(const char *name) {
    ++totals.passed;
    ++suite_passed;
    kprintf("[ ok    ] %s\n", name);
}

static void fail(const char *name) {
    ++totals.failed;
    ++suite_failed;
    kprintf("[ fail  ] %s\n", name);
}

static void skip(const char *name) {
    ++totals.skipped;
    ++suite_skipped;
    kprintf("[ skip  ] %s\n", name);
}

static void check(bool expr, const char *name) {
    if (expr) pass(name);
    else fail(name);
}

typedef struct seen_name_ctx {
    const char *name;
    bool seen;
} seen_name_ctx_t;

static bool list_seen_name(const vfs_dirent_t *e, void *ctx) {
    seen_name_ctx_t *s = (seen_name_ctx_t *)ctx;
    if (strcmp(e->name, s->name) == 0) { s->seen = true; return false; }
    return true;
}

static bool list_count_cb(const vfs_dirent_t *e, void *ctx) {
    (void)e;
    u32 *n = (u32 *)ctx;
    ++*n;
    return true;
}

static bool ext4_seen_name(const ext4_dirent_t *e, void *ctx) {
    seen_name_ctx_t *s = (seen_name_ctx_t *)ctx;
    if (strcmp(e->name, s->name) == 0) { s->seen = true; return false; }
    return true;
}

static block_status_t ktest_block_read_should_not_run(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    (void)dev; (void)lba; (void)count; (void)buffer;
    return BLOCK_ERR_IO;
}

static void test_libc_bitmap_ring_crc(void) {
    suite_begin("libc/bitmap/ringbuf/crc32");
    char a[16];
    char b[16];
    memset(a, 0x41, sizeof(a));
    check(a[0] == 'A' && a[15] == 'A', "memset fills full range");
    memcpy(b, a, sizeof(a));
    check(memcmp(a, b, sizeof(a)) == 0, "memcpy/memcmp roundtrip");
    memcpy(a, "0123456789", 11u);
    memmove(a + 2, a, 8u);
    check(strcmp(a, "0101234567") == 0, "memmove overlapping right shift");
    check(strlen("aurora") == 6u && strnlen("aurora", 3u) == 3u, "strlen/strnlen");
    check(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0, "strcmp ordering");
    check(strncmp("abcdef", "abcXYZ", 3u) == 0, "strncmp prefix");
    check(strchr("abc", 'b') && !strchr("abc", 'z'), "strchr hit/miss");
    const char *end = 0;
    check(strtou64("0x2a!", &end, 0) == 42u && end && *end == '!', "strtou64 base auto");

    u64 storage[2];
    bitmap_t bm;
    bitmap_init(&bm, storage, 128u);
    usize bit = 0;
    check(bitmap_find_first_clear(&bm, &bit) && bit == 0u, "bitmap first clear");
    bitmap_set(&bm, 0u);
    bitmap_set(&bm, 65u);
    check(bitmap_test(&bm, 0u) && bitmap_test(&bm, 65u), "bitmap set/test across words");
    check(bitmap_count_set(&bm) == 2u, "bitmap count set");
    bitmap_clear(&bm, 0u);
    check(!bitmap_test(&bm, 0u) && bitmap_find_first_set(&bm, &bit) && bit == 65u, "bitmap clear/first set");

    check(ringbuf_selftest(), "ringbuf wraparound/read/peek");
    check(crc32_selftest(), "crc32 reference vectors");
    suite_end();
}

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
    check(vmm_selftest(), "vmm map/translate/unmap 4K page");
    suite_end();
}

static void test_vfs_ramfs_devfs(void) {
    suite_begin("vfs/ramfs/devfs");
    vfs_stat_t st;
    check(vfs_rust_route_selftest(), "Rust VFS route selftest");
    check(aurora_rust_path_policy_selftest(), "Rust VFS path policy selftest");
    check(vfs_stat("/", &st) == VFS_OK && st.type == VFS_NODE_DIR, "vfs stat root");
    check(vfs_stat("/etc/motd", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 0u, "vfs stat /etc/motd");
    check(vfs_stat("relative/path", &st) == VFS_ERR_INVAL, "vfs rejects relative path before routing");
    check(vfs_stat("/bad\\path", &st) == VFS_ERR_INVAL, "vfs rejects disallowed path bytes");
    char buf[128];
    usize got = 0;
    memset(buf, 0, sizeof(buf));
    check(vfs_read("/etc/motd", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got > 0u && strstr(buf, "AuroraOS") != 0, "vfs read /etc/motd");
    seen_name_ctx_t motd = { "motd", false };
    check(vfs_list("/etc", list_seen_name, &motd) == VFS_OK && motd.seen, "vfs list /etc finds motd");
    u32 count = 0;
    check(vfs_list("/", list_count_cb, &count) == VFS_OK && count >= 3u, "vfs root lists mounted directories");

    const char *dir = "/tmp/ktest-vfs";
    const char *file = "/tmp/ktest-vfs/file.txt";
    (void)vfs_unlink(file);
    (void)vfs_unlink(dir);
    check(vfs_mkdir(dir) == VFS_OK, "ramfs mkdir");
    static const char seed[] = "abc";
    check(vfs_create(file, seed, sizeof(seed) - 1u) == VFS_OK, "ramfs create file");
    static const char patch[] = "XYZ";
    usize wrote = 0;
    check(vfs_write(file, 1u, patch, sizeof(patch) - 1u, &wrote) == VFS_OK && wrote == sizeof(patch) - 1u, "ramfs write with growth");
    wrote = 0;
    check(vfs_write(file, 0x7fffffffffffffffull, patch, sizeof(patch) - 1u, &wrote) == VFS_ERR_INVAL && wrote == 0, "ramfs rejects huge offset write without growth loop");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_read(file, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && strcmp(buf, "aXYZ") == 0, "ramfs read modified file");
    check(vfs_unlink(file) == VFS_OK, "ramfs unlink file");
    check(vfs_unlink(dir) == VFS_OK, "ramfs unlink empty dir");

    u8 zeros[32];
    memset(zeros, 0x55, sizeof(zeros));
    got = 0;
    check(vfs_read("/dev/zero", 0, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros) && zeros[0] == 0 && zeros[31] == 0, "devfs /dev/zero");
    wrote = 0;
    check(vfs_write("/dev/null", 0, "discard", 7u, &wrote) == VFS_OK && wrote == 7u, "devfs /dev/null write");
    u8 rnd[16];
    memset(rnd, 0, sizeof(rnd));
    got = 0;
    check(vfs_read("/dev/random", 0, rnd, sizeof(rnd), &got) == VFS_OK && got == sizeof(rnd), "devfs /dev/random read");
    suite_end();
}

static void tar_octal(char *dst, usize width, usize value) {
    memset(dst, '0', width);
    dst[width - 1u] = 0;
    if (width < 2u) return;
    for (usize pos = width - 2u;; --pos) {
        dst[pos] = (char)('0' + (value & 7u));
        value >>= 3u;
        if (pos == 0 || value == 0) break;
    }
}

static void tar_header_write(u8 *block, const char *name, const char *data, usize size) {
    memset(block, 0, 512u);
    strncpy((char *)block + 0, name, 99u);
    tar_octal((char *)block + 100, 8u, 0644u);
    tar_octal((char *)block + 108, 8u, 0u);
    tar_octal((char *)block + 116, 8u, 0u);
    tar_octal((char *)block + 124, 12u, size);
    tar_octal((char *)block + 136, 12u, 1u);
    memset(block + 148, ' ', 8u);
    block[156] = '0';
    memcpy(block + 257, "ustar", 5u);
    memcpy(block + 263, "00", 2u);
    usize sum = 0;
    for (usize i = 0; i < 512u; ++i) sum += block[i];
    tar_octal((char *)block + 148, 8u, sum);
    block[155] = ' ';
    memcpy(block + 512u, data, size);
}

static void test_tarfs_module(void) {
    suite_begin("tarfs module");
    static const char data[] = "tarfs payload\n";
    u8 *image = (u8 *)kcalloc(1, 2048u);
    if (!image) {
        fail("allocate tar image");
        suite_end();
        return;
    }
    tar_header_write(image, "boot/test.txt", data, sizeof(data) - 1u);
    tarfs_t *fs = tarfs_open(image, 2048u);
    check(fs != 0, "tarfs opens ustar image");
    if (fs) {
        vfs_status_t ms = vfs_mount("/tar-test", "tarfs", tarfs_ops(), fs, false);
        check(ms == VFS_OK, "tarfs mounts through VFS");
        if (ms == VFS_OK) {
            vfs_stat_t st;
            check(vfs_stat("/tar-test/boot", &st) == VFS_OK && st.type == VFS_NODE_DIR, "tarfs implicit directory stat");
            char buf[32];
            usize got = 0;
            memset(buf, 0, sizeof(buf));
            check(vfs_read("/tar-test/boot/test.txt", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && strcmp(buf, data) == 0, "tarfs file read through VFS");
            seen_name_ctx_t seen = { "test.txt", false };
            check(vfs_list("/tar-test/boot", list_seen_name, &seen) == VFS_OK && seen.seen, "tarfs directory list");
            check(vfs_write("/tar-test/boot/test.txt", 0, "x", 1u, 0) == VFS_ERR_PERM, "tarfs is read-only in VFS");
            check(vfs_unmount("/tar-test") == VFS_OK, "tarfs unmount");
        }
        tarfs_destroy(fs);
    }
    u8 *bad = (u8 *)kcalloc(1, 1024u);
    if (bad) {
        memcpy(bad, image, 1024u);
        memset(bad + 148, '0', 8u);
        check(tarfs_open(bad, 1024u) == 0, "tarfs rejects zero checksum header");
        memcpy(bad, image, 1024u);
        tar_octal((char *)bad + 124, 12u, 900u);
        memset(bad + 148, ' ', 8u);
        usize sum = 0;
        for (usize i = 0; i < 512u; ++i) sum += bad[i];
        tar_octal((char *)bad + 148, 8u, sum);
        bad[155] = ' ';
        check(tarfs_open(bad, 1024u) == 0, "tarfs rejects payload beyond image");
        kfree(bad);
    } else {
        skip("tarfs malformed image tests");
        skip("tarfs malformed image tests");
    }
    kfree(image);
    suite_end();
}

static void test_block_mbr_ext4(void) {
    suite_begin("block/mbr/ext4/disk0");
    block_device_t fake;
    memset(&fake, 0, sizeof(fake));
    fake.sector_count = 16u;
    fake.sector_size = BLOCKDEV_SECTOR_SIZE;
    fake.read = ktest_block_read_should_not_run;
    u8 fake_sector[512];
    check(block_read(&fake, 15u, 2u, fake_sector) == BLOCK_ERR_RANGE && block_read(&fake, 0xffffffffffffffffull, 2u, fake_sector) == BLOCK_ERR_RANGE, "block_read rejects overflow ranges before driver");
    mbr_partition_t bad_part;
    memset(&bad_part, 0, sizeof(bad_part));
    bad_part.type = 0x83;
    bad_part.lba_first = 15u;
    bad_part.sector_count = 2u;
    check(!mbr_partition_valid(&fake, &bad_part), "MBR partition range validator rejects overflow partition");
    usize n = block_count();
    check(n > 0u, "ATA block device discovered");
    block_device_t *dev = block_get(0);
    if (!dev) {
        skip("MBR read");
        skip("EXT4 raw mount");
        skip("VFS /disk0 read");
        suite_end();
        return;
    }
    check(dev->sector_size == BLOCKDEV_SECTOR_SIZE && dev->sector_count > 0u, "block device geometry sane");
    u8 sector[512];
    block_status_t br = block_read(dev, 0, 1, sector);
    if (br != BLOCK_OK) kprintf("[ detail] block_read(%s,lba=0,count=1)=%s\n", dev->name, block_status_name(br));
    else if (sector[510] != 0x55 || sector[511] != 0xaa) kprintf("[ detail] MBR signature bytes=%02x %02x\n", sector[510], sector[511]);
    check(br == BLOCK_OK && sector[510] == 0x55 && sector[511] == 0xaa, "block read MBR sector");
    mbr_table_t mbr;
    bool mbr_ok = mbr_read(dev, &mbr);
    if (!mbr_ok) kprintf("[ detail] mbr_read(%s) failed after direct read status=%s\n", dev->name, block_status_name(br));
    check(mbr_ok, "MBR parser accepts boot disk");
    const mbr_partition_t *part = mbr_ok ? mbr_find_linux(&mbr) : 0;
    check(part && part->lba_first >= 2048u && part->sector_count > 4096u, "MBR has Linux EXT partition");
    if (part) {
        ext4_mount_t mnt;
        ext4_status_t est = ext4_mount(dev, part->lba_first, &mnt);
        check(est == EXT4_OK, "EXT4 raw mount first Linux partition");
        if (est == EXT4_OK) {
            ext4_inode_disk_t root;
            check(ext4_read_inode(&mnt, EXT4_ROOT_INO, &root) == EXT4_OK && ext4_inode_is_dir(&root), "EXT4 root inode is directory");
            seen_name_ctx_t hello_seen = { "hello.txt", false };
            check(ext4_list_dir(&mnt, &root, ext4_seen_name, &hello_seen) == EXT4_OK && hello_seen.seen, "EXT4 root directory iteration finds hello.txt");
            ext4_inode_disk_t hello;
            u32 ino = 0;
            check(ext4_lookup_path(&mnt, "/hello.txt", &hello, &ino) == EXT4_OK && ino != 0 && ext4_inode_is_regular(&hello), "EXT4 path lookup /hello.txt");
            char text[96];
            usize rd = 0;
            memset(text, 0, sizeof(text));
            check(ext4_read_file(&mnt, &hello, 0, text, sizeof(text) - 1u, &rd) == EXT4_OK && rd > 0u && strstr(text, "AuroraOS") != 0, "EXT4 read regular file");
        }
    }
    char text[96];
    memset(text, 0, sizeof(text));
    usize got = 0;
    check(vfs_read("/disk0/hello.txt", 0, text, sizeof(text) - 1u, &got) == VFS_OK && got > 0u && strstr(text, "AuroraOS") != 0, "VFS read /disk0/hello.txt through ext4 adapter");
    suite_end();
}

static void test_syscall_task_timer_elf(void) {
    suite_begin("syscall/task/timer/elf/log");
    syscall_result_t r = syscall_dispatch(AURORA_SYS_VERSION, 0, 0, 0, 0, 0, 0);
    check(r.error == 0 && r.value == (i64)AURORA_SYSCALL_ABI_VERSION, "Rust syscall ABI version matches central kernel version");
    check(aurora_rust_syscall_selftest(), "Rust syscall decoder/validator selftest");
    r = syscall_dispatch(AURORA_SYS_CLOSE, 0, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall rejects invalid handle before C backend");
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)"relative/path", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust path policy rejects relative syscall path");
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)"/tmp/too-large", (u64)(uptr)"x", 65537u, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall caps create payload size");
    r = syscall_dispatch(AURORA_SYS_STAT, (u64)(uptr)"/etc/motd", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall requires stat output pointer");
    check(syscall_selftest(), "Rust-dispatched syscall filesystem/process-control selftest");
    check(strcmp(syscall_name(AURORA_SYS_WRITE), "write") == 0 && strcmp(syscall_name(AURORA_SYS_GETPID), "getpid") == 0 && strcmp(syscall_name(AURORA_SYS_PROCINFO), "procinfo") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWN), "spawn") == 0 && strcmp(syscall_name(AURORA_SYS_WAIT), "wait") == 0 && strcmp(syscall_name(AURORA_SYS_YIELD), "yield") == 0 && strcmp(syscall_name(AURORA_SYS_SLEEP), "sleep") == 0 && strcmp(syscall_name(AURORA_SYS_SCHEDINFO), "schedinfo") == 0 && strcmp(syscall_name(AURORA_SYS_DUP), "dup") == 0 && strcmp(syscall_name(AURORA_SYS_TELL), "tell") == 0 && strcmp(syscall_name(AURORA_SYS_FSTAT), "fstat") == 0 && strcmp(syscall_name(AURORA_SYS_FDINFO), "fdinfo") == 0 && strcmp(syscall_name(AURORA_SYS_READDIR), "readdir") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWNV), "spawnv") == 0 && strcmp(syscall_name(AURORA_SYS_PREEMPTINFO), "preemptinfo") == 0 && strcmp(syscall_name(999), "unknown") == 0, "syscall name table");
    r = syscall_dispatch(AURORA_SYS_TICKS, 0, 0, 0, 0, 0, 0);
    check(r.error == 0 && r.value >= 0, "syscall ticks");

    check(task_selftest(), "cooperative kernel task scheduler selftest");
    check(scheduler_selftest(), "process scheduler queue/wait/accounting selftest");
    task_init();
    task_stats_t ts;
    task_get_stats(&ts);
    check(ts.used_slots == 0 && ts.next_pid == 1u, "task table reset after selftest");

    check(timer_selftest(), "PIT timer advances while interrupts are enabled");
    check(strcmp(log_level_name(LOG_INFO), "INFO") == 0 && strcmp(log_level_name(LOG_FATAL), "FATAL") == 0, "log level names");
    KLOG(LOG_INFO, "ktest", "log write path exercised");

    elf64_ehdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.e_ident[0] = 0x7f;
    hdr.e_ident[1] = 'E';
    hdr.e_ident[2] = 'L';
    hdr.e_ident[3] = 'F';
    hdr.e_ident[4] = 2;
    hdr.e_ident[5] = 1;
    hdr.e_ident[6] = 1;
    hdr.e_type = ELF64_ET_EXEC;
    hdr.e_machine = ELF64_EM_X86_64;
    hdr.e_version = 1;
    hdr.e_entry = 0x400000;
    hdr.e_phoff = sizeof(elf64_ehdr_t);
    hdr.e_ehsize = sizeof(elf64_ehdr_t);
    hdr.e_phentsize = sizeof(elf64_phdr_t);
    hdr.e_phnum = 1;
    check(elf64_validate_header(&hdr, sizeof(hdr) + sizeof(elf64_phdr_t)) == ELF_OK, "ELF64 validates minimal amd64 executable header with program header");
    hdr.e_phnum = 0;
    check(elf64_validate_header(&hdr, sizeof(hdr)) == ELF_ERR_FORMAT, "ELF64 rejects executable header without program headers");
    hdr.e_phnum = 1;
    hdr.e_machine = 3;
    check(elf64_validate_header(&hdr, sizeof(hdr) + sizeof(elf64_phdr_t)) == ELF_ERR_UNSUPPORTED, "ELF64 rejects non-amd64 machine");
    suite_end();
}


static void detail_process_result(const char *label, process_status_t st, const process_result_t *r) {
    if (!r) return;
    kprintf("[ detail] %s status=%s exit=%d faulted=%u pml4=%p asid=%llu entry=%p stack_top=%p pages=%llu",
            label, process_status_name(st), r->exit_code, r->faulted ? 1u : 0u,
            (void *)(uptr)r->address_space, (unsigned long long)r->address_space_generation,
            (void *)(uptr)r->entry, (void *)(uptr)r->user_stack_top,
            (unsigned long long)r->mapped_pages);
    if (r->faulted) {
        kprintf(" vector=%llu rip=%p addr=%p",
                (unsigned long long)r->fault_vector, (void *)(uptr)r->fault_rip,
                (void *)(uptr)r->fault_addr);
    }
    kprintf("\n");
}

static void test_user_processes(void) {
    suite_begin("gdt/userbin/ring3-process");
    check(gdt_selftest(), "GDT has user segments and TSS kernel stack");
    check(user_bins_selftest(), "embedded /bin ELF payloads installed in VFS");
    vfs_stat_t st;
    check(vfs_stat("/bin/hello", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/hello user ELF");
    check(vfs_stat("/bin/regtrash", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/regtrash ABI stress ELF");
    check(vfs_stat("/bin/badptr", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/badptr pointer hardening ELF");
    check(vfs_stat("/bin/badpath", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/badpath path-policy ELF");
    check(vfs_stat("/bin/statcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/statcheck syscall-surface ELF");
    check(vfs_stat("/bin/procstat", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/procstat process-info ELF");
    check(vfs_stat("/bin/spawncheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/spawncheck process-control ELF");
    check(vfs_stat("/bin/schedcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/schedcheck scheduler syscall ELF");
    check(vfs_stat("/bin/preemptcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/preemptcheck preempt syscall ELF");
    check(vfs_stat("/bin/fdcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdcheck fd syscall ELF");
    check(vfs_stat("/bin/isolate", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/isolate address-space ELF");
    check(vfs_stat("/bin/fdleak", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdleak fd-table ELF");
    check(vfs_stat("/bin/forkcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/forkcheck fork/wait ELF");
    check(vfs_stat("/bin/procctl", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/procctl async spawn/wait ELF");
    check(vfs_stat("/bin/execcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execcheck execv ELF");
    elf_loaded_image_t img;
    elf_status_t es = elf64_load_from_vfs("/bin/hello", &img);
    check(es == ELF_OK && img.segment_count > 0u && img.entry >= 0x0000010000000000ull, "ELF loader parses /bin/hello");
    if (es == ELF_OK) elf64_free_image(&img);

    const char *hello_argv[] = { "/bin/hello" };
    process_result_t r;
    process_status_t ps = process_exec("/bin/hello", 1, hello_argv, &r);
    bool hello_ok = ps == PROC_OK && !r.faulted && r.exit_code == 7;
    check(hello_ok, "ring3 /bin/hello exits through int80 SYS_EXIT");
    if (!hello_ok) detail_process_result("/bin/hello", ps, &r);

    const char *reg_argv[] = { "/bin/regtrash" };
    ps = process_exec("/bin/regtrash", 1, reg_argv, &r);
    bool reg_ok = ps == PROC_OK && !r.faulted && r.exit_code == 23;
    check(reg_ok, "ring3 /bin/regtrash cannot corrupt kernel callee-saved registers");
    if (!reg_ok) detail_process_result("/bin/regtrash", ps, &r);

    const char *bad_argv[] = { "/bin/badptr" };
    ps = process_exec("/bin/badptr", 1, bad_argv, &r);
    bool bad_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(bad_ok, "ring3 bad user pointers return syscall errors without kernel fault");
    if (!bad_ok) detail_process_result("/bin/badptr", ps, &r);

    const char *badpath_argv[] = { "/bin/badpath" };
    ps = process_exec("/bin/badpath", 1, badpath_argv, &r);
    bool badpath_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(badpath_ok, "ring3 /bin/badpath exercises Rust path-policy rejection");
    if (!badpath_ok) detail_process_result("/bin/badpath", ps, &r);

    const char *stat_argv[] = { "/bin/statcheck" };
    ps = process_exec("/bin/statcheck", 1, stat_argv, &r);
    bool stat_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(stat_ok, "ring3 /bin/statcheck exercises stat/mkdir/ticks syscalls");
    if (!stat_ok) detail_process_result("/bin/statcheck", ps, &r);

    const char *proc_argv[] = { "/bin/procstat" };
    ps = process_exec("/bin/procstat", 1, proc_argv, &r);
    bool proc_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(proc_ok, "ring3 /bin/procstat reads current process info through syscalls");
    if (!proc_ok) detail_process_result("/bin/procstat", ps, &r);

    const char *spawncheck_argv[] = { "/bin/spawncheck" };
    ps = process_exec("/bin/spawncheck", 1, spawncheck_argv, &r);
    bool spawncheck_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(spawncheck_ok, "ring3 /bin/spawncheck exercises async spawn/wait safely");
    if (!spawncheck_ok) detail_process_result("/bin/spawncheck", ps, &r);


    const char *procctl_argv[] = { "/bin/procctl" };
    ps = process_exec("/bin/procctl", 1, procctl_argv, &r);
    bool procctl_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(procctl_ok, "ring3 /bin/procctl performs async spawnv and blocking wait");
    if (!procctl_ok) detail_process_result("/bin/procctl", ps, &r);

    const char *exec_argv[] = { "/bin/execcheck" };
    ps = process_exec("/bin/execcheck", 1, exec_argv, &r);
    bool exec_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/fscheck") == 0;
    check(exec_ok, "ring3 /bin/execcheck rejects failed exec then replaces image with execv");
    if (!exec_ok) detail_process_result("/bin/execcheck", ps, &r);

    const char *fork_argv[] = { "/bin/forkcheck" };
    ps = process_exec("/bin/forkcheck", 1, fork_argv, &r);
    bool fork_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fork_ok, "ring3 /bin/forkcheck clones address space and waits child fork");
    if (!fork_ok) detail_process_result("/bin/forkcheck", ps, &r);

    const char *sched_argv[] = { "/bin/schedcheck" };
    ps = process_exec("/bin/schedcheck", 1, sched_argv, &r);
    bool sched_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(sched_ok, "ring3 /bin/schedcheck exercises yield/sleep/schedinfo syscalls");
    if (!sched_ok) detail_process_result("/bin/schedcheck", ps, &r);

    const char *preempt_argv[] = { "/bin/preemptcheck" };
    ps = process_exec("/bin/preemptcheck", 1, preempt_argv, &r);
    bool preempt_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(preempt_ok, "ring3 /bin/preemptcheck reads preemption/timeslice telemetry");
    if (!preempt_ok) detail_process_result("/bin/preemptcheck", ps, &r);

    const char *fdcheck_argv[] = { "/bin/fdcheck" };
    ps = process_exec("/bin/fdcheck", 1, fdcheck_argv, &r);
    bool fdcheck_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fdcheck_ok, "ring3 /bin/fdcheck exercises dup/tell/fstat/fdinfo/readdir syscalls");
    if (!fdcheck_ok) detail_process_result("/bin/fdcheck", ps, &r);

    const char *iso_argv[] = { "/bin/isolate" };
    process_result_t iso_a;
    ps = process_exec("/bin/isolate", 1, iso_argv, &iso_a);
    bool iso_a_ok = ps == PROC_OK && !iso_a.faulted && iso_a.exit_code == 41;
    check(iso_a_ok, "first /bin/isolate gets zero-filled private BSS");
    if (!iso_a_ok) detail_process_result("/bin/isolate#1", ps, &iso_a);
    process_result_t iso_b;
    ps = process_exec("/bin/isolate", 1, iso_argv, &iso_b);
    bool iso_b_ok = ps == PROC_OK && !iso_b.faulted && iso_b.exit_code == 41 && iso_b.address_space_generation != iso_a.address_space_generation;
    check(iso_b_ok, "second /bin/isolate gets a distinct clean address space");
    if (!iso_b_ok) detail_process_result("/bin/isolate#2", ps, &iso_b);

    const char *fd_argv[] = { "/bin/fdleak" };
    process_result_t fd_a;
    ps = process_exec("/bin/fdleak", 1, fd_argv, &fd_a);
    bool fd_a_ok = ps == PROC_OK && !fd_a.faulted && fd_a.exit_code == 0;
    check(fd_a_ok, "first /bin/fdleak can consume all per-process handles");
    if (!fd_a_ok) detail_process_result("/bin/fdleak#1", ps, &fd_a);
    process_result_t fd_b;
    ps = process_exec("/bin/fdleak", 1, fd_argv, &fd_b);
    bool fd_b_ok = ps == PROC_OK && !fd_b.faulted && fd_b.exit_code == 0;
    check(fd_b_ok, "second /bin/fdleak proves fd table reset on exit");
    if (!fd_b_ok) detail_process_result("/bin/fdleak#2", ps, &fd_b);

    const char *write_argv[] = { "/bin/writetest" };
    ps = process_exec("/bin/writetest", 1, write_argv, &r);
    bool write_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(write_ok, "ring3 /bin/writetest uses ramfs syscalls");
    if (!write_ok) detail_process_result("/bin/writetest", ps, &r);
    char buf[80];
    usize got = 0;
    memset(buf, 0, sizeof(buf));
    check(vfs_read("/tmp/user-writetest.txt", 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && strstr(buf, "userland wrote") != 0, "kernel observes user-created ramfs file");

    const char *fs_argv[] = { "/bin/fscheck", "/disk0/hello.txt" };
    ps = process_exec("/bin/fscheck", 2, fs_argv, &r);
    bool fs_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fs_ok, "ring3 /bin/fscheck reads EXT4 through syscalls");
    if (!fs_ok) detail_process_result("/bin/fscheck", ps, &r);
    suite_end();
}

static void test_process_registry(void) {
    suite_begin("process/registry/contracts");
    usize before = process_table_count();
    check(before > 0, "process registry retained completed user processes");
    check(process_table_selftest(), "process registry records completed exec lifecycle");
    process_result_t last;
    const char *argv[] = { "/bin/procstat" };
    process_status_t ps = process_exec("/bin/procstat", 1, argv, &last);
    check(ps == PROC_OK && last.exit_code == 0 && !last.faulted, "process registry supports procinfo user program after stress");
    process_info_t info;
    check(process_lookup(last.pid, &info) && info.pid == last.pid && info.state == PROCESS_STATE_EXITED && info.exit_code == 0, "process lookup returns completed pid metadata");
    u32 pid = 0;
    const char *spawn_argv[] = { "/bin/hello" };
    process_result_t spawned;
    ps = process_spawn("/bin/hello", 1, spawn_argv, &pid, &spawned);
    check(ps == PROC_OK && pid == spawned.pid && spawned.exit_code == 7 && !spawned.faulted, "process spawn records completed child pid");
    check(process_wait(pid, &info) && info.pid == pid && info.state == PROCESS_STATE_EXITED && info.exit_code == 7, "process wait returns completed child metadata");
    const char *spawnv_argv[] = { "/bin/fscheck", "/disk0/hello.txt" };
    syscall_result_t sv = syscall_dispatch(AURORA_SYS_SPAWNV, (u64)(uptr)"/bin/fscheck", 2u, (u64)(uptr)spawnv_argv, 0, 0, 0);
    check(sv.error == 0 && sv.value > 0, "Rust-dispatched spawnv syscall accepts argv vector from kernel boundary");
    if (!sv.error && sv.value > 0) {
        check(process_wait((u32)sv.value, &info) && info.exit_code == 0 && info.state == PROCESS_STATE_EXITED, "process wait returns spawnv child metadata");
    } else {
        skip("process wait returns spawnv child metadata");
    }
    syscall_result_t sr = syscall_dispatch(AURORA_SYS_WAIT, pid, (u64)(uptr)&info, 0, 0, 0, 0);
    check(sr.error == 0 && info.pid == pid && info.exit_code == 7, "Rust-dispatched wait syscall returns child metadata");
    check(strcmp(syscall_name(AURORA_SYS_FORK), "fork") == 0, "Rust-dispatched fork syscall name is stable");
    check(strcmp(syscall_name(AURORA_SYS_EXEC), "exec") == 0 && strcmp(syscall_name(AURORA_SYS_EXECV), "execv") == 0, "Rust-dispatched exec syscall names are stable");
    check(!process_lookup(0, &info) && !process_lookup(0xffffffffu, &info), "process lookup rejects invalid or unknown pid");
    check(process_table_count() >= before, "process registry count is monotonic until ring wraps");
    suite_end();
}


static void test_scheduler_contracts(void) {
    suite_begin("scheduler/runqueue/contracts");
    scheduler_init();
    sched_stats_t stats;
    scheduler_stats(&stats);
    check(stats.queue_capacity == SCHED_QUEUE_CAP && stats.queued == 0, "scheduler initializes empty process run queue");
    check(stats.preempt_enabled == 1u && stats.quantum_ticks == SCHED_DEFAULT_QUANTUM_TICKS, "scheduler initializes preemption quantum contract");
    const char *hello_argv[] = { "/bin/hello" };
    const char *iso_argv[] = { "/bin/isolate" };
    u32 j1 = 0;
    u32 j2 = 0;
    check(scheduler_enqueue("/bin/hello", 1, hello_argv, &j1), "scheduler enqueues first user job");
    check(scheduler_enqueue("/bin/isolate", 1, iso_argv, &j2) && j2 != j1, "scheduler enqueues second user job with distinct id");
    scheduler_stats(&stats);
    check(stats.queued == 2 && stats.total_enqueued == 2, "scheduler stats track queued jobs");
    check(scheduler_run_ready(1) == 1, "scheduler dispatches one queued process");
    sched_job_info_t info;
    check(scheduler_get_job(j1, &info) && info.state == SCHED_JOB_DONE && info.pid != 0 && info.exit_code == 7, "scheduler records completed first job");
    check(scheduler_get_job(j2, &info) && info.state == SCHED_JOB_QUEUED, "scheduler leaves later job queued after bounded dispatch");
    check(scheduler_wait_job(j2, &info) && info.state == SCHED_JOB_DONE && info.exit_code == 41, "scheduler wait drains requested queued job");
    u32 before_yields = stats.total_yields;
    scheduler_note_yield();
    scheduler_note_sleep(0);
    cpu_regs_t tick_regs;
    memset(&tick_regs, 0, sizeof(tick_regs));
    tick_regs.cs = 0x08;
    tick_regs.rip = 0xffffffff80002000ull;
    scheduler_tick(&tick_regs);
    aurora_preemptinfo_t pi;
    check(scheduler_preempt_info(&pi) && pi.enabled == 1u && pi.quantum_ticks == SCHED_DEFAULT_QUANTUM_TICKS, "scheduler exposes preemption telemetry");
    scheduler_stats(&stats);
    check(stats.total_yields >= before_yields + 1 && stats.total_sleeps >= 1 && stats.total_timer_ticks >= 1, "scheduler accounting tracks yield sleep and timer tick calls");
    check(!scheduler_wait_job(0xffffffffu, &info), "scheduler wait rejects unknown job id");
    bool reuse_ok = true;
    u32 last_job = 0;
    for (u32 i = 0; i < SCHED_QUEUE_CAP + 1u; ++i) {
        u32 jid = 0;
        if (!scheduler_enqueue("/bin/hello", 1, hello_argv, &jid) || scheduler_run_ready(1) != 1) { reuse_ok = false; break; }
        last_job = jid;
    }
    check(reuse_ok && scheduler_get_job(last_job, &info) && info.state == SCHED_JOB_DONE, "scheduler reuses terminal queue slots after 33 jobs");
    suite_end();
}

static void test_coverage_contracts(void) {
    suite_begin("coverage/contracts/postconditions");
    bool syscall_names_ok = true;
    for (u64 no = 0; no < AURORA_SYS_MAX; ++no) {
        if (strcmp(syscall_name(no), "unknown") == 0) syscall_names_ok = false;
    }
    check(syscall_names_ok && strcmp(syscall_name(AURORA_SYS_MAX), "unknown") == 0, "all syscall IDs have stable Rust names");
    check(aurora_rust_syscall_selftest(), "Rust syscall validator remains linked and callable");
    check(aurora_rust_vfs_route_selftest(), "Rust VFS router remains linked and callable");
    check(aurora_rust_path_policy_selftest(), "Rust path policy remains linked and callable");
    check(aurora_rust_usercopy_selftest(), "Rust usercopy checker remains linked and callable");
    check(!process_user_active(), "no active user process left after userland tests");
    check(vmm_current_space() == vmm_kernel_space(), "kernel CR3 space restored after all userland tests");
    vmm_stats_t vs;
    vmm_get_stats(&vs);
    check(vs.current_pml4_physical == vs.pml4_physical && vmm_read_cr3() == vs.pml4_physical, "hardware CR3 restored to kernel pml4");
    check(block_count() >= 1 && block_get(0) != 0, "block registry still valid after tests");
    check(vfs_stat("/tmp", &(vfs_stat_t){0}) == VFS_OK, "root ramfs /tmp still reachable after tests");
    suite_end();
}

bool ktest_run_all(void) {
    memset(&totals, 0, sizeof(totals));
    kprintf("\n%s started\n", AURORA_KTEST_TITLE);
    u64 start = pit_ticks();
    test_libc_bitmap_ring_crc();
    test_boot_memory_vmm_heap();
    test_vfs_ramfs_devfs();
    test_tarfs_module();
    test_block_mbr_ext4();
    test_syscall_task_timer_elf();
    test_user_processes();
    test_process_registry();
    test_scheduler_contracts();
    test_coverage_contracts();
    u64 end = pit_ticks();
    kprintf("\n%s finished: suites=%u passed=%u failed=%u skipped=%u ticks=%llu\n", AURORA_KTEST_TITLE,
            totals.suites, totals.passed, totals.failed, totals.skipped, (unsigned long long)(end - start));
    if (totals.failed == 0) kprintf("KTEST_STATUS: PASS\n");
    else kprintf("KTEST_STATUS: FAIL\n");
    return totals.failed == 0;
}

void ktest_get_last_totals(ktest_totals_t *out) {
    if (out) *out = totals;
}
