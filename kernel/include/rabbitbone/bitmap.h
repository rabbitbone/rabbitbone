#ifndef RABBITBONE_BITMAP_H
#define RABBITBONE_BITMAP_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct bitmap {
    u64 *words;
    usize bit_count;
} bitmap_t;

/* If storage is null, bitmap_init creates an empty bitmap instead of retaining an invalid backing pointer. */
void bitmap_init(bitmap_t *bm, u64 *storage, usize bits);
void bitmap_set(bitmap_t *bm, usize bit);
void bitmap_clear(bitmap_t *bm, usize bit);
bool bitmap_test(const bitmap_t *bm, usize bit);
bool bitmap_find_first_set(const bitmap_t *bm, usize *out);
bool bitmap_find_first_clear(const bitmap_t *bm, usize *out);
usize bitmap_count_set(const bitmap_t *bm);

#if defined(__cplusplus)
}
#endif
#endif
