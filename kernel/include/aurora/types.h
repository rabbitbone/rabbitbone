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
#define AURORA_PACKED __attribute__((packed))
#define AURORA_NORETURN __attribute__((noreturn))
#define AURORA_WEAK __attribute__((weak))
#define AURORA_STATIC_ASSERT(name, expr) typedef char static_assert_##name[(expr) ? 1 : -1]

#if defined(__cplusplus)
}
#endif

#endif
