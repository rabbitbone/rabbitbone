#pragma once

#include <rabbitbone/ktest.h>
#include <rabbitbone/bitmap.h>
#include <rabbitbone/block.h>
#include <rabbitbone/bootinfo.h>
#include <rabbitbone/console.h>
#include <rabbitbone/crc32.h>
#include <rabbitbone/format.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/elf64.h>
#include <rabbitbone/ext4.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/mbr.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/hpet.h>
#include <rabbitbone/smp.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/path.h>
#include <rabbitbone/ringbuf.h>
#include <rabbitbone/syscall.h>
#include <rabbitbone/task.h>
#include <rabbitbone/scheduler.h>
#include <rabbitbone/tarfs.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/process.h>
#include <rabbitbone/rust.h>
#include <rabbitbone/version.h>
#include <rabbitbone/tty.h>

static ktest_totals_t totals;
static const char *current_suite;
static u32 suite_failed;
static u32 suite_passed;
static u32 suite_skipped;
static bool ktest_verbose_passes;

static void ktest_log_line(log_level_t level, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    log_vwrite(level, "ktest", fmt, ap);
    __builtin_va_end(ap);
}

static void suite_begin(const char *name) {
    current_suite = name;
    suite_failed = 0;
    suite_passed = 0;
    suite_skipped = 0;
    ++totals.suites;
    kprintf("\n[ suite ] %s\n", name);
    ktest_log_line(LOG_INFO, "[ suite ] %s", name);
}

static void suite_end(void) {
    kprintf("[ result] %s: passed=%u failed=%u skipped=%u\n", current_suite, suite_passed, suite_failed, suite_skipped);
    ktest_log_line(suite_failed ? LOG_ERROR : LOG_INFO, "[ result] %s: passed=%u failed=%u skipped=%u", current_suite, suite_passed, suite_failed, suite_skipped);
}

static void pass(const char *name) {
    ++totals.passed;
    ++suite_passed;
    ktest_log_line(LOG_INFO, "[ ok    ] %s", name);
    if (ktest_verbose_passes) kprintf("[ ok    ] %s\n", name);
}

static void fail(const char *name) {
    ++totals.failed;
    ++suite_failed;
    kprintf("[ fail  ] %s\n", name);
    ktest_log_line(LOG_ERROR, "[ fail  ] %s", name);
}

static void skip(const char *name) {
    ++totals.skipped;
    ++suite_skipped;
    kprintf("[ skip  ] %s\n", name);
    ktest_log_line(LOG_WARN, "[ skip  ] %s", name);
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
    strlcpy(b->names[b->count], e->name, VFS_NAME_MAX);
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
                int n = ksnprintf(child, sizeof(child), "%s/%s", path, batch->names[i]);
                if (n < 0 || (usize)n >= sizeof(child)) continue;
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

static bool ktest_restore_raw_block(block_device_t *dev, u64 first_sector, u32 sector_count, const void *original) {
    if (!dev || !original || sector_count == 0) return false;
    for (u32 attempt = 0; attempt < 3u; ++attempt) {
        if (block_write(dev, first_sector, sector_count, original) == BLOCK_OK) return true;
    }
    return false;
}

static bool ktest_cleanup_disk_prefix(const char *prefix) {
    if (!prefix || !prefix[0]) return true;
    for (u32 pass = 0; pass < 256u; ++pass) {
        vfs_status_t lv = VFS_OK;
        ktest_cleanup_batch_t *batch = ktest_cleanup_collect("/disk0", prefix, &lv);
        if (!batch) return false;
        if (batch->count == 0) { kfree(batch); return true; }
        bool removed_any = false;
        for (u32 i = 0; i < batch->count; ++i) {
            char path[VFS_PATH_MAX];
            int n = ksnprintf(path, sizeof(path), "/disk0/%s", batch->names[i]);
            if (n < 0 || (usize)n >= sizeof(path)) continue;
            if (ktest_remove_tree(path)) removed_any = true;
        }
        bool overflow = batch->overflow;
        kfree(batch);
        if (!removed_any && !overflow) return false;
    }
    return false;
}

typedef struct seen_name_ctx {
    const char *name;
    bool seen;
} seen_name_ctx_t;

static bool list_seen_name(const vfs_dirent_t *e, void *ctx) {
    seen_name_ctx_t *s = (seen_name_ctx_t *)ctx;
    if (!e || !s || !s->name) return true;
    if (strcmp(e->name, s->name) == 0) { s->seen = true; return false; }
    return true;
}

static bool list_count_cb(const vfs_dirent_t *e, void *ctx) {
    (void)e;
    u32 *n = (u32 *)ctx;
    if (!n || *n == ((u32)~0u)) return false;
    ++*n;
    return true;
}

static bool ext4_seen_name(const ext4_dirent_t *e, void *ctx) {
    seen_name_ctx_t *s = (seen_name_ctx_t *)ctx;
    if (strcmp(e->name, s->name) == 0) { s->seen = true; return false; }
    return true;
}


static block_device_t *ktest_find_linux_block_device(mbr_table_t *mbr_out, const mbr_partition_t **part_out) {
    for (usize i = 0; i < block_count(); ++i) {
        block_device_t *dev = block_get(i);
        if (!dev) continue;
        mbr_table_t mbr;
        if (!mbr_read(dev, &mbr)) continue;
        const mbr_partition_t *part = mbr_find_linux_on_device(dev, &mbr);
        if (!part) continue;
        usize idx = (usize)(part - mbr.part);
        if (mbr_out) {
            *mbr_out = mbr;
            if (part_out) *part_out = &mbr_out->part[idx];
        } else if (part_out) {
            *part_out = 0;
        }
        return dev;
    }
    return 0;
}

static block_status_t ktest_block_read_should_not_run(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    (void)dev; (void)lba; (void)count; (void)buffer;
    return BLOCK_ERR_IO;
}

