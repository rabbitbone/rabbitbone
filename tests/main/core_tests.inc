static int g_test_failures = 0;

static void require(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_test_failures;
    }
}

static void test_bitmap() {
    u64 storage[2];
    bitmap_t bm;
    bitmap_init(&bm, storage, 128);
    usize bit = 999;
    require(bitmap_find_first_clear(&bm, &bit) && bit == 0, "first clear bit");
    bitmap_set(&bm, 0);
    bitmap_set(&bm, 65);
    require(bitmap_test(&bm, 0), "bit 0 set");
    require(bitmap_test(&bm, 65), "bit 65 set");
    require(bitmap_count_set(&bm) == 2, "count set");
    require(bitmap_find_first_set(&bm, &bit) && bit == 0, "first set bit");
    bitmap_clear(&bm, 0);
    require(!bitmap_test(&bm, 0), "bit 0 clear");
    u64 pad_storage[1];
    bitmap_init(&bm, pad_storage, 3);
    pad_storage[0] |= 0xfffffffffffffff8ull;
    require(bitmap_count_set(&bm) == 0, "bitmap ignores padding bits");
    bitmap_init(&bm, nullptr, 8);
    require(bitmap_count_set(&bm) == 0 && !bitmap_test(&bm, 0), "bitmap null storage safe");
    const char *end = nullptr;
    require(strtou64("18446744073709551616", &end, 10) == 0xffffffffffffffffull && end && *end == 0, "strtou64 overflow saturates");
    require(strtou64("77", &end, 1) == 0 && end && *end == '7', "strtou64 invalid base rejected");
}

static void test_ringbuf_crc32() {
    require(ringbuf_selftest(), "ringbuf selftest");
    require(crc32_selftest(), "crc32 selftest");
}

static void test_printf() {
    char buf[128];
    ksnprintf(buf, sizeof(buf), "%s %d %u %x %p", "ok", -7, 42u, 0x2au, (void *)0x1234);
    require(std::string(buf).find("ok -7 42 2a 0x") == 0, "printf basic");
    ksnprintf(buf, sizeof(buf), "%04x", 0xau);
    require(strcmp(buf, "000a") == 0, "printf zero pad");
    ksnprintf(buf, sizeof(buf), "%lld", static_cast<long long>(INT64_MIN));
    require(strcmp(buf, "-9223372036854775808") == 0, "printf handles int64 min without UB");
}

static void put32(std::vector<u8> &d, std::size_t off, u32 v) {
    d[off] = v & 0xff;
    d[off + 1] = (v >> 8) & 0xff;
    d[off + 2] = (v >> 16) & 0xff;
    d[off + 3] = (v >> 24) & 0xff;
}

static void put16(std::vector<u8> &d, std::size_t off, u16 v) {
    d[off] = v & 0xff;
    d[off + 1] = (v >> 8) & 0xff;
}

static void test_path_and_heap() {
    char out[256];
    require(path_normalize("/a//b/../c/.", out, sizeof(out)), "path normalize returns true");
    require(strcmp(out, "/a/c") == 0, "path normalize result");
    require(path_join("/a/b", "../d", out, sizeof(out)), "path join returns true");
    require(strcmp(out, "/a/d") == 0, "path join result");
    kmem_init();
    void *a = kmalloc(33);
    void *b = kmalloc(4097);
    require(a && b, "kmem alloc");
    memset(a, 0x5a, 33);
    void *c = krealloc(a, 512);
    require(c, "kmem realloc");
    kfree(b);
    kfree(c);
    require(kmem_selftest(), "kmem selftest");
    require(kmalloc(static_cast<usize>(-1)) == nullptr, "kmalloc rejects overflowing huge allocation");
}

static void test_mbr() {
    std::vector<u8> sec(512);
    sec[510] = 0x55;
    sec[511] = 0xaa;
    sec[446] = 0x80;
    sec[450] = 0x83;
    put32(sec, 454, 2048);
    put32(sec, 458, 8192);
    mbr_table_t mbr;
    require(mbr_parse_sector(sec.data(), &mbr), "mbr parse");
    require(mbr.part[0].bootable, "mbr bootable");
    require(mbr.part[0].type == 0x83, "mbr type");
    require(mbr.part[0].lba_first == 2048, "mbr lba");
    require(mbr_find_linux(&mbr) == &mbr.part[0], "mbr linux find");
}

struct MemDisk {
    std::vector<u8> data;
};

