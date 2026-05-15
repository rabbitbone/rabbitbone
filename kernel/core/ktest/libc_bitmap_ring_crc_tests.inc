static void test_libc_bitmap_ring_crc(void) {
    suite_begin("libc/bitmap/ringbuf/crc32");
    char a[16];
    char b[16];
    memset(a, 0x41, sizeof(a));
    check(a[0] == 'A' && a[15] == 'A', "memset fills full range");
    memcpy(b, a, sizeof(a));
    check(memcmp(a, b, sizeof(a)) == 0, "memcpy/memcmp roundtrip");
    memcpy(a, "0123456789", 11u);
    memmove(a + 2, a, 8u);
    check(strcmp(a, "0101234567") == 0, "memmove overlapping right shift");
    check(strlen("rabbitbone") == 10u && strnlen("rabbitbone", 3u) == 3u && strnlen("rabbitbone", 32u) == 10u, "strlen/strnlen");
    check(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0, "strcmp ordering");
    check(strncmp("abcdef", "abcXYZ", 3u) == 0, "strncmp prefix");
    check(strchr("abc", 'b') && !strchr("abc", 'z'), "strchr hit/miss");
    const char *end = 0;
    check(strtou64("0x2a!", &end, 0) == 42u && end && *end == '!', "strtou64 base auto");
    check(strtou64("18446744073709551616", &end, 10) == 0xffffffffffffffffull && end && *end == 0, "strtou64 saturates overflow");
    check(strtou64("123", &end, 1) == 0u && end && *end == '1', "strtou64 rejects invalid base");

    u64 storage[2];
    bitmap_t bm;
    bitmap_init(&bm, storage, 128u);
    usize bit = 0;
    check(bitmap_find_first_clear(&bm, &bit) && bit == 0u, "bitmap first clear");
    bitmap_set(&bm, 0u);
    bitmap_set(&bm, 65u);
    check(bitmap_test(&bm, 0u) && bitmap_test(&bm, 65u), "bitmap set/test across words");
    check(bitmap_count_set(&bm) == 2u, "bitmap count set");
    bitmap_clear(&bm, 0u);
    check(!bitmap_test(&bm, 0u) && bitmap_find_first_set(&bm, &bit) && bit == 65u, "bitmap clear/first set");
    u64 pad_storage[1];
    bitmap_init(&bm, pad_storage, 3u);
    pad_storage[0] |= 0xfffffffffffffff8ull;
    check(bitmap_count_set(&bm) == 0u, "bitmap count ignores padding bits");
    bitmap_init(&bm, 0, 16u);
    check(bitmap_count_set(&bm) == 0u && !bitmap_test(&bm, 0u), "bitmap null storage becomes empty bitmap");

    check(ringbuf_selftest(), "ringbuf wraparound/read/peek");
    check(crc32_selftest(), "crc32 reference vectors");
    suite_end();
}

