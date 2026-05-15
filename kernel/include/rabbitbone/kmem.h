#ifndef RABBITBONE_KMEM_H
#define RABBITBONE_KMEM_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct kmem_stats {
    u64 heap_bytes;
    u64 used_bytes;
    u64 free_bytes;
    u64 allocation_count;
    u64 page_bytes_requested;
    u64 failed_allocations;
} kmem_stats_t;

void kmem_init(void);
void *kmalloc(usize size);
void *kcalloc(usize count, usize size);
void *krealloc(void *ptr, usize new_size);
void kfree(void *ptr);
char *kstrdup(const char *s);
void kmem_get_stats(kmem_stats_t *out);
void kmem_dump(void);
bool kmem_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
