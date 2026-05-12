#ifndef AURORA_TYPES_H
#define AURORA_TYPES_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef __UINT64_TYPE__ u64;
typedef __INT8_TYPE__ i8;
typedef __INT16_TYPE__ i16;
typedef __INT32_TYPE__ i32;
typedef __INT64_TYPE__ i64;
typedef __SIZE_TYPE__ usize;
typedef __PTRDIFF_TYPE__ isize;
typedef __UINTPTR_TYPE__ uptr;

#if !defined(__cplusplus)
typedef _Bool bool;
#ifndef true
#define true 1
#endif
#ifndef false
#define false 0
#endif
#endif

#define AURORA_UNUSED(x) ((void)(x))
#define AURORA_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define AURORA_ALIGN_UP(v, a) (((v) + ((a) - 1)) & ~((a) - 1))
#define AURORA_ALIGN_DOWN(v, a) ((v) & ~((a) - 1))

static inline bool aurora_is_power_of_two_u64(u64 v) {
    return v != 0 && (v & (v - 1u)) == 0;
}

static inline bool aurora_align_up_u64_checked(u64 v, u64 alignment, u64 *out) {
    if (!out || !aurora_is_power_of_two_u64(alignment)) return false;
    u64 addend = alignment - 1u;
    u64 tmp = 0;
    if (__builtin_add_overflow(v, addend, &tmp)) return false;
    *out = tmp & ~addend;
    return true;
}

static inline bool aurora_align_up_usize_checked(usize v, usize alignment, usize *out) {
    if (!out || alignment == 0 || (alignment & (alignment - 1u)) != 0) return false;
    usize addend = alignment - 1u;
    usize tmp = 0;
    if (__builtin_add_overflow(v, addend, &tmp)) return false;
    *out = tmp & ~addend;
    return true;
}

#define AURORA_PACKED __attribute__((packed))
#define AURORA_NORETURN __attribute__((noreturn))
#define AURORA_WEAK __attribute__((weak))
#define AURORA_STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]

#if defined(__cplusplus)
}
#endif

#endif
