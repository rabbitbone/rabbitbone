#pragma once

#include <aurora/kmem.h>
#include <aurora/memory.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/panic.h>
#include <aurora/spinlock.h>

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
static spinlock_t kmem_lock;

