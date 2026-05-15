#include <rabbitbone/crc32.h>
#include <rabbitbone/libc.h>

u32 crc32_update(u32 crc, const void *data, usize size) {
    if (!data && size) return crc;
    const u8 *p = (const u8 *)data;
    crc = ~crc;
    for (usize i = 0; i < size; ++i) {
        crc ^= p[i];
        for (u32 bit = 0; bit < 8u; ++bit) {
            u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

u32 crc32(const void *data, usize size) {
    return crc32_update(0, data, size);
}

bool crc32_selftest(void) {
    static const char test[] = "123456789";
    if (crc32(test, sizeof(test) - 1u) != 0xcbf43926u) return false;
    static const char abc[] = "abc";
    if (crc32(abc, 3u) != 0x352441c2u) return false;
    return crc32("", 0) == 0u && crc32_update(0x12345678u, 0, 4u) == 0x12345678u;
}
