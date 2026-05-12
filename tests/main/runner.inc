int main() {
    test_bitmap();
    test_printf();
    test_ringbuf_crc32();
    test_path_and_heap();
    test_mbr();
    test_block_ranges();
    test_tarfs_bounds();
    test_ext4_minimal();
    test_ext4_corrupt_inputs();
    if (g_test_failures) {
        std::fprintf(stderr, "%d test(s) failed\n", g_test_failures);
        return 1;
    }
    std::puts("all tests passed");
    return 0;
}
