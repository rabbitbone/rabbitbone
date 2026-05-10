#ifndef AURORA_CRC32_H
#define AURORA_CRC32_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

u32 crc32_update(u32 crc, const void *data, usize size);
u32 crc32(const void *data, usize size);
bool crc32_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
