#include <aurora/memory.h>
#include <aurora/bitmap.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/libc.h>
#include <aurora/panic.h>
#include <aurora/spinlock.h>

extern char __kernel_start[];
extern char __kernel_end[];

#define MAX_TRACKED_FRAMES (1024u * 1024u)
#define FRAME_BITMAP_PHYS 0x100000ull
#define FRAME_BITMAP_BYTES (MAX_TRACKED_FRAMES / 8u)
static bitmap_t frames;
static u64 frame_count;
static u64 total_usable;
static spinlock_t memory_lock;

static bool add_overflow_u64(u64 a, u64 b, u64 *out) { return __builtin_add_overflow(a, b, out); }
static u64 saturating_add_u64(u64 a, u64 b) { u64 out = 0; return add_overflow_u64(a, b, &out) ? (u64)-1 : out; }

static bool range_end_checked(u64 base, u64 length, u64 *end_out) {
    if (length == 0 || !end_out) return false;
    return !add_overflow_u64(base, length, end_out) && *end_out >= base;
}

static bool ranges_overlap_u64(u64 a_base, u64 a_len, u64 b_base, u64 b_len) {
    u64 a_end = 0;
    u64 b_end = 0;
    if (!range_end_checked(a_base, a_len, &a_end) || !range_end_checked(b_base, b_len, &b_end)) return false;
    return a_base < b_end && b_base < a_end;
}

static bool range_intersects_reserved(u64 base, u64 length) {
    if (length == 0) return true;
    if (ranges_overlap_u64(base, length, 0, 0x200000ull)) return true;
    if (ranges_overlap_u64(base, length, (u64)(uptr)__kernel_start, (u64)(uptr)__kernel_end - (u64)(uptr)__kernel_start)) return true;
    if (ranges_overlap_u64(base, length, FRAME_BITMAP_PHYS, FRAME_BITMAP_BYTES)) return true;
    return false;
}

static void mark_range(u64 base, u64 length, bool free) {
    u64 end_addr = 0;
    u64 start_addr = 0;
    if (!range_end_checked(base, length, &end_addr)) return;
    if (!aurora_align_up_u64_checked(base, PAGE_SIZE, &start_addr)) return;
    u64 start = start_addr / PAGE_SIZE;
    u64 end = AURORA_ALIGN_DOWN(end_addr, PAGE_SIZE) / PAGE_SIZE;
    if (start >= frame_count || end <= start) return;
    if (end > frame_count) end = frame_count;
    for (u64 f = start; f < end; ++f) {
        if (free) bitmap_clear(&frames, (usize)f);
        else bitmap_set(&frames, (usize)f);
    }
}

void memory_init(const aurora_bootinfo_t *bootinfo) {
    u64 flags = spin_lock_irqsave(&memory_lock);
    u64 max_addr = 0;
    total_usable = 0;
    if (bootinfo_validate(bootinfo)) {
        for (u16 i = 0; i < bootinfo->e820_count; ++i) {
            const aurora_e820_entry_t *e = bootinfo_e820(bootinfo, i);
            if (!e || e->length == 0) continue;
            u64 end_addr = saturating_add_u64(e->base, e->length);
            if (end_addr > max_addr) max_addr = end_addr;
            if (e->type == AURORA_E820_USABLE) total_usable = saturating_add_u64(total_usable, e->length);
        }
    }
    if (max_addr < 32ull * 1024ull * 1024ull) max_addr = 32ull * 1024ull * 1024ull;
    frame_count = max_addr / PAGE_SIZE;
    if (frame_count > MAX_TRACKED_FRAMES) frame_count = MAX_TRACKED_FRAMES;
    bitmap_init(&frames, (u64 *)(uptr)FRAME_BITMAP_PHYS, (usize)frame_count);
    memset((void *)(uptr)FRAME_BITMAP_PHYS, 0xff, FRAME_BITMAP_BYTES);
    for (u64 f = 0; f < frame_count; ++f) bitmap_set(&frames, (usize)f);

    if (bootinfo_validate(bootinfo)) {
        for (u16 i = 0; i < bootinfo->e820_count; ++i) {
            const aurora_e820_entry_t *e = bootinfo_e820(bootinfo, i);
            if (e && e->type == AURORA_E820_USABLE) mark_range(e->base, e->length, true);
        }
    } else {
        mark_range(0x200000, max_addr - 0x200000, true);
    }

    mark_range(0, 0x200000, false);
    mark_range((u64)(uptr)__kernel_start, (u64)(uptr)__kernel_end - (u64)(uptr)__kernel_start, false);
    mark_range(FRAME_BITMAP_PHYS, FRAME_BITMAP_BYTES, false);
    spin_unlock_irqrestore(&memory_lock, flags);
    KLOG(LOG_INFO, "mem", "frames=%llu usable=%llu KiB", (unsigned long long)frame_count, (unsigned long long)(total_usable / 1024));
}

void *memory_alloc_contiguous_pages_below(usize pages, u64 max_exclusive) {
    if (pages == 0 || max_exclusive < PAGE_SIZE) return 0;
    u64 flags = spin_lock_irqsave(&memory_lock);
    usize max_bit = (usize)(max_exclusive / PAGE_SIZE);
    if (max_bit > frame_count) max_bit = (usize)frame_count;
    if (pages > max_bit) {
        spin_unlock_irqrestore(&memory_lock, flags);
        return 0;
    }

    for (usize bit = 0; bit + pages <= max_bit; ++bit) {
        bool free_run = true;
        for (usize j = 0; j < pages; ++j) {
            if (bitmap_test(&frames, bit + j)) {
                free_run = false;
                bit += j;
                break;
            }
        }
        if (!free_run) continue;
        for (usize j = 0; j < pages; ++j) bitmap_set(&frames, bit + j);
        spin_unlock_irqrestore(&memory_lock, flags);
        return (void *)(uptr)(bit * PAGE_SIZE);
    }
    spin_unlock_irqrestore(&memory_lock, flags);
    return 0;
}

void *memory_alloc_page_below(u64 max_exclusive) {
    return memory_alloc_contiguous_pages_below(1u, max_exclusive);
}

void *memory_alloc_page(void) {
    return memory_alloc_page_below(MEMORY_KERNEL_DIRECT_LIMIT);
}

void memory_free_contiguous_pages(void *base, usize pages) {
    if (!base || pages == 0) return;
    uptr addr = (uptr)base;
    if (addr % PAGE_SIZE) return;
    u64 length = 0;
    if (__builtin_mul_overflow((u64)pages, (u64)PAGE_SIZE, &length) || range_intersects_reserved((u64)addr, length)) return;
    u64 flags = spin_lock_irqsave(&memory_lock);
    usize bit = addr / PAGE_SIZE;
    if (bit >= frame_count || pages > frame_count - bit) {
        spin_unlock_irqrestore(&memory_lock, flags);
        return;
    }
    for (usize i = 0; i < pages; ++i) {
        if (!bitmap_test(&frames, bit + i)) PANIC("physical double free frame=%llu", (unsigned long long)(bit + i));
    }
    for (usize i = 0; i < pages; ++i) bitmap_clear(&frames, bit + i);
    spin_unlock_irqrestore(&memory_lock, flags);
}

void memory_free_page(void *page) {
    memory_free_contiguous_pages(page, 1u);
}

void memory_get_stats(memory_stats_t *out) {
    if (!out) return;
    u64 flags = spin_lock_irqsave(&memory_lock);
    usize used = bitmap_count_set(&frames);
    out->frame_count = frame_count;
    out->free_frames = frame_count - used;
    out->free_bytes = out->free_frames * PAGE_SIZE;
    out->used_bytes = used * PAGE_SIZE;
    out->total_usable = total_usable;
    spin_unlock_irqrestore(&memory_lock, flags);
}

void memory_dump_map(void) {
    memory_stats_t s;
    memory_get_stats(&s);
    kprintf("memory: total_usable=%llu KiB frames=%llu free=%llu used=%llu\n",
            (unsigned long long)(s.total_usable / 1024),
            (unsigned long long)s.frame_count,
            (unsigned long long)s.free_frames,
            (unsigned long long)(s.used_bytes / 1024));
}


bool memory_selftest(void) {
    memory_stats_t before;
    memory_get_stats(&before);
    void *a = memory_alloc_page();
    void *b = memory_alloc_page();
    void *c = memory_alloc_contiguous_pages_below(3u, 1024ull * 1024ull * 1024ull);
    bool ok = a && b && c && a != b;
    if (ok) ok = (((uptr)a & (PAGE_SIZE - 1u)) == 0 && ((uptr)b & (PAGE_SIZE - 1u)) == 0 && ((uptr)c & (PAGE_SIZE - 1u)) == 0);
    if (a) memory_free_page(a);
    if (b) memory_free_page(b);
    if (c) memory_free_contiguous_pages(c, 3u);
    if (!ok) return false;
    memory_stats_t after;
    memory_get_stats(&after);
    return after.free_frames >= before.free_frames;
}
