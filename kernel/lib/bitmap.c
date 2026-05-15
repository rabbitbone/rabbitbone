#include <rabbitbone/bitmap.h>
#include <rabbitbone/libc.h>

static bool word_count_checked(usize bits, usize *out) {
    if (!out) return false;
    if (bits > ((usize)-1) - 63u) return false;
    usize words = (bits + 63u) / 64u;
    if (words > ((usize)-1) / sizeof(u64)) return false;
    *out = words;
    return true;
}

void bitmap_init(bitmap_t *bm, u64 *storage, usize bits) {
    if (!bm) return;
    bm->words = 0;
    bm->bit_count = 0;
    if (!storage || !bits) {
        bm->words = storage;
        return;
    }
    usize words = 0;
    if (!word_count_checked(bits, &words)) return;
    bm->words = storage;
    bm->bit_count = bits;
    memset(storage, 0, words * sizeof(u64));
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
    usize words = 0;
    if (!word_count_checked(bm->bit_count, &words)) return false;
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
    usize words = 0;
    if (!word_count_checked(bm->bit_count, &words)) return false;
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
    usize words = 0;
    if (!word_count_checked(bm->bit_count, &words)) return 0;
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
