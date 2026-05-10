#ifndef AURORA_MEMORY_H
#define AURORA_MEMORY_H
#include <aurora/types.h>
#include <aurora/bootinfo.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define PAGE_SIZE 4096ull
#define MEMORY_KERNEL_DIRECT_LIMIT (1024ull * 1024ull * 1024ull)

typedef struct memory_stats {
    u64 total_usable;
    u64 free_bytes;
    u64 used_bytes;
    u64 frame_count;
    u64 free_frames;
} memory_stats_t;

void memory_init(const aurora_bootinfo_t *bootinfo);
void *memory_alloc_page(void);
void *memory_alloc_page_below(u64 max_exclusive);
void *memory_alloc_contiguous_pages_below(usize pages, u64 max_exclusive);
void memory_free_page(void *page);
void memory_free_contiguous_pages(void *base, usize pages);
void memory_get_stats(memory_stats_t *out);
void memory_dump_map(void);
bool memory_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
