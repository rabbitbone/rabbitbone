#pragma once

#include <rabbitbone/types.h>

static inline u64 rabbitbone_u64_add_saturating(u64 a, u64 b) {
    u64 out = 0;
    return __builtin_add_overflow(a, b, &out) ? ~0ull : out;
}

static inline u64 rabbitbone_u64_mul_div_saturating(u64 a, u64 b, u64 divisor) {
    if (divisor == 0) return ~0ull;
    __uint128_t product = (__uint128_t)a * (__uint128_t)b;
    u64 hi = (u64)(product >> 64);
    u64 lo = (u64)product;
    __uint128_t rem = 0;
    u64 q = 0;
    for (int bit = 127; bit >= 0; --bit) {
        u64 next = bit >= 64 ? ((hi >> (bit - 64)) & 1ull) : ((lo >> bit) & 1ull);
        rem = (rem << 1) | next;
        if (rem >= (__uint128_t)divisor) {
            rem -= (__uint128_t)divisor;
            if (bit >= 64) return ~0ull;
            q |= 1ull << bit;
        }
    }
    return q;
}
