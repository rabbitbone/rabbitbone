#include <aurora/bitmap.h>
#include <aurora/libc.h>

static usize word_count(usize bits) { return (bits + 63u) / 64u; }

void bitmap_init(bitmap_t *bm, u64 *storage, usize bits) {
    if (!bm) return;
    bm->words = storage;
    bm->bit_count = storage ? bits : 0u;
    if (storage && bits) memset(storage, 0, word_count(bits) * sizeof(u64));
}

void bitmap_set(bitmap_t *bm, usize bit) {
    if (!bm || !bm->words || bit >= bm->bit_count) return;
    bm->words[bit / 64u] |= (1ull << (bit % 64u));
}

void bitmap_clear(bitmap_t *bm, usize bit) {
    if (!bm || !bm->words || bit >= bm->bit_count) return;
    bm->words[bit / 64u] &= ~(1ull << (bit % 64u));
}

bool bitmap_test(const bitmap_t *bm, usize bit) {
    if (!bm || !bm->words || bit >= bm->bit_count) return false;
    return (bm->words[bit / 64u] & (1ull << (bit % 64u))) != 0;
}

bool bitmap_find_first_set(const bitmap_t *bm, usize *out) {
    if (!bm || !bm->words) return false;
    usize words = word_count(bm->bit_count);
    for (usize i = 0; i < words; ++i) {
        u64 w = bm->words[i];
        if (!w) continue;
        for (usize b = 0; b < 64; ++b) {
            usize bit = i * 64u + b;
            if (bit >= bm->bit_count) return false;
            if ((w & (1ull << b)) != 0) {
                if (out) *out = bit;
                return true;
            }
        }
    }
    return false;
}

bool bitmap_find_first_clear(const bitmap_t *bm, usize *out) {
    if (!bm || !bm->words) return false;
    usize words = word_count(bm->bit_count);
    for (usize i = 0; i < words; ++i) {
        u64 w = ~bm->words[i];
        if (!w) continue;
        for (usize b = 0; b < 64; ++b) {
            usize bit = i * 64u + b;
            if (bit >= bm->bit_count) return false;
            if ((w & (1ull << b)) != 0) {
                if (out) *out = bit;
                return true;
            }
        }
    }
    return false;
}

usize bitmap_count_set(const bitmap_t *bm) {
    if (!bm || !bm->words) return 0;
    usize total = 0;
    usize words = word_count(bm->bit_count);
    for (usize i = 0; i < words; ++i) {
        u64 w = bm->words[i];
        if (i + 1u == words && (bm->bit_count % 64u) != 0) {
            w &= (1ull << (bm->bit_count % 64u)) - 1ull;
        }
        while (w) {
            w &= w - 1;
            ++total;
        }
    }
    return total;
}
