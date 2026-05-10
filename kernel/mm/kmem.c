#include <aurora/kmem.h>
#include <aurora/memory.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/panic.h>

#if defined(AURORA_HOST_TEST)
#include <stdlib.h>
#undef PANIC
#define PANIC(...) abort()
#undef KLOG
#define KLOG(...) ((void)0)
#endif

#define KMEM_MAGIC_ALLOC 0xA0B0C0D0E0F01234ull
#define KMEM_MAGIC_FREE  0x43210F0E0D0C0B0Aull
#define KMEM_MIN_SPLIT   32u
#define KMEM_BOOT_PAGES  16u
#define KMALLOC_MAX       (64ull * 1024ull * 1024ull)

typedef struct kmem_block {
    u64 magic;
    usize size;
    bool free;
    struct kmem_block *prev;
    struct kmem_block *next;
} kmem_block_t;

static kmem_block_t *heap_head;
static kmem_block_t *heap_tail;
static kmem_stats_t stats;
static bool initialized;

static bool align16_checked(usize n, usize *out) {
    usize t = 0;
    if (__builtin_add_overflow(n, (usize)15u, &t)) return false;
    if (out) *out = t & ~(usize)15u;
    return true;
}

static usize align16(usize n) { usize out = 0; return align16_checked(n, &out) ? out : 0; }
static void *block_payload(kmem_block_t *b) { return (void *)(b + 1); }
static kmem_block_t *payload_block(void *p) { return ((kmem_block_t *)p) - 1; }

static void insert_block(kmem_block_t *b) {
    b->next = 0;
    b->prev = heap_tail;
    if (heap_tail) heap_tail->next = b;
    else heap_head = b;
    heap_tail = b;
}

static u8 *block_end(kmem_block_t *b) {
    return b ? ((u8 *)block_payload(b)) + b->size : 0;
}

static bool blocks_physically_adjacent(kmem_block_t *left, kmem_block_t *right) {
    return left && right && block_end(left) == (u8 *)right;
}

static void merge_forward(kmem_block_t *b) {
    while (b && b->next && b->free && b->next->free && blocks_physically_adjacent(b, b->next)) {
        kmem_block_t *n = b->next;
        b->size += sizeof(kmem_block_t) + n->size;
        stats.free_bytes += sizeof(kmem_block_t);
        b->next = n->next;
        if (n->next) n->next->prev = b;
        else heap_tail = b;
        n->magic = 0;
    }
}

static void split_block(kmem_block_t *b, usize wanted) {
    wanted = align16(wanted);
    if (!b || !b->free) return;
    usize threshold = 0;
    if (__builtin_add_overflow(wanted, sizeof(kmem_block_t), &threshold) ||
        __builtin_add_overflow(threshold, (usize)KMEM_MIN_SPLIT, &threshold) ||
        b->size < threshold) return;
    u8 *raw = (u8 *)block_payload(b);
    kmem_block_t *rest = (kmem_block_t *)(raw + wanted);
    rest->magic = KMEM_MAGIC_FREE;
    rest->size = b->size - wanted - sizeof(kmem_block_t);
    rest->free = true;
    rest->prev = b;
    rest->next = b->next;
    if (rest->next) rest->next->prev = rest;
    else heap_tail = rest;
    b->next = rest;
    b->size = wanted;
    stats.free_bytes -= sizeof(kmem_block_t);
}

static void split_allocated_block(kmem_block_t *b, usize wanted) {
    wanted = align16(wanted);
    if (!b || b->free) return;
    usize threshold = 0;
    if (__builtin_add_overflow(wanted, sizeof(kmem_block_t), &threshold) ||
        __builtin_add_overflow(threshold, (usize)KMEM_MIN_SPLIT, &threshold) ||
        b->size < threshold) return;
    usize old_size = b->size;
    u8 *raw = (u8 *)block_payload(b);
    kmem_block_t *rest = (kmem_block_t *)(raw + wanted);
    rest->magic = KMEM_MAGIC_FREE;
    rest->size = old_size - wanted - sizeof(kmem_block_t);
    rest->free = true;
    rest->prev = b;
    rest->next = b->next;
    if (rest->next) rest->next->prev = rest;
    else heap_tail = rest;
    b->next = rest;
    b->size = wanted;
    stats.used_bytes -= old_size - wanted;
    stats.free_bytes += rest->size;
    merge_forward(rest);
}

static bool add_heap_pages(usize pages) {
    if (pages == 0 || pages > ((usize)-1) / PAGE_SIZE) return false;
    usize bytes = pages * PAGE_SIZE;
    if (bytes <= sizeof(kmem_block_t)) return false;
#if defined(AURORA_HOST_TEST)
    void *mem = malloc(bytes);
    if (!mem) return false;
#else
    void *mem = memory_alloc_contiguous_pages_below(pages, MEMORY_KERNEL_DIRECT_LIMIT);
#endif
    if (!mem) return false;
    kmem_block_t *b = (kmem_block_t *)mem;
    b->magic = KMEM_MAGIC_FREE;
    b->size = bytes - sizeof(kmem_block_t);
    b->free = true;
    b->prev = b->next = 0;
    insert_block(b);
    stats.heap_bytes += bytes;
    stats.free_bytes += b->size;
    stats.page_bytes_requested += bytes;
    return true;
}

void kmem_init(void) {
    memset(&stats, 0, sizeof(stats));
    heap_head = heap_tail = 0;
    initialized = true;
    if (!add_heap_pages(KMEM_BOOT_PAGES)) PANIC("kernel heap bootstrap failed");
    KLOG(LOG_INFO, "kmem", "heap initialized: %llu KiB", (unsigned long long)(stats.heap_bytes / 1024));
}

void *kmalloc(usize size) {
    if (!initialized) kmem_init();
    if (size == 0) return 0;
    if (size > KMALLOC_MAX || !align16_checked(size, &size) || size == 0) { stats.failed_allocations++; return 0; }
retry:
    for (kmem_block_t *b = heap_head; b; b = b->next) {
        if (b->magic != KMEM_MAGIC_FREE && b->magic != KMEM_MAGIC_ALLOC) PANIC("heap metadata corruption at %p", b);
        if (b->free && b->size >= size) {
            split_block(b, size);
            b->free = false;
            b->magic = KMEM_MAGIC_ALLOC;
            stats.used_bytes += b->size;
            stats.free_bytes -= b->size;
            stats.allocation_count++;
            memset(block_payload(b), 0, b->size);
            return block_payload(b);
        }
    }
    usize needed = 0;
    if (__builtin_add_overflow(size, sizeof(kmem_block_t), &needed) ||
        __builtin_add_overflow(needed, (usize)PAGE_SIZE - 1u, &needed)) {
        stats.failed_allocations++;
        return 0;
    }
    usize pages = needed / PAGE_SIZE;
    if (pages < 4u) pages = 4u;
    if (add_heap_pages(pages)) goto retry;
    stats.failed_allocations++;
    KLOG(LOG_ERROR, "kmem", "allocation failed: %llu", (unsigned long long)size);
    return 0;
}

void *kcalloc(usize count, usize size) {
    if (size && count > ((usize)-1) / size) return 0;
    return kmalloc(count * size);
}

void *krealloc(void *ptr, usize new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) { kfree(ptr); return 0; }
    kmem_block_t *b = payload_block(ptr);
    if (b->magic != KMEM_MAGIC_ALLOC || b->free) PANIC("krealloc on invalid pointer %p", ptr);
    if (new_size > KMALLOC_MAX || !align16_checked(new_size, &new_size) || new_size == 0) return 0;
    if (b->size >= new_size) {
        split_allocated_block(b, new_size);
        return ptr;
    }
    void *np = kmalloc(new_size);
    if (!np) return 0;
    memcpy(np, ptr, b->size);
    kfree(ptr);
    return np;
}

void kfree(void *ptr) {
    if (!ptr) return;
    kmem_block_t *b = payload_block(ptr);
    if (b->magic != KMEM_MAGIC_ALLOC || b->free) PANIC("double free or invalid free at %p", ptr);
    b->free = true;
    b->magic = KMEM_MAGIC_FREE;
    stats.used_bytes -= b->size;
    stats.free_bytes += b->size;
    if (b->prev && b->prev->free && blocks_physically_adjacent(b->prev, b)) b = b->prev;
    merge_forward(b);
}

char *kstrdup(const char *s) {
    if (!s) return 0;
    usize len = strlen(s) + 1u;
    char *out = (char *)kmalloc(len);
    if (!out) return 0;
    memcpy(out, s, len);
    return out;
}

void kmem_get_stats(kmem_stats_t *out) {
    if (out) *out = stats;
}

void kmem_dump(void) {
    kmem_stats_t s;
    kmem_get_stats(&s);
    kprintf("kmem: heap=%llu KiB used=%llu free=%llu allocs=%llu fails=%llu\n",
            (unsigned long long)(s.heap_bytes / 1024),
            (unsigned long long)s.used_bytes,
            (unsigned long long)s.free_bytes,
            (unsigned long long)s.allocation_count,
            (unsigned long long)s.failed_allocations);
    usize i = 0;
    for (kmem_block_t *b = heap_head; b && i < 16u; b = b->next, ++i) {
        kprintf("  block%llu %p size=%llu %s\n", (unsigned long long)i, b,
                (unsigned long long)b->size, b->free ? "free" : "used");
    }
}

bool kmem_selftest(void) {
    void *a = kmalloc(24);
    void *b = kmalloc(4096);
    void *c = kcalloc(8, 32);
    if (!a || !b || !c) return false;
    memset(a, 0x11, 24);
    memset(b, 0x22, 4096);
    void *d = krealloc(a, 128);
    if (!d) return false;
    d = krealloc(d, 32);
    if (!d) return false;

    /* Force at least two heap extensions, then free list-neighbor blocks.
       The allocator must not coalesce them unless their addresses are really adjacent. */
    void *big1 = kmalloc(90000);
    void *big2 = kmalloc(90000);
    if (!big1 || !big2 || big1 == big2) return false;
    memset(big1, 0x33, 90000);
    memset(big2, 0x44, 90000);

    kfree(b);
    kfree(c);
    kfree(d);
    kfree(big1);
    kfree(big2);

    void *probe = kmalloc(1024);
    if (!probe) return false;
    memset(probe, 0x55, 1024);
    kfree(probe);
    return true;
}
