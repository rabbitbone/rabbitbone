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
#include <aurora/tty.h>

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

static const char *ktest_disk_raw_path(const char *disk_path) {
    return (disk_path && strncmp(disk_path, "/disk0", 6u) == 0) ? disk_path + 6 : disk_path;
}

#define KTEST_CLEANUP_BATCH 32u

typedef struct ktest_cleanup_batch {
    const char *prefix;
    char names[KTEST_CLEANUP_BATCH][VFS_NAME_MAX];
    vfs_node_type_t types[KTEST_CLEANUP_BATCH];
    u32 count;
    bool overflow;
} ktest_cleanup_batch_t;

static bool ktest_name_has_prefix(const char *name, const char *prefix) {
    if (!name) return false;
    if (!prefix || !prefix[0]) return true;
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

static bool ktest_cleanup_collect_cb(const vfs_dirent_t *e, void *ctx) {
    ktest_cleanup_batch_t *b = (ktest_cleanup_batch_t *)ctx;
    if (!e || !b) return true;
    if (strcmp(e->name, ".") == 0 || strcmp(e->name, "..") == 0) return true;
    if (!ktest_name_has_prefix(e->name, b->prefix)) return true;
    if (b->count >= KTEST_CLEANUP_BATCH) {
        b->overflow = true;
        return false;
    }
    strncpy(b->names[b->count], e->name, VFS_NAME_MAX - 1u);
    b->names[b->count][VFS_NAME_MAX - 1u] = 0;
    b->types[b->count] = e->type;
    ++b->count;
    return true;
}

static ktest_cleanup_batch_t *ktest_cleanup_collect(const char *path, const char *prefix, vfs_status_t *status_out) {
    ktest_cleanup_batch_t *batch = (ktest_cleanup_batch_t *)kmalloc(sizeof(*batch));
    if (!batch) {
        if (status_out) *status_out = VFS_ERR_NOMEM;
        return 0;
    }
    memset(batch, 0, sizeof(*batch));
    batch->prefix = prefix;
    vfs_status_t st = vfs_list(path, ktest_cleanup_collect_cb, batch);
    if (status_out) *status_out = st;
    if (st != VFS_OK) {
        kfree(batch);
        return 0;
    }
    return batch;
}

static bool ktest_remove_tree(const char *path) {
    if (!path) return false;
    vfs_stat_t st;
    vfs_status_t sv = vfs_lstat(path, &st);
    if (sv == VFS_ERR_NOENT) return true;
    if (sv != VFS_OK) return false;
    if (st.type == VFS_NODE_DIR) {
        for (u32 pass = 0; pass < 128u; ++pass) {
            vfs_status_t lv = VFS_OK;
            ktest_cleanup_batch_t *batch = ktest_cleanup_collect(path, 0, &lv);
            if (!batch) return lv == VFS_OK;
            if (batch->count == 0) { kfree(batch); break; }
            bool removed_any = false;
            for (u32 i = 0; i < batch->count; ++i) {
                char child[VFS_PATH_MAX];
                ksnprintf(child, sizeof(child), "%s/%s", path, batch->names[i]);
                if (ktest_remove_tree(child)) removed_any = true;
            }
            bool overflow = batch->overflow;
            kfree(batch);
            if (!removed_any && !overflow) break;
        }
    }
    sv = vfs_unlink(path);
    return sv == VFS_OK || sv == VFS_ERR_NOENT;
}

static void ktest_cleanup_disk_prefix(const char *prefix) {
    if (!prefix || !prefix[0]) return;
    for (u32 pass = 0; pass < 256u; ++pass) {
        vfs_status_t lv = VFS_OK;
        ktest_cleanup_batch_t *batch = ktest_cleanup_collect("/disk0", prefix, &lv);
        if (!batch) break;
        if (batch->count == 0) { kfree(batch); break; }
        bool removed_any = false;
        for (u32 i = 0; i < batch->count; ++i) {
            char path[VFS_PATH_MAX];
            ksnprintf(path, sizeof(path), "/disk0/%s", batch->names[i]);
            if (ktest_remove_tree(path)) removed_any = true;
        }
        bool overflow = batch->overflow;
        kfree(batch);
        if (!removed_any && !overflow) break;
    }
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
    check(tty_selftest(), "TTY mode/info/readkey selftest");
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

    vfs_stat_t dir_st_before;
    vfs_stat_t dir_st_after;
    const char *subdir = "/tmp/ktest-vfs/subdir";
    (void)vfs_unlink(subdir);
    check(vfs_stat(dir, &dir_st_before) == VFS_OK && dir_st_before.type == VFS_NODE_DIR && dir_st_before.nlink >= 2u, "ramfs directory nlink baseline");
    check(vfs_mkdir(subdir) == VFS_OK, "ramfs mkdir child directory for nlink");
    check(vfs_stat(dir, &dir_st_after) == VFS_OK && dir_st_after.nlink == dir_st_before.nlink + 1u, "ramfs parent directory nlink increments on mkdir");
    check(vfs_unlink(subdir) == VFS_OK, "ramfs unlink empty child directory for nlink");
    check(vfs_stat(dir, &dir_st_after) == VFS_OK && dir_st_after.nlink == dir_st_before.nlink, "ramfs parent directory nlink decrements on rmdir");

    const char *hl_src = "/tmp/ktest-vfs/hlink-src.txt";
    const char *hl_dst = "/tmp/ktest-vfs/hlink-dst.txt";
    (void)vfs_unlink(hl_dst);
    (void)vfs_unlink(hl_src);
    check(vfs_create(hl_src, "hard", 4u) == VFS_OK, "ramfs hardlink source create");
    check(vfs_link(hl_src, hl_dst) == VFS_OK, "ramfs hardlink create");
    vfs_stat_t hl_src_st;
    vfs_stat_t hl_dst_st;
    check(vfs_stat(hl_src, &hl_src_st) == VFS_OK && vfs_stat(hl_dst, &hl_dst_st) == VFS_OK && hl_src_st.inode == hl_dst_st.inode && hl_src_st.nlink == 2u && hl_dst_st.nlink == 2u, "ramfs hardlink shares inode and link count");
    wrote = 0;
    check(vfs_write(hl_dst, 0, "HARD", 4u, &wrote) == VFS_OK && wrote == 4u, "ramfs hardlink write through alias");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_read(hl_src, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got == 4u && memcmp(buf, "HARD", 4u) == 0, "ramfs hardlink source sees alias write");
    check(vfs_unlink(hl_src) == VFS_OK, "ramfs unlink one hardlink");
    check(vfs_stat(hl_dst, &hl_dst_st) == VFS_OK && hl_dst_st.nlink == 1u && hl_dst_st.size == 4u, "ramfs hardlink survives source unlink");
    check(vfs_unlink(hl_dst) == VFS_OK, "ramfs unlink final hardlink");

    const char *sym_target = "/tmp/ktest-vfs/symlink-target.txt";
    const char *sym_path = "/tmp/ktest-vfs/symlink.ln";
    (void)vfs_unlink(sym_path);
    (void)vfs_unlink(sym_target);
    check(vfs_create(sym_target, "target", 6u) == VFS_OK, "ramfs symlink target create");
    check(vfs_symlink("symlink-target.txt", sym_path) == VFS_OK, "ramfs symlink create relative target");
    vfs_stat_t sym_lst;
    check(vfs_lstat(sym_path, &sym_lst) == VFS_OK && sym_lst.type == VFS_NODE_SYMLINK && sym_lst.size == strlen("symlink-target.txt") && sym_lst.nlink == 1u, "ramfs lstat sees symlink inode");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_readlink(sym_path, buf, sizeof(buf), &got) == VFS_OK && got == strlen("symlink-target.txt") && memcmp(buf, "symlink-target.txt", got) == 0, "ramfs readlink returns raw target");
    check(vfs_stat(sym_path, &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size == 6u, "ramfs stat follows symlink");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_read(sym_path, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got == 6u && memcmp(buf, "target", 6u) == 0, "ramfs read follows symlink");
    check(vfs_unlink(sym_path) == VFS_OK && vfs_stat(sym_target, &st) == VFS_OK, "ramfs unlink symlink keeps target");

    const char *sym2 = "/tmp/ktest-vfs/symlink-hard.ln";
    const char *sym_hard = "/tmp/ktest-vfs/symlink-hard-alias.ln";
    (void)vfs_unlink(sym_hard);
    (void)vfs_unlink(sym2);
    check(vfs_symlink("symlink-target.txt", sym2) == VFS_OK, "ramfs symlink hardlink source create");
    check(vfs_link(sym2, sym_hard) == VFS_OK, "ramfs hardlink to symlink inode create");
    vfs_stat_t sym2_st;
    vfs_stat_t sym_hard_st;
    check(vfs_lstat(sym2, &sym2_st) == VFS_OK && vfs_lstat(sym_hard, &sym_hard_st) == VFS_OK && sym2_st.type == VFS_NODE_SYMLINK && sym2_st.inode == sym_hard_st.inode && sym2_st.nlink == 2u && sym_hard_st.nlink == 2u, "ramfs hardlink to symlink preserves symlink inode");
    check(vfs_unlink(sym2) == VFS_OK, "ramfs unlink original symlink hardlink");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_lstat(sym_hard, &sym_hard_st) == VFS_OK && sym_hard_st.nlink == 1u && vfs_readlink(sym_hard, buf, sizeof(buf), &got) == VFS_OK && got == strlen("symlink-target.txt"), "ramfs symlink hardlink survives unlink");
    check(vfs_unlink(sym_hard) == VFS_OK && vfs_unlink(sym_target) == VFS_OK, "ramfs symlink hardlink cleanup");

    const char *long_sym = "/tmp/ktest-vfs/long-symlink.ln";
    static const char long_sym_target[] = "this/is/a/long/symlink/target/that/exercises/raw-target-storage-without-inline-shortcuts";
    (void)vfs_unlink(long_sym);
    check(vfs_symlink(long_sym_target, long_sym) == VFS_OK, "ramfs long symlink create");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_lstat(long_sym, &sym_lst) == VFS_OK && sym_lst.type == VFS_NODE_SYMLINK && sym_lst.size == strlen(long_sym_target) && vfs_readlink(long_sym, buf, sizeof(buf), &got) == VFS_OK && got == strlen(long_sym_target) && memcmp(buf, long_sym_target, got) == 0, "ramfs long symlink lstat/readlink");
    check(vfs_unlink(long_sym) == VFS_OK, "ramfs long symlink cleanup");

    vfs_node_ref_t file_ref;
    const char *renamed = "/tmp/ktest-vfs/renamed.txt";
    (void)vfs_unlink(renamed);
    check(vfs_get_ref(file, &file_ref) == VFS_OK && file_ref.inode != 0, "vfs captures inode ref for file");
    check(vfs_rename(file, renamed) == VFS_OK, "ramfs rename open-ref target");
    memset(buf, 0, sizeof(buf));
    got = 0;
    check(vfs_read_ref(&file_ref, 0, buf, sizeof(buf) - 1u, &got) == VFS_OK && got == 4u && memcmp(buf, "aXYZ", 4u) == 0, "vfs read_ref survives rename");
    wrote = 0;
    check(vfs_write_ref(&file_ref, 4u, "!", 1u, &wrote) == VFS_OK && wrote == 1u, "vfs write_ref survives rename");
    check(vfs_truncate_ref(&file_ref, 2u) == VFS_OK, "vfs truncate_ref targets inode");
    check(vfs_stat_ref(&file_ref, &st) == VFS_OK && st.size == 2u, "vfs stat_ref sees truncated inode");
    check(vfs_sync_ref(&file_ref, true) == VFS_OK && vfs_sync_ref(&file_ref, false) == VFS_OK, "vfs sync_ref file data/full");
    check(vfs_sync_parent_dir(renamed) == VFS_OK, "vfs sync parent directory after rename");
    vfs_node_ref_t dir_ref;
    seen_name_ctx_t renamed_seen = { "renamed.txt", false };
    check(vfs_get_ref(dir, &dir_ref) == VFS_OK && vfs_list_ref(&dir_ref, list_seen_name, &renamed_seen) == VFS_OK && renamed_seen.seen, "vfs list_ref targets directory inode");
    check(vfs_stat(file, &st) == VFS_ERR_NOENT, "ramfs old rename path absent");
    check(vfs_unlink(renamed) == VFS_OK, "ramfs unlink file");
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
    check(vfs_read("/dev/prng", 0, rnd, sizeof(rnd), &got) == VFS_OK && got == sizeof(rnd), "devfs /dev/prng read");
    check(vfs_stat("/dev/tty", &st) == VFS_OK && st.type == VFS_NODE_DEV, "devfs /dev/tty present");
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
    u8 *repair_scratch = (u8 *)kmalloc(4096u);
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
        kfree(repair_scratch);
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
    const mbr_partition_t *part = mbr_ok ? mbr_find_linux_on_device(dev, &mbr) : 0;
    check(part && part->lba_first >= 2048u && part->sector_count > 4096u, "MBR has Linux EXT partition");
    if (part) {
        ext4_mount_t mnt;
        ext4_status_t est = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &mnt);
        check(est == EXT4_OK, "EXT4 raw mount first Linux partition");
        if (est == EXT4_OK) {
            ext4_inode_disk_t root;
            ext4_fsck_report_t report;
            check(ext4_validate_metadata(&mnt, &report) == EXT4_OK && report.errors == 0 && report.checked_groups == mnt.group_count, "EXT4 metadata counters match allocation bitmaps");
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
    vfs_statvfs_t sv;
    memset(&sv, 0, sizeof(sv));
    check(vfs_statvfs("/disk0", &sv) == VFS_OK && sv.block_size >= 1024u && sv.total_blocks > 0u && sv.free_blocks <= sv.total_blocks && (sv.flags & VFS_STATVFS_FLAG_PERSISTENT) != 0, "VFS statvfs exposes EXT4 capacity and persistence flags");
    check(vfs_sync_path("/disk0") == VFS_OK && vfs_sync_all() == VFS_OK, "VFS sync flushes EXT4 writeback state");
    ktest_cleanup_disk_prefix("aurora-");

    const char *rw_path = "/disk0/aurora-rw.txt";
    const char rw_payload[] = "Aurora EXT4 write path survives VFS create/write/read/unlink";
    vfs_status_t rw_cleanup = vfs_unlink(rw_path);
    vfs_status_t rw_create = vfs_create(rw_path, rw_payload, sizeof(rw_payload) - 1u);
    if (rw_create != VFS_OK) {
        kprintf("[ detail] ext4 rw create cleanup=%s create=%s\n", vfs_status_name(rw_cleanup), vfs_status_name(rw_create));
    }
    check(rw_create == VFS_OK, "VFS create /disk0 file through ext4 writer");
    if (part) {
        ext4_mount_t verify_mnt;
        ext4_inode_disk_t created_inode;
        u32 created_ino = 0;
        bool extent_file = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &verify_mnt) == EXT4_OK &&
                           ext4_lookup_path(&verify_mnt, "/aurora-rw.txt", &created_inode, &created_ino) == EXT4_OK &&
                           created_ino != 0 && (created_inode.i_flags & EXT4_INODE_FLAG_EXTENTS) != 0 &&
                           ((const u16 *)created_inode.i_block)[0] == EXT4_EXTENT_MAGIC;
        check(extent_file, "EXT4 writer creates regular files with inline extent tree");
    } else {
        skip("EXT4 writer creates regular files with inline extent tree");
    }
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == sizeof(rw_payload) - 1u && strstr(text, "EXT4 write") != 0, "VFS read back ext4-created file");
    const char patch[] = "persistent";
    usize wrote = 0;
    check(vfs_write(rw_path, 7u, patch, sizeof(patch) - 1u, &wrote) == VFS_OK && wrote == sizeof(patch) - 1u, "VFS overwrite ext4 file");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && strstr(text, "persistent") != 0, "VFS read back ext4 overwrite");
    check(vfs_truncate(rw_path, 12u) == VFS_OK, "VFS truncate ext4 file smaller");
    vfs_stat_t trunc_st;
    check(vfs_stat(rw_path, &trunc_st) == VFS_OK && trunc_st.size == 12u, "VFS stat ext4 truncated size");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(rw_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 12u, "VFS read ext4 truncated contents");
    check(vfs_truncate(rw_path, 8192u + 17u) == VFS_OK, "VFS expand ext4 file through truncate");
    check(vfs_stat(rw_path, &trunc_st) == VFS_OK && trunc_st.size == 8192u + 17u, "VFS stat ext4 expanded size");
    vfs_node_ref_t ext4_ref;
    check(vfs_get_ref(rw_path, &ext4_ref) == VFS_OK && ext4_ref.inode != 0, "VFS captures EXT4 inode ref before rename");
    const char *renamed_path = "/disk0/aurora-rw-renamed.txt";
    (void)vfs_unlink(renamed_path);
    check(vfs_rename(rw_path, renamed_path) == VFS_OK, "VFS rename ext4 file");
    check(vfs_stat(rw_path, &trunc_st) == VFS_ERR_NOENT && vfs_stat(renamed_path, &trunc_st) == VFS_OK && trunc_st.size == 8192u + 17u, "VFS observes ext4 renamed file");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read_ref(&ext4_ref, 0, text, 12u, &got) == VFS_OK && got == 12u && memcmp(text, "Aurora persi", 12u) == 0, "VFS read_ref survives EXT4 rename");
    wrote = 0;
    check(vfs_write_ref(&ext4_ref, 0, "FD", 2u, &wrote) == VFS_OK && wrote == 2u, "VFS write_ref survives EXT4 rename");
    check(vfs_sync_ref(&ext4_ref, true) == VFS_OK && vfs_sync_ref(&ext4_ref, false) == VFS_OK, "VFS sync_ref EXT4 file data/full");
    check(vfs_sync_parent_dir(renamed_path) == VFS_OK, "VFS fsync parent dir after EXT4 rename");

    const char *replace_src = "/disk0/aurora-replace-src.txt";
    const char *replace_dst = "/disk0/aurora-replace-dst.txt";
    (void)vfs_unlink(replace_src);
    (void)vfs_unlink(replace_dst);
    check(vfs_create(replace_dst, "old-file", 8u) == VFS_OK && vfs_create(replace_src, "new-file", 8u) == VFS_OK, "VFS prepares ext4 rename-overwrite files");
    check(vfs_rename(replace_src, replace_dst) == VFS_OK, "VFS rename overwrites existing ext4 file atomically");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_stat(replace_src, &trunc_st) == VFS_ERR_NOENT && vfs_read(replace_dst, 0, text, 8u, &got) == VFS_OK && got == 8u && memcmp(text, "new-file", 8u) == 0, "VFS rename-overwrite exposes replacement contents");

    const char *install_stage = "/disk0/aurora-app.stage";
    const char *install_final = "/disk0/aurora-app.bin";
    (void)vfs_unlink(install_stage);
    (void)vfs_unlink(install_final);
    check(vfs_create(install_final, "app-v1", 6u) == VFS_OK && vfs_create(install_stage, "app-v2", 6u) == VFS_OK, "VFS prepares staged ext4 app install");
    check(vfs_install_commit(install_stage, install_final) == VFS_OK, "VFS install_commit swaps staged ext4 app into final path");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_stat(install_stage, &trunc_st) == VFS_ERR_NOENT && vfs_read(install_final, 0, text, 6u, &got) == VFS_OK && got == 6u && memcmp(text, "app-v2", 6u) == 0, "VFS install_commit preserves final app payload");
    check(vfs_sync_path(install_final) == VFS_OK, "VFS install_commit result fsyncs through EXT4");
    (void)vfs_unlink(install_final);
    (void)vfs_unlink(replace_dst);

    const char *ext_hl_src = "/disk0/aurora-hlink-src.txt";
    const char *ext_hl_dst = "/disk0/aurora-hlink-dst.txt";
    (void)vfs_unlink(ext_hl_dst);
    (void)vfs_unlink(ext_hl_src);
    check(vfs_create(ext_hl_src, "hard", 4u) == VFS_OK, "EXT4 hardlink source create");
    check(vfs_link(ext_hl_src, ext_hl_dst) == VFS_OK, "EXT4 hardlink create");
    vfs_stat_t ext_hl_src_st;
    vfs_stat_t ext_hl_dst_st;
    check(vfs_stat(ext_hl_src, &ext_hl_src_st) == VFS_OK && vfs_stat(ext_hl_dst, &ext_hl_dst_st) == VFS_OK && ext_hl_src_st.inode == ext_hl_dst_st.inode && ext_hl_src_st.nlink == 2u && ext_hl_dst_st.nlink == 2u, "EXT4 hardlink shares inode and link count");
    wrote = 0;
    check(vfs_write(ext_hl_dst, 0, "HARD", 4u, &wrote) == VFS_OK && wrote == 4u, "EXT4 hardlink write through alias");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(ext_hl_src, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 4u && memcmp(text, "HARD", 4u) == 0, "EXT4 hardlink source sees alias write");
    check(vfs_unlink(ext_hl_src) == VFS_OK, "EXT4 unlink one hardlink");
    check(vfs_stat(ext_hl_dst, &ext_hl_dst_st) == VFS_OK && ext_hl_dst_st.nlink == 1u && ext_hl_dst_st.size == 4u, "EXT4 hardlink survives source unlink");
    check(vfs_unlink(ext_hl_dst) == VFS_OK, "EXT4 unlink final hardlink");

    const char *ext_sym_target = "/disk0/aurora-sym-target.txt";
    const char *ext_sym_path = "/disk0/aurora-sym.ln";
    (void)vfs_unlink(ext_sym_path);
    (void)vfs_unlink(ext_sym_target);
    check(vfs_create(ext_sym_target, "target", 6u) == VFS_OK, "EXT4 symlink target create");
    check(vfs_symlink("aurora-sym-target.txt", ext_sym_path) == VFS_OK, "EXT4 symlink create relative target");
    vfs_stat_t ext_sym_lst;
    check(vfs_lstat(ext_sym_path, &ext_sym_lst) == VFS_OK && ext_sym_lst.type == VFS_NODE_SYMLINK && ext_sym_lst.size == strlen("aurora-sym-target.txt") && ext_sym_lst.nlink == 1u, "EXT4 lstat sees symlink inode");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_readlink(ext_sym_path, text, sizeof(text), &got) == VFS_OK && got == strlen("aurora-sym-target.txt") && memcmp(text, "aurora-sym-target.txt", got) == 0, "EXT4 readlink returns raw target");
    check(vfs_stat(ext_sym_path, &trunc_st) == VFS_OK && trunc_st.type == VFS_NODE_FILE && trunc_st.size == 6u, "EXT4 stat follows symlink");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(ext_sym_path, 0, text, sizeof(text) - 1u, &got) == VFS_OK && got == 6u && memcmp(text, "target", 6u) == 0, "EXT4 read follows symlink");
    check(vfs_unlink(ext_sym_path) == VFS_OK && vfs_stat(ext_sym_target, &trunc_st) == VFS_OK, "EXT4 unlink symlink keeps target");

    const char *ext_sym2 = "/disk0/aurora-sym-hard.ln";
    const char *ext_sym_hard = "/disk0/aurora-sym-hard-alias.ln";
    (void)vfs_unlink(ext_sym_hard);
    (void)vfs_unlink(ext_sym2);
    check(vfs_symlink("aurora-sym-target.txt", ext_sym2) == VFS_OK, "EXT4 symlink hardlink source create");
    check(vfs_link(ext_sym2, ext_sym_hard) == VFS_OK, "EXT4 hardlink to symlink inode create");
    vfs_stat_t ext_sym2_st;
    vfs_stat_t ext_sym_hard_st;
    check(vfs_lstat(ext_sym2, &ext_sym2_st) == VFS_OK && vfs_lstat(ext_sym_hard, &ext_sym_hard_st) == VFS_OK && ext_sym2_st.type == VFS_NODE_SYMLINK && ext_sym2_st.inode == ext_sym_hard_st.inode && ext_sym2_st.nlink == 2u && ext_sym_hard_st.nlink == 2u, "EXT4 hardlink to symlink preserves symlink inode");
    check(vfs_unlink(ext_sym2) == VFS_OK, "EXT4 unlink original symlink hardlink");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_lstat(ext_sym_hard, &ext_sym_hard_st) == VFS_OK && ext_sym_hard_st.nlink == 1u && vfs_readlink(ext_sym_hard, text, sizeof(text), &got) == VFS_OK && got == strlen("aurora-sym-target.txt"), "EXT4 symlink hardlink survives unlink");
    check(vfs_unlink(ext_sym_hard) == VFS_OK && vfs_unlink(ext_sym_target) == VFS_OK, "EXT4 symlink hardlink cleanup");

    const char *ext_long_sym = "/disk0/aurora-long-sym.ln";
    const char ext_long_sym_target[] = "this/is/a/long/symlink/target/that/exercises/ext4-extent-backed-target-storage";
    (void)vfs_unlink(ext_long_sym);
    check(vfs_symlink(ext_long_sym_target, ext_long_sym) == VFS_OK, "EXT4 long symlink create");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_lstat(ext_long_sym, &ext_sym_lst) == VFS_OK && ext_sym_lst.type == VFS_NODE_SYMLINK && ext_sym_lst.size == strlen(ext_long_sym_target) && vfs_readlink(ext_long_sym, text, sizeof(text), &got) == VFS_OK && got == strlen(ext_long_sym_target) && memcmp(text, ext_long_sym_target, got) == 0, "EXT4 long symlink lstat/readlink");
    check(vfs_unlink(ext_long_sym) == VFS_OK, "EXT4 long symlink cleanup");

    (void)vfs_unlink("/disk0/aurora-dir/subdir");
    (void)vfs_unlink("/disk0/aurora-dir/child.txt");
    (void)vfs_unlink("/disk0/aurora-dir");
    check(vfs_mkdir("/disk0/aurora-dir") == VFS_OK, "VFS mkdir on ext4");
    vfs_stat_t ext_dir_before;
    vfs_stat_t ext_dir_after;
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_before) == VFS_OK && ext_dir_before.type == VFS_NODE_DIR && ext_dir_before.nlink >= 2u, "EXT4 directory nlink baseline");
    check(vfs_mkdir("/disk0/aurora-dir/subdir") == VFS_OK, "EXT4 mkdir child directory for nlink");
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_after) == VFS_OK && ext_dir_after.nlink == ext_dir_before.nlink + 1u, "EXT4 parent directory nlink increments on mkdir");
    check(vfs_unlink("/disk0/aurora-dir/subdir") == VFS_OK, "EXT4 unlink empty child directory for nlink");
    check(vfs_stat("/disk0/aurora-dir", &ext_dir_after) == VFS_OK && ext_dir_after.nlink == ext_dir_before.nlink, "EXT4 parent directory nlink decrements on rmdir");
    check(vfs_create("/disk0/aurora-dir/child.txt", "x", 1u) == VFS_OK, "VFS create child on ext4");
    check(vfs_unlink("/disk0/aurora-dir") == VFS_ERR_NOTEMPTY, "VFS rejects unlink non-empty ext4 dir");
    check(vfs_unlink("/disk0/aurora-dir/child.txt") == VFS_OK, "VFS unlink ext4 child");
    check(vfs_unlink("/disk0/aurora-dir") == VFS_OK, "VFS unlink empty ext4 dir");

    const char *sparse_path = "/disk0/aurora-sparse.bin";
    (void)vfs_unlink(sparse_path);
    check(vfs_create(sparse_path, 0, 0) == VFS_OK, "VFS create empty ext4 extent file");
    const char tail[] = "tail";
    wrote = 0;
    check(vfs_write(sparse_path, 12288u + 5u, tail, sizeof(tail) - 1u, &wrote) == VFS_OK && wrote == sizeof(tail) - 1u, "VFS sparse ext4 write beyond EOF");
    vfs_stat_t sparse_st;
    check(vfs_stat(sparse_path, &sparse_st) == VFS_OK && sparse_st.size == 12288u + 5u + sizeof(tail) - 1u, "VFS sparse ext4 stat after gap write");
    char zeros[32];
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool zero_gap = vfs_read(sparse_path, 0, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && zero_gap; ++zi) zero_gap = zeros[zi] == 0;
    check(zero_gap, "EXT4 sparse hole reads back as zero-filled data");
    memset(text, 0, sizeof(text));
    got = 0;
    check(vfs_read(sparse_path, 12288u + 5u, text, sizeof(tail) - 1u, &got) == VFS_OK && got == sizeof(tail) - 1u && memcmp(text, tail, sizeof(tail) - 1u) == 0, "EXT4 sparse tail data survives readback");
    check(vfs_truncate(sparse_path, 1u) == VFS_OK && vfs_stat(sparse_path, &sparse_st) == VFS_OK && sparse_st.size == 1u, "EXT4 sparse extent file truncates smaller");
    check(vfs_unlink(sparse_path) == VFS_OK, "VFS unlink ext4 sparse extent file");

    const char *perf_path = "/disk0/aurora-fullblock-perf.bin";
    (void)vfs_unlink(perf_path);
    if (part) {
        ext4_mount_t perf_mnt_before;
        ext4_perf_stats_t perf_before;
        ext4_perf_stats_t perf_after;
        char *perf_block = 0;
        bool perf_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &perf_mnt_before) == EXT4_OK &&
                       ext4_get_perf_stats(&perf_mnt_before, &perf_before) == EXT4_OK &&
                       perf_mnt_before.block_size <= 4096u &&
                       (perf_block = (char *)kmalloc((usize)perf_mnt_before.block_size)) != 0 &&
                       vfs_create(perf_path, 0, 0) == VFS_OK;
        if (perf_ok) {
            for (usize pi = 0; pi < (usize)perf_mnt_before.block_size; ++pi) perf_block[pi] = (char)('A' + (pi % 23u));
            wrote = 0;
            perf_ok = vfs_write(perf_path, 0, perf_block, (usize)perf_mnt_before.block_size, &wrote) == VFS_OK &&
                      wrote == (usize)perf_mnt_before.block_size &&
                      ext4_get_perf_stats(&perf_mnt_before, &perf_after) == EXT4_OK &&
                      perf_after.zero_block_skips > perf_before.zero_block_skips;
        }
        if (perf_block) kfree(perf_block);
        check(perf_ok, "EXT4 full-block data allocation skips eager zero-write safely");
        check(vfs_unlink(perf_path) == VFS_OK, "VFS unlink ext4 full-block perf file");

        const char *buffered_path = "/disk0/aurora-buffered-data.bin";
        (void)vfs_unlink(buffered_path);
        ext4_mount_t buffered_mnt;
        ext4_perf_stats_t buffered_before;
        ext4_perf_stats_t buffered_after;
        char *buffered_write = 0;
        char *buffered_read = 0;
        bool buffered_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &buffered_mnt) == EXT4_OK &&
                           ext4_get_perf_stats(&buffered_mnt, &buffered_before) == EXT4_OK &&
                           buffered_mnt.block_size <= 4096u &&
                           (buffered_write = (char *)kmalloc((usize)buffered_mnt.block_size)) != 0 &&
                           (buffered_read = (char *)kmalloc((usize)buffered_mnt.block_size)) != 0 &&
                           vfs_create(buffered_path, 0, 0) == VFS_OK;
        if (buffered_ok) {
            for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('a' + (bi % 19u));
            wrote = 0;
            memset(buffered_read, 0, (usize)buffered_mnt.block_size);
            buffered_ok = vfs_write(buffered_path, 0, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                          wrote == (usize)buffered_mnt.block_size &&
                          vfs_read(buffered_path, 0, buffered_read, (usize)buffered_mnt.block_size, &got) == VFS_OK &&
                          got == (usize)buffered_mnt.block_size &&
                          memcmp(buffered_read, buffered_write, (usize)buffered_mnt.block_size) == 0 &&
                          ext4_get_perf_stats(&buffered_mnt, &buffered_after) == EXT4_OK &&
                          buffered_after.data_cache_stores > buffered_before.data_cache_stores &&
                          buffered_after.data_cache_hits > buffered_before.data_cache_hits;
        }
        check(buffered_ok, "EXT4 buffered data cache serves immediate readback before sync");
        check(vfs_unlink(buffered_path) == VFS_OK, "VFS unlink ext4 buffered-data file");

        const char *readahead_path = "/disk0/aurora-readahead-data.bin";
        (void)vfs_unlink(readahead_path);
        ext4_perf_stats_t read_policy_before;
        ext4_perf_stats_t read_policy_after;
        bool read_policy_ok = ext4_get_perf_stats(&buffered_mnt, &read_policy_before) == EXT4_OK &&
                              vfs_create(readahead_path, 0, 0) == VFS_OK;
        if (read_policy_ok && buffered_write) {
            for (u32 ri = 0; ri < 3u && read_policy_ok; ++ri) {
                for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('R' + ri + (bi % 7u));
                wrote = 0;
                read_policy_ok = vfs_write(readahead_path, (u64)ri * buffered_mnt.block_size, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                                 wrote == (usize)buffered_mnt.block_size;
            }
            memset(buffered_read, 0, (usize)buffered_mnt.block_size);
            read_policy_ok = read_policy_ok &&
                             vfs_sync_path(readahead_path) == VFS_OK &&
                             ext4_get_perf_stats(&buffered_mnt, &read_policy_before) == EXT4_OK &&
                             vfs_read(readahead_path, 0, buffered_read, (usize)buffered_mnt.block_size, &got) == VFS_OK &&
                             got == (usize)buffered_mnt.block_size &&
                             ext4_get_perf_stats(&buffered_mnt, &read_policy_after) == EXT4_OK &&
                             read_policy_after.data_cache_clean_stores > read_policy_before.data_cache_clean_stores &&
                             read_policy_after.data_cache_readahead > read_policy_before.data_cache_readahead;
        }
        check(read_policy_ok, "EXT4 data cache read miss installs clean page and read-ahead");
        check(vfs_unlink(readahead_path) == VFS_OK, "VFS unlink ext4 readahead-data file");

        const char *pressure_path = "/disk0/aurora-cache-pressure.bin";
        (void)vfs_unlink(pressure_path);
        ext4_perf_stats_t pressure_before;
        ext4_perf_stats_t pressure_after;
        bool pressure_ok = ext4_get_perf_stats(&buffered_mnt, &pressure_before) == EXT4_OK &&
                           vfs_create(pressure_path, 0, 0) == VFS_OK;
        if (pressure_ok && buffered_write) {
            for (u32 pi = 0; pi < 32u && pressure_ok; ++pi) {
                for (usize bi = 0; bi < (usize)buffered_mnt.block_size; ++bi) buffered_write[bi] = (char)('p' + (pi % 11u));
                wrote = 0;
                pressure_ok = vfs_write(pressure_path, (u64)pi * buffered_mnt.block_size, buffered_write, (usize)buffered_mnt.block_size, &wrote) == VFS_OK &&
                              wrote == (usize)buffered_mnt.block_size;
            }
            pressure_ok = pressure_ok &&
                          ext4_get_perf_stats(&buffered_mnt, &pressure_after) == EXT4_OK &&
                          pressure_after.data_cache_pressure_flushes > pressure_before.data_cache_pressure_flushes &&
                          pressure_after.data_cache_writeback_runs > pressure_before.data_cache_writeback_runs &&
                          vfs_sync_path(pressure_path) == VFS_OK;
        }
        check(pressure_ok, "EXT4 data cache pressure triggers bounded writeback");
        check(vfs_unlink(pressure_path) == VFS_OK, "VFS unlink ext4 cache-pressure file");
        if (buffered_write) kfree(buffered_write);
        if (buffered_read) kfree(buffered_read);

        const char *unwritten_path = "/disk0/aurora-unwritten.bin";
        (void)vfs_unlink(unwritten_path);
        ext4_mount_t unw_mnt;
        ext4_inode_disk_t unw_inode;
        u32 unw_ino = 0;
        ext4_extent_report_t unw_report;
        ext4_perf_stats_t unw_before;
        ext4_perf_stats_t unw_after;
        char unw_read[128];
        bool unw_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &unw_mnt) == EXT4_OK &&
                      ext4_get_perf_stats(&unw_mnt, &unw_before) == EXT4_OK &&
                      unw_mnt.block_size * 3u <= 16384u &&
                      vfs_create(unwritten_path, 0, 0) == VFS_OK &&
                      ext4_preallocate_file_path(&unw_mnt, "/aurora-unwritten.bin", unw_mnt.block_size * 3u) == EXT4_OK &&
                      ext4_lookup_path(&unw_mnt, "/aurora-unwritten.bin", &unw_inode, &unw_ino) == EXT4_OK &&
                      ext4_inspect_inode_extents(&unw_mnt, &unw_inode, &unw_report) == EXT4_OK &&
                      unw_report.unwritten_blocks == 3u && unw_report.unwritten_extents == 1u &&
                      vfs_read(unwritten_path, 0, unw_read, sizeof(unw_read), &got) == VFS_OK && got == sizeof(unw_read);
        for (usize ui = 0; ui < sizeof(unw_read) && unw_ok; ++ui) unw_ok = unw_read[ui] == 0;
        check(unw_ok, "EXT4 unwritten preallocation reads back as zero-filled data");

        char uw = 'Z';
        wrote = 0;
        bool unw_convert_ok = unw_ok &&
                              vfs_write(unwritten_path, unw_mnt.block_size + 17u, &uw, 1u, &wrote) == VFS_OK && wrote == 1u &&
                              ext4_lookup_path(&unw_mnt, "/aurora-unwritten.bin", &unw_inode, &unw_ino) == EXT4_OK &&
                              ext4_inspect_inode_extents(&unw_mnt, &unw_inode, &unw_report) == EXT4_OK &&
                              unw_report.unwritten_blocks == 2u && unw_report.data_blocks == 3u &&
                              ext4_get_perf_stats(&unw_mnt, &unw_after) == EXT4_OK &&
                              unw_after.unwritten_allocations > unw_before.unwritten_allocations &&
                              unw_after.unwritten_conversions > unw_before.unwritten_conversions;
        memset(unw_read, 0x5a, sizeof(unw_read));
        got = 0;
        unw_convert_ok = unw_convert_ok &&
                         vfs_read(unwritten_path, unw_mnt.block_size + 16u, unw_read, 3u, &got) == VFS_OK &&
                         got == 3u && unw_read[0] == 0 && unw_read[1] == 'Z' && unw_read[2] == 0;
        check(unw_convert_ok, "EXT4 converts a written unwritten extent block without leaking stale data");
        check(vfs_truncate(unwritten_path, 0) == VFS_OK && vfs_unlink(unwritten_path) == VFS_OK, "VFS truncate/unlink ext4 unwritten preallocation file");

        const char *vfs_prealloc_path = "/disk0/aurora-vfs-prealloc.bin";
        (void)vfs_unlink(vfs_prealloc_path);
        bool vfs_prealloc_ok = vfs_create(vfs_prealloc_path, 0, 0) == VFS_OK &&
                               vfs_preallocate(vfs_prealloc_path, unw_mnt.block_size * 5u + 19u) == VFS_OK;
        vfs_stat_t vfs_prealloc_st;
        memset(&vfs_prealloc_st, 0, sizeof(vfs_prealloc_st));
        u8 vfs_prealloc_buf[32];
        memset(vfs_prealloc_buf, 0xa5, sizeof(vfs_prealloc_buf));
        usize vfs_prealloc_got = 0;
        vfs_prealloc_ok = vfs_prealloc_ok && vfs_stat(vfs_prealloc_path, &vfs_prealloc_st) == VFS_OK &&
                          vfs_prealloc_st.size == unw_mnt.block_size * 5u + 19u &&
                          vfs_read(vfs_prealloc_path, unw_mnt.block_size * 4u + 7u, vfs_prealloc_buf, sizeof(vfs_prealloc_buf), &vfs_prealloc_got) == VFS_OK &&
                          vfs_prealloc_got == sizeof(vfs_prealloc_buf);
        bool vfs_prealloc_zero = vfs_prealloc_ok;
        for (usize vi = 0; vi < sizeof(vfs_prealloc_buf); ++vi) if (vfs_prealloc_buf[vi] != 0) vfs_prealloc_zero = false;
        check(vfs_prealloc_ok && vfs_prealloc_zero, "VFS preallocate exposes zero-readable EXT4 unwritten extents");
        const char vfs_prealloc_payload[] = "prealloc-write";
        usize vfs_prealloc_written = 0;
        memset(vfs_prealloc_buf, 0, sizeof(vfs_prealloc_buf));
        bool vfs_prealloc_convert = vfs_write(vfs_prealloc_path, unw_mnt.block_size * 2u + 3u, vfs_prealloc_payload, sizeof(vfs_prealloc_payload) - 1u, &vfs_prealloc_written) == VFS_OK &&
                                    vfs_prealloc_written == sizeof(vfs_prealloc_payload) - 1u &&
                                    vfs_read(vfs_prealloc_path, unw_mnt.block_size * 2u + 3u, vfs_prealloc_buf, sizeof(vfs_prealloc_payload) - 1u, &vfs_prealloc_got) == VFS_OK &&
                                    vfs_prealloc_got == sizeof(vfs_prealloc_payload) - 1u &&
                                    memcmp(vfs_prealloc_buf, vfs_prealloc_payload, sizeof(vfs_prealloc_payload) - 1u) == 0;
        check(vfs_prealloc_convert, "VFS write converts preallocated EXT4 unwritten blocks");
        check(vfs_truncate(vfs_prealloc_path, 0) == VFS_OK && vfs_unlink(vfs_prealloc_path) == VFS_OK, "VFS truncate/unlink ext4 preallocated file");
    } else {
        skip("EXT4 full-block data allocation skips eager zero-write safely");
        skip("VFS unlink ext4 full-block perf file");
        skip("EXT4 buffered data cache serves immediate readback before sync");
        skip("VFS unlink ext4 buffered-data file");
        skip("EXT4 data cache read miss installs clean page and read-ahead");
        skip("VFS unlink ext4 readahead-data file");
        skip("EXT4 data cache pressure triggers bounded writeback");
        skip("VFS unlink ext4 cache-pressure file");
        skip("EXT4 unwritten preallocation reads back as zero-filled data");
        skip("EXT4 converts a written unwritten extent block without leaking stale data");
        skip("VFS truncate/unlink ext4 unwritten preallocation file");
        skip("VFS preallocate exposes zero-readable EXT4 unwritten extents");
        skip("VFS write converts preallocated EXT4 unwritten blocks");
        skip("VFS truncate/unlink ext4 preallocated file");
    }

    const char *indexed_path = "/disk0/aurora-indexed-extents.bin";
    (void)vfs_unlink(indexed_path);
    check(vfs_create(indexed_path, 0, 0) == VFS_OK, "VFS create empty ext4 indexed-extent candidate");
    bool indexed_writes_ok = true;
    for (u32 i = 0; i < 5u; ++i) {
        char ch = (char)('A' + i);
        wrote = 0;
        if (vfs_write(indexed_path, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) indexed_writes_ok = false;
    }
    check(indexed_writes_ok, "EXT4 writes five discontiguous blocks into one file");
    vfs_stat_t indexed_st;
    check(vfs_stat(indexed_path, &indexed_st) == VFS_OK && indexed_st.size == 32769u, "EXT4 indexed extent candidate has sparse final size");
    if (part) {
        ext4_mount_t indexed_mnt;
        ext4_inode_disk_t indexed_inode;
        u32 indexed_ino = 0;
        bool indexed_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &indexed_mnt) == EXT4_OK &&
                            ext4_lookup_path(&indexed_mnt, "/aurora-indexed-extents.bin", &indexed_inode, &indexed_ino) == EXT4_OK &&
                            indexed_ino != 0 && ext4_inode_uses_extents(&indexed_inode) &&
                            ext4_inode_extent_depth(&indexed_inode) == 1u &&
                            ext4_inode_extent_root_entries(&indexed_inode) == 1u;
        check(indexed_tree, "EXT4 promotes overflowed inline extents into indexed leaf tree");
    } else {
        skip("EXT4 promotes overflowed inline extents into indexed leaf tree");
    }
    bool indexed_read_ok = true;
    for (u32 i = 0; i < 5u; ++i) {
        char ch = 0;
        got = 0;
        if (vfs_read(indexed_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('A' + i)) indexed_read_ok = false;
    }
    check(indexed_read_ok, "EXT4 indexed extent data survives sparse readback");
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool indexed_hole_zero = vfs_read(indexed_path, 4096u, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && indexed_hole_zero; ++zi) indexed_hole_zero = zeros[zi] == 0;
    check(indexed_hole_zero, "EXT4 indexed extent hole reads as zero-filled data");
    check(vfs_truncate(indexed_path, 1u) == VFS_OK && vfs_stat(indexed_path, &indexed_st) == VFS_OK && indexed_st.size == 1u, "EXT4 indexed extent file truncates through leaf entries");
    if (part) {
        ext4_mount_t indexed_demote_mnt;
        ext4_inode_disk_t indexed_demote_inode;
        u32 indexed_demote_ino = 0;
        ext4_extent_report_t indexed_demote_extents;
        bool demoted_inline = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &indexed_demote_mnt) == EXT4_OK &&
                              ext4_lookup_path(&indexed_demote_mnt, "/aurora-indexed-extents.bin", &indexed_demote_inode, &indexed_demote_ino) == EXT4_OK &&
                              indexed_demote_ino != 0 &&
                              ext4_inspect_inode_extents(&indexed_demote_mnt, &indexed_demote_inode, &indexed_demote_extents) == EXT4_OK &&
                              indexed_demote_extents.uses_extents && indexed_demote_extents.depth == 0u &&
                              indexed_demote_extents.root_entries == 1u && indexed_demote_extents.leaf_nodes == 0u &&
                              indexed_demote_extents.metadata_blocks == 0u && indexed_demote_extents.data_blocks == 1u &&
                              indexed_demote_extents.errors == 0;
        check(demoted_inline, "EXT4 demotes small truncated indexed extent file back to inline extents");
    } else {
        skip("EXT4 demotes small truncated indexed extent file back to inline extents");
    }
    check(vfs_unlink(indexed_path) == VFS_OK, "VFS unlink ext4 indexed extent file");

    ktest_cleanup_disk_prefix("aurora-extent-split");
    char split_path[96];
    ksnprintf(split_path, sizeof(split_path), "/disk0/aurora-extent-split.bin");
    const char *split_raw = ktest_disk_raw_path(split_path);
    (void)vfs_unlink(split_path);
    u64 split_free_before = 0;
    bool split_baseline_ok = false;
    if (part) {
        ext4_mount_t before_mnt;
        ext4_fsck_report_t before_report;
        split_baseline_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &before_mnt) == EXT4_OK &&
                            ext4_validate_metadata(&before_mnt, &before_report) == EXT4_OK &&
                            before_report.errors == 0;
        split_free_before = before_report.sb_free_blocks;
    }
    check(vfs_create(split_path, 0, 0) == VFS_OK, "VFS create ext4 extent leaf-split stress file");
    vfs_node_ref_t split_ref;
    bool split_ref_ok = vfs_get_ref(split_path, &split_ref) == VFS_OK;
    check(split_ref_ok, "VFS captures ext4 leaf-split inode ref");
    bool split_writes_ok = split_ref_ok;
    enum { EXT4_SPLIT_KTEST_EXTENTS = 96u };
    for (u32 i = 0; split_writes_ok && i < EXT4_SPLIT_KTEST_EXTENTS; ++i) {
        char ch = (char)('a' + (i % 26u));
        wrote = 0;
        if (vfs_write_ref(&split_ref, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) {
            split_writes_ok = false;
            break;
        }
    }
    check(split_writes_ok, "EXT4 writes sparse extents and splits indexed leaves");
    if (part) {
        ext4_mount_t split_mnt;
        ext4_inode_disk_t split_inode;
        u32 split_ino = 0;
        ext4_extent_report_t split_extents;
        bool split_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &split_mnt) == EXT4_OK &&
                          ext4_lookup_path(&split_mnt, split_raw, &split_inode, &split_ino) == EXT4_OK &&
                          split_ino != 0 && ext4_inspect_inode_extents(&split_mnt, &split_inode, &split_extents) == EXT4_OK &&
                          split_extents.uses_extents && split_extents.depth == 1u &&
                          split_extents.root_entries >= 2u && split_extents.leaf_nodes >= 2u &&
                          split_extents.extent_entries == EXT4_SPLIT_KTEST_EXTENTS && split_extents.data_blocks == EXT4_SPLIT_KTEST_EXTENTS &&
                          split_extents.metadata_blocks >= 2u && split_extents.errors == 0;
        check(split_tree, "EXT4 indexed extent root tracks multiple leaf blocks after split");
    } else {
        skip("EXT4 indexed extent root tracks multiple leaf blocks after split");
    }
    bool split_read_ok = true;
    for (u32 i = 0; i < EXT4_SPLIT_KTEST_EXTENTS; i += 17u) {
        char ch = 0;
        got = 0;
        if (vfs_read(split_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('a' + (i % 26u))) {
            split_read_ok = false;
            break;
        }
    }
    check(split_read_ok, "EXT4 multi-leaf indexed extents read back sparse samples");
    memset(zeros, 0x5a, sizeof(zeros));
    got = 0;
    bool split_hole_zero = vfs_read(split_path, 4096u, zeros, sizeof(zeros), &got) == VFS_OK && got == sizeof(zeros);
    for (usize zi = 0; zi < sizeof(zeros) && split_hole_zero; ++zi) split_hole_zero = zeros[zi] == 0;
    check(split_hole_zero, "EXT4 multi-leaf indexed extent holes remain zero-filled");
    check(vfs_truncate(split_path, 17u) == VFS_OK && vfs_stat(split_path, &indexed_st) == VFS_OK && indexed_st.size == 17u, "EXT4 multi-leaf indexed extent file truncates across leaves");
    if (part) {
        ext4_mount_t split_demote_mnt;
        ext4_inode_disk_t split_demote_inode;
        u32 split_demote_ino = 0;
        ext4_extent_report_t split_demote_extents;
        bool split_demoted_inline = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &split_demote_mnt) == EXT4_OK &&
                                    ext4_lookup_path(&split_demote_mnt, split_raw, &split_demote_inode, &split_demote_ino) == EXT4_OK &&
                                    split_demote_ino != 0 &&
                                    ext4_inspect_inode_extents(&split_demote_mnt, &split_demote_inode, &split_demote_extents) == EXT4_OK &&
                                    split_demote_extents.uses_extents && split_demote_extents.depth == 0u &&
                                    split_demote_extents.root_entries == 1u && split_demote_extents.leaf_nodes == 0u &&
                                    split_demote_extents.metadata_blocks == 0u && split_demote_extents.data_blocks == 1u &&
                                    split_demote_extents.errors == 0;
        check(split_demoted_inline, "EXT4 demotes multi-leaf file to inline extents after deep truncate");
    } else {
        skip("EXT4 demotes multi-leaf file to inline extents after deep truncate");
    }
    check(vfs_unlink(split_path) == VFS_OK, "VFS unlink ext4 multi-leaf indexed extent file");

    ktest_cleanup_disk_prefix("aurora-depth2-extents");
    char deep_path[96];
    ksnprintf(deep_path, sizeof(deep_path), "/disk0/aurora-depth2-extents.bin");
    const char *deep_raw = ktest_disk_raw_path(deep_path);
    (void)vfs_unlink(deep_path);
    check(vfs_create(deep_path, 0, 0) == VFS_OK, "VFS create ext4 depth-2 extent stress file");
    vfs_node_ref_t deep_ref;
    bool deep_ref_ok = vfs_get_ref(deep_path, &deep_ref) == VFS_OK;
    check(deep_ref_ok, "VFS captures ext4 depth-2 inode ref");
    bool deep_writes_ok = deep_ref_ok;
    enum { EXT4_DEPTH2_KTEST_EXTENTS = 360u };
    for (u32 i = 0; deep_writes_ok && i < EXT4_DEPTH2_KTEST_EXTENTS; ++i) {
        char ch = (char)('A' + (i % 26u));
        wrote = 0;
        if (vfs_write_ref(&deep_ref, (u64)i * 8192ull, &ch, 1u, &wrote) != VFS_OK || wrote != 1u) {
            deep_writes_ok = false;
            break;
        }
    }
    check(deep_writes_ok, "EXT4 writes enough sparse extents to grow past root leaf-index capacity");
    if (part) {
        ext4_mount_t deep_mnt;
        ext4_inode_disk_t deep_inode;
        u32 deep_ino = 0;
        ext4_extent_report_t deep_extents;
        bool deep_tree = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &deep_mnt) == EXT4_OK &&
                         ext4_lookup_path(&deep_mnt, deep_raw, &deep_inode, &deep_ino) == EXT4_OK &&
                         deep_ino != 0 && ext4_inspect_inode_extents(&deep_mnt, &deep_inode, &deep_extents) == EXT4_OK &&
                         deep_extents.uses_extents && deep_extents.depth == 2u && deep_extents.index_nodes >= 1u &&
                         deep_extents.leaf_nodes >= 5u && deep_extents.extent_entries == EXT4_DEPTH2_KTEST_EXTENTS &&
                         deep_extents.data_blocks == EXT4_DEPTH2_KTEST_EXTENTS && deep_extents.errors == 0;
        check(deep_tree, "EXT4 supports depth > 1 indexed extent tree");
    } else {
        skip("EXT4 supports depth > 1 indexed extent tree");
    }
    bool deep_read_ok = true;
    for (u32 i = 0; i < EXT4_DEPTH2_KTEST_EXTENTS; i += 42u) {
        char ch = 0;
        got = 0;
        if (vfs_read(deep_path, (u64)i * 8192ull, &ch, 1u, &got) != VFS_OK || got != 1u || ch != (char)('A' + (i % 26u))) {
            deep_read_ok = false;
            break;
        }
    }
    check(deep_read_ok, "EXT4 depth-2 extent tree reads back sparse samples");
    check(vfs_truncate(deep_path, 9u) == VFS_OK, "EXT4 depth-2 extent file truncates through internal index nodes");
    if (part) {
        ext4_mount_t deep_demote_mnt;
        ext4_inode_disk_t deep_demote_inode;
        u32 deep_demote_ino = 0;
        ext4_extent_report_t deep_demote_extents;
        bool deep_demoted = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &deep_demote_mnt) == EXT4_OK &&
                            ext4_lookup_path(&deep_demote_mnt, deep_raw, &deep_demote_inode, &deep_demote_ino) == EXT4_OK &&
                            deep_demote_ino != 0 && ext4_inspect_inode_extents(&deep_demote_mnt, &deep_demote_inode, &deep_demote_extents) == EXT4_OK &&
                            deep_demote_extents.uses_extents && deep_demote_extents.depth == 0u && deep_demote_extents.data_blocks == 1u &&
                            deep_demote_extents.metadata_blocks == 0u && deep_demote_extents.errors == 0;
        check(deep_demoted, "EXT4 demotes depth-2 extent file after deep truncate");
    } else {
        skip("EXT4 demotes depth-2 extent file after deep truncate");
    }
    check(vfs_unlink(deep_path) == VFS_OK, "VFS unlink ext4 depth-2 extent file");

    ktest_cleanup_disk_prefix("aurora-htree");
    char htree_dir[96];
    ksnprintf(htree_dir, sizeof(htree_dir), "/disk0/aurora-htree");
    const char *htree_raw_path = ktest_disk_raw_path(htree_dir);
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        (void)vfs_unlink(pbuf);
    }
    (void)vfs_unlink(htree_dir);
    check(vfs_mkdir(htree_dir) == VFS_OK, "VFS mkdir ext4 htree candidate dir");
    bool htree_create_ok = true;
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        char payload = (char)('0' + (i % 10u));
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        if (vfs_create(pbuf, &payload, 1u) != VFS_OK) { htree_create_ok = false; break; }
    }
    check(htree_create_ok, "EXT4 creates enough directory entries to build htree index");
    if (part) {
        ext4_mount_t htree_mnt;
        ext4_inode_disk_t htree_inode;
        u32 htree_ino = 0;
        ext4_fsck_report_t htree_report;
        bool htree_indexed = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &htree_mnt) == EXT4_OK &&
                             ext4_lookup_path(&htree_mnt, htree_raw_path, &htree_inode, &htree_ino) == EXT4_OK &&
                             (htree_inode.i_flags & 0x00001000u) != 0 && htree_inode.i_file_acl_lo != 0 &&
                             ext4_validate_metadata(&htree_mnt, &htree_report) == EXT4_OK && htree_report.htree_dirs >= 1u &&
                             htree_report.htree_entries >= 20u && htree_report.htree_errors == 0;
        check(htree_indexed, "EXT4 maintains persistent htree-indexed directory metadata");
    } else {
        skip("EXT4 maintains persistent htree-indexed directory metadata");
    }
    char htree_child19[128];
    ksnprintf(htree_child19, sizeof(htree_child19), "%s/f19.txt", htree_dir);
    char htree_read = 0;
    got = 0;
    check(vfs_read(htree_child19, 0, &htree_read, 1u, &got) == VFS_OK && got == 1u && htree_read == '9', "EXT4 htree lookup resolves indexed directory entry");
    if (part) {
        ext4_mount_t repair_mnt;
        ext4_inode_disk_t repair_dir;
        u32 repair_dir_ino = 0;
        u8 *htree_block = repair_scratch;
        bool repair_ok = htree_block && ext4_mount_bounded(dev, part->lba_first, part->sector_count, &repair_mnt) == EXT4_OK &&
                         ext4_lookup_path(&repair_mnt, htree_raw_path, &repair_dir, &repair_dir_ino) == EXT4_OK &&
                         repair_dir_ino != 0 && repair_dir.i_file_acl_lo != 0 && repair_mnt.block_size <= 4096u &&
                         ext4_sync_metadata(&repair_mnt) == EXT4_OK;
        if (repair_ok) {
            u64 index_block = repair_dir.i_file_acl_lo;
            u64 first_sector = part->lba_first + (index_block * repair_mnt.block_size) / BLOCKDEV_SECTOR_SIZE;
            u32 sector_count = (u32)(repair_mnt.block_size / BLOCKDEV_SECTOR_SIZE);
            repair_ok = sector_count != 0 && sector_count <= 4096u / BLOCKDEV_SECTOR_SIZE &&
                        block_read(dev, first_sector, sector_count, htree_block) == BLOCK_OK;
            if (repair_ok) {
                htree_block[0] ^= 0x5au;
                repair_ok = block_write(dev, first_sector, sector_count, htree_block) == BLOCK_OK;
            }
        }
        ext4_mount_t corrupt_mnt;
        ext4_fsck_report_t corrupt_report;
        ext4_fsck_report_t repair_report;
        char repaired_read = 0;
        usize repaired_got = 0;
        bool repaired = repair_ok &&
                        ext4_mount_bounded(dev, part->lba_first, part->sector_count, &corrupt_mnt) == EXT4_OK &&
                        ext4_validate_metadata(&corrupt_mnt, &corrupt_report) == EXT4_ERR_CORRUPT &&
                        corrupt_report.htree_errors > 0 &&
                        ext4_repair_metadata(&corrupt_mnt, &repair_report) == EXT4_OK &&
                        repair_report.errors == 0 && repair_report.repaired_htree >= 1u &&
                        vfs_read(htree_child19, 0, &repaired_read, 1u, &repaired_got) == VFS_OK &&
                        repaired_got == 1u && repaired_read == '9';
        check(repaired, "EXT4 repair-lite rebuilds corrupted htree metadata");
    } else {
        skip("EXT4 repair-lite rebuilds corrupted htree metadata");
    }
    for (u32 i = 0; i < 20u; ++i) {
        char pbuf[128];
        ksnprintf(pbuf, sizeof(pbuf), "%s/f%02u.txt", htree_dir, i);
        (void)vfs_unlink(pbuf);
    }
    check(vfs_unlink(htree_dir) == VFS_OK, "VFS unlink empty ext4 htree directory");

    ktest_cleanup_disk_prefix("aurora-dirent-repair");
    char dirent_dir[96];
    char dirent_child[128];
    ksnprintf(dirent_dir, sizeof(dirent_dir), "/disk0/aurora-dirent-repair");
    ksnprintf(dirent_child, sizeof(dirent_child), "%s/file.txt", dirent_dir);
    const char *dirent_raw = ktest_disk_raw_path(dirent_dir);
    (void)vfs_unlink(dirent_child);
    (void)vfs_unlink(dirent_dir);
    check(vfs_mkdir(dirent_dir) == VFS_OK && vfs_create(dirent_child, "R", 1u) == VFS_OK, "VFS prepares ext4 dirent repair target");
    if (part) {
        typedef struct raw_dirent_for_test { u32 inode; u16 rec_len; u8 name_len; u8 file_type; char name[]; } raw_dirent_for_test_t;
        ext4_mount_t dirent_mnt;
        ext4_inode_disk_t dirent_inode;
        u32 dirent_ino = 0;
        u8 *raw_dir_block = repair_scratch;
        bool dirent_repair_ok = raw_dir_block && ext4_mount_bounded(dev, part->lba_first, part->sector_count, &dirent_mnt) == EXT4_OK &&
                                ext4_lookup_path(&dirent_mnt, dirent_raw, &dirent_inode, &dirent_ino) == EXT4_OK &&
                                dirent_ino != 0 && dirent_mnt.block_size <= 4096u &&
                                ext4_sync_metadata(&dirent_mnt) == EXT4_OK;
        if (dirent_repair_ok) {
            const u16 *ew = (const u16 *)dirent_inode.i_block;
            const u32 *ed = (const u32 *)dirent_inode.i_block;
            u64 phys = ((u64)ew[9] << 32) | ed[5];
            u64 first_sector = part->lba_first + (phys * dirent_mnt.block_size) / BLOCKDEV_SECTOR_SIZE;
            u32 sector_count = (u32)(dirent_mnt.block_size / BLOCKDEV_SECTOR_SIZE);
            dirent_repair_ok = ew[0] == EXT4_EXTENT_MAGIC && ew[3] == 0 && phys != 0 && sector_count != 0 &&
                                block_read(dev, first_sector, sector_count, raw_dir_block) == BLOCK_OK;
            if (dirent_repair_ok) {
                bool corrupted = false;
                usize ppos = 0;
                while (ppos + 8u <= dirent_mnt.block_size) {
                    raw_dirent_for_test_t *de = (raw_dirent_for_test_t *)(raw_dir_block + ppos);
                    u16 rec_len = de->rec_len;
                    if (rec_len < 8u || ppos + rec_len > dirent_mnt.block_size) break;
                    if (de->inode && de->name_len == 8u && memcmp(de->name, "file.txt", 8u) == 0) {
                        u16 used = (u16)((8u + de->name_len + 3u) & ~3u);
                        if (rec_len >= used + 8u) {
                            de->rec_len = used;
                            raw_dirent_for_test_t *free_de = (raw_dirent_for_test_t *)(raw_dir_block + ppos + used);
                            memset(free_de, 0, rec_len - used);
                            free_de->rec_len = (u16)(rec_len - used);
                            free_de->name_len = 99u;
                            free_de->file_type = 7u;
                            corrupted = true;
                        }
                        break;
                    }
                    ppos += rec_len;
                }
                dirent_repair_ok = corrupted && block_write(dev, first_sector, sector_count, raw_dir_block) == BLOCK_OK;
            }
        }
        ext4_mount_t broken_dirent_mnt;
        ext4_fsck_report_t broken_dirent_report;
        ext4_fsck_report_t fixed_dirent_report;
        char dirent_payload = 0;
        usize dirent_got = 0;
        bool fixed_dirent = dirent_repair_ok &&
                            ext4_mount_bounded(dev, part->lba_first, part->sector_count, &broken_dirent_mnt) == EXT4_OK &&
                            ext4_validate_metadata(&broken_dirent_mnt, &broken_dirent_report) == EXT4_ERR_CORRUPT && broken_dirent_report.errors > 0 &&
                            ext4_repair_metadata(&broken_dirent_mnt, &fixed_dirent_report) == EXT4_OK && fixed_dirent_report.errors == 0 && fixed_dirent_report.repaired_dirents >= 1u &&
                            vfs_read(dirent_child, 0, &dirent_payload, 1u, &dirent_got) == VFS_OK && dirent_got == 1u && dirent_payload == 'R';
        check(fixed_dirent, "EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata");
    } else {
        skip("EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata");
    }
    check(vfs_unlink(dirent_child) == VFS_OK && vfs_unlink(dirent_dir) == VFS_OK, "VFS cleanup ext4 dirent repair target");

    if (part && split_baseline_ok) {
        ext4_mount_t after_mnt;
        ext4_fsck_report_t after_report;
        bool no_leak = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &after_mnt) == EXT4_OK &&
                       ext4_validate_metadata(&after_mnt, &after_report) == EXT4_OK &&
                       after_report.errors == 0 && after_report.sb_free_blocks == split_free_before;
        check(no_leak, "EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    } else {
        skip("EXT4 multi-leaf indexed extent stress frees data and leaf metadata blocks");
    }

    check(vfs_unlink(renamed_path) == VFS_OK, "VFS unlink ext4 renamed file");
    if (part) {
        ext4_mount_t final_mnt;
        ext4_fsck_report_t final_report;
        bool final_ok = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &final_mnt) == EXT4_OK &&
                        ext4_validate_metadata(&final_mnt, &final_report) == EXT4_OK && final_report.errors == 0;
        check(final_ok, "EXT4 metadata remains consistent after mutation tests");
        ext4_mount_t counter_mnt;
        ext4_fsck_report_t broken_report;
        ext4_fsck_report_t fixed_report;
        bool counter_repair = ext4_mount_bounded(dev, part->lba_first, part->sector_count, &counter_mnt) == EXT4_OK;
        if (counter_repair) {
            counter_mnt.sb.s_free_blocks_count_lo ^= 7u;
            counter_mnt.sb.s_free_inodes_count ^= 3u;
            counter_repair = ext4_validate_metadata(&counter_mnt, &broken_report) == EXT4_ERR_CORRUPT && broken_report.errors > 0 &&
                             ext4_repair_metadata(&counter_mnt, &fixed_report) == EXT4_OK && fixed_report.errors == 0 &&
                             fixed_report.repaired_counters >= 1u;
        }
        check(counter_repair, "EXT4 repair-lite reconciles corrupted free counters");
    } else {
        skip("EXT4 metadata remains consistent after mutation tests");
        skip("EXT4 repair-lite reconciles corrupted free counters");
    }
    kfree(repair_scratch);
    suite_end();
}

static void test_syscall_task_timer_elf(void) {
    suite_begin("syscall/task/timer/elf/log");
    syscall_result_t r = syscall_dispatch(AURORA_SYS_VERSION, 0, 0, 0, 0, 0, 0);
    check(r.error == 0 && r.value == (i64)AURORA_SYSCALL_ABI_VERSION, "Rust syscall ABI version matches central kernel version");
    check(aurora_rust_syscall_selftest(), "Rust syscall decoder/validator selftest");
    r = syscall_dispatch(AURORA_SYS_CLOSE, AURORA_PROCESS_HANDLE_CAP, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall rejects handle equal to ABI cap before C backend");
    char cwd_buf[VFS_PATH_MAX];
    memset(cwd_buf, 0, sizeof(cwd_buf));
    r = syscall_dispatch(AURORA_SYS_GETCWD, (u64)(uptr)cwd_buf, sizeof(cwd_buf), 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_buf, "/") == 0, "syscall getcwd returns initial root cwd");
    r = syscall_dispatch(AURORA_SYS_CHDIR, (u64)(uptr)"/disk0", 0, 0, 0, 0, 0);
    check(r.error == 0, "syscall chdir accepts persistent EXT4 directory");
    memset(cwd_buf, 0, sizeof(cwd_buf));
    r = syscall_dispatch(AURORA_SYS_GETCWD, (u64)(uptr)cwd_buf, sizeof(cwd_buf), 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_buf, "/disk0") == 0, "syscall getcwd reflects changed cwd");
    aurora_statvfs_t cwd_sv;
    memset(&cwd_sv, 0, sizeof(cwd_sv));
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)".", (u64)(uptr)&cwd_sv, 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_sv.fs_name, "ext4") == 0, "relative syscall path resolves against cwd");
    r = syscall_dispatch(AURORA_SYS_CHDIR, (u64)(uptr)"/", 0, 0, 0, 0, 0);
    check(r.error == 0, "syscall chdir restores root cwd");
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)"/bad\\path", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall rejects disallowed path bytes before C backend");
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)"/tmp/too-large", (u64)(uptr)"x", 65537u, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall caps create payload size");
    r = syscall_dispatch(AURORA_SYS_STAT, (u64)(uptr)"/etc/motd", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall requires stat output pointer");
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)"/disk0", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall requires statvfs output pointer");
    aurora_statvfs_t sys_sv;
    memset(&sys_sv, 0, sizeof(sys_sv));
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)"/disk0", (u64)(uptr)&sys_sv, 0, 0, 0, 0);
    check(r.error == 0 && sys_sv.block_size >= 1024u && sys_sv.total_blocks > 0u && sys_sv.free_blocks <= sys_sv.total_blocks && strcmp(sys_sv.fs_name, "ext4") == 0, "Rust-dispatched statvfs returns EXT4 capacity");
    r = syscall_dispatch(AURORA_SYS_SYNC, 0, 0, 0, 0, 0, 0);
    check(r.error == 0, "Rust-dispatched sync flushes mounted filesystems");

    check(syscall_selftest(), "Rust-dispatched syscall filesystem/process-control selftest");
    check(strcmp(syscall_name(AURORA_SYS_WRITE), "write") == 0 && strcmp(syscall_name(AURORA_SYS_GETPID), "getpid") == 0 && strcmp(syscall_name(AURORA_SYS_PROCINFO), "procinfo") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWN), "spawn") == 0 && strcmp(syscall_name(AURORA_SYS_WAIT), "wait") == 0 && strcmp(syscall_name(AURORA_SYS_YIELD), "yield") == 0 && strcmp(syscall_name(AURORA_SYS_SLEEP), "sleep") == 0 && strcmp(syscall_name(AURORA_SYS_SCHEDINFO), "schedinfo") == 0 && strcmp(syscall_name(AURORA_SYS_DUP), "dup") == 0 && strcmp(syscall_name(AURORA_SYS_TELL), "tell") == 0 && strcmp(syscall_name(AURORA_SYS_FSTAT), "fstat") == 0 && strcmp(syscall_name(AURORA_SYS_FDINFO), "fdinfo") == 0 && strcmp(syscall_name(AURORA_SYS_READDIR), "readdir") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWNV), "spawnv") == 0 && strcmp(syscall_name(AURORA_SYS_PREEMPTINFO), "preemptinfo") == 0 && strcmp(syscall_name(AURORA_SYS_TRUNCATE), "truncate") == 0 && strcmp(syscall_name(AURORA_SYS_RENAME), "rename") == 0 && strcmp(syscall_name(AURORA_SYS_SYNC), "sync") == 0 && strcmp(syscall_name(AURORA_SYS_FSYNC), "fsync") == 0 && strcmp(syscall_name(AURORA_SYS_STATVFS), "statvfs") == 0 && strcmp(syscall_name(AURORA_SYS_INSTALL_COMMIT), "install_commit") == 0 && strcmp(syscall_name(AURORA_SYS_CHDIR), "chdir") == 0 && strcmp(syscall_name(AURORA_SYS_GETCWD), "getcwd") == 0 && strcmp(syscall_name(999), "unknown") == 0, "syscall name table");
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
    check(vfs_stat("/bin/execfdcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execfdcheck fd inheritance/CLOEXEC ELF");
    check(vfs_stat("/bin/execvecheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/execvecheck env ELF");
    check(vfs_stat("/bin/exectarget", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/exectarget exec target ELF");
    check(vfs_stat("/bin/pipecheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/pipecheck IPC pipe ELF");
    check(vfs_stat("/bin/fdremapcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/fdremapcheck dup2 ELF");
    check(vfs_stat("/bin/pollcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/pollcheck readiness ELF");
    check(vfs_stat("/bin/stdcat", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/stdcat stdio copy ELF");
    check(vfs_stat("/bin/termcheck", &st) == VFS_OK && st.type == VFS_NODE_FILE && st.size > 64u, "VFS stat /bin/termcheck terminal ELF");
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
    check(badpath_ok, "ring3 /bin/badpath exercises path-policy and cwd-relative syscalls");
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

    const char *execfd_argv[] = { "/bin/execfdcheck" };
    ps = process_exec("/bin/execfdcheck", 1, execfd_argv, &r);
    bool execfd_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execfd_ok, "ring3 /bin/execfdcheck preserves non-CLOEXEC fd across execv");
    if (!execfd_ok) detail_process_result("/bin/execfdcheck", ps, &r);

    const char *execve_argv[] = { "/bin/execvecheck" };
    ps = process_exec("/bin/execvecheck", 1, execve_argv, &r);
    bool execve_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execve_ok, "ring3 /bin/execvecheck replaces image with argv+envp");
    if (!execve_ok) detail_process_result("/bin/execvecheck", ps, &r);

    const char *execfdclo_argv[] = { "/bin/execfdcheck", "cloexec" };
    ps = process_exec("/bin/execfdcheck", 2, execfdclo_argv, &r);
    bool execfdclo_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0 && strcmp(r.name, "/bin/exectarget") == 0;
    check(execfdclo_ok, "ring3 /bin/execfdcheck closes FD_CLOEXEC handles across execv");
    if (!execfdclo_ok) detail_process_result("/bin/execfdcheck", ps, &r);

    const char *fork_argv[] = { "/bin/forkcheck" };
    ps = process_exec("/bin/forkcheck", 1, fork_argv, &r);
    bool fork_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fork_ok, "ring3 /bin/forkcheck clones address space and waits child fork");
    if (!fork_ok) detail_process_result("/bin/forkcheck", ps, &r);

    const char *pipe_argv[] = { "/bin/pipecheck" };
    ps = process_exec("/bin/pipecheck", 1, pipe_argv, &r);
    bool pipe_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(pipe_ok, "ring3 /bin/pipecheck exercises pipes, stdio remap AURORA_STDIN AURORA_STDOUT AURORA_STDERR and fork+exec pipeline");
    if (!pipe_ok) detail_process_result("/bin/pipecheck", ps, &r);

    const char *fdremap_argv[] = { "/bin/fdremapcheck" };
    ps = process_exec("/bin/fdremapcheck", 1, fdremap_argv, &r);
    bool fdremap_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(fdremap_ok, "ring3 /bin/fdremapcheck exercises dup2 fd remapping");
    if (!fdremap_ok) detail_process_result("/bin/fdremapcheck", ps, &r);

    const char *poll_argv[] = { "/bin/pollcheck" };
    ps = process_exec("/bin/pollcheck", 1, poll_argv, &r);
    bool poll_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(poll_ok, "ring3 /bin/pollcheck exercises pipe/file readiness");
    if (!poll_ok) detail_process_result("/bin/pollcheck", ps, &r);

    const char *stdcat_argv[] = { "/bin/stdcat", "/disk0/hello.txt" };
    ps = process_exec("/bin/stdcat", 2, stdcat_argv, &r);
    bool stdcat_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(stdcat_ok, "ring3 /bin/stdcat copies VFS file to stdout");
    if (!stdcat_ok) detail_process_result("/bin/stdcat", ps, &r);


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

    const char *term_argv[] = { "/bin/termcheck" };
    ps = process_exec("/bin/termcheck", 1, term_argv, &r);
    bool term_ok = ps == PROC_OK && !r.faulted && r.exit_code == 0;
    check(term_ok, "ring3 /bin/termcheck exercises TTY terminal ABI");
    if (!term_ok) detail_process_result("/bin/termcheck", ps, &r);

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
    check(strcmp(syscall_name(AURORA_SYS_EXEC), "exec") == 0 && strcmp(syscall_name(AURORA_SYS_EXECV), "execv") == 0 && strcmp(syscall_name(AURORA_SYS_EXECVE), "execve") == 0 && strcmp(syscall_name(AURORA_SYS_FDCTL), "fdctl") == 0 && strcmp(syscall_name(AURORA_SYS_DUP2), "dup2") == 0 && strcmp(syscall_name(AURORA_SYS_POLL), "poll") == 0 && strcmp(syscall_name(AURORA_SYS_TTY_GETINFO), "tty_getinfo") == 0 && strcmp(syscall_name(AURORA_SYS_TTY_SETMODE), "tty_setmode") == 0 && strcmp(syscall_name(AURORA_SYS_TTY_READKEY), "tty_readkey") == 0, "Rust-dispatched exec/fdctl syscall names are stable");
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
