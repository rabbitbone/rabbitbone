#include <aurora/bitmap.h>
#include <aurora/block.h>
#include <aurora/console.h>
#include <aurora/crc32.h>
#include <aurora/ext4.h>
#include <aurora/libc.h>
#include <aurora/mbr.h>
#include <aurora/kmem.h>
#include <aurora/path.h>
#include <aurora/ringbuf.h>
#include <aurora/tarfs.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

static void require(bool cond, const char *msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
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

static block_status_t mem_read(block_device_t *dev, u64 lba, u32 count, void *buffer) {
    auto *disk = static_cast<MemDisk *>(dev->ctx);
    if (lba > UINT64_MAX / 512ull || static_cast<u64>(count) > UINT64_MAX / 512ull) return BLOCK_ERR_RANGE;
    const u64 off = lba * 512ull;
    const u64 len = static_cast<u64>(count) * 512ull;
    if (off > disk->data.size() || len > disk->data.size() - off) return BLOCK_ERR_RANGE;
    memcpy(buffer, disk->data.data() + off, static_cast<std::size_t>(len));
    return BLOCK_OK;
}

static block_status_t mem_write(block_device_t *dev, u64 lba, u32 count, const void *buffer) {
    auto *disk = static_cast<MemDisk *>(dev->ctx);
    if (lba > UINT64_MAX / 512ull || static_cast<u64>(count) > UINT64_MAX / 512ull) return BLOCK_ERR_RANGE;
    const u64 off = lba * 512ull;
    const u64 len = static_cast<u64>(count) * 512ull;
    if (off > disk->data.size() || len > disk->data.size() - off) return BLOCK_ERR_RANGE;
    memcpy(disk->data.data() + off, buffer, static_cast<std::size_t>(len));
    return BLOCK_OK;
}

static block_status_t fail_read(block_device_t *, u64, u32, void *) {
    return BLOCK_ERR_IO;
}

static void test_block_ranges() {
    block_device_t dev{};
    dev.sector_count = 16;
    dev.sector_size = 512;
    dev.read = fail_read;
    u8 sector[512]{};
    require(block_read(&dev, 15, 2, sector) == BLOCK_ERR_RANGE, "block range end overflow rejected");
    require(block_read(&dev, UINT64_MAX, 2, sector) == BLOCK_ERR_RANGE, "block lba overflow rejected");
    mbr_partition_t part{};
    part.type = 0x83;
    part.lba_first = 15;
    part.sector_count = 2;
    require(!mbr_partition_valid(&dev, &part), "mbr range validator rejects oversized partition");
}

static void make_dirent(std::vector<u8> &d, std::size_t off, u32 ino, u16 rec_len, u8 type, const char *name) {
    const std::size_t len = strlen(name);
    put32(d, off, ino);
    put16(d, off + 4, rec_len);
    d[off + 6] = static_cast<u8>(len);
    d[off + 7] = type;
    memcpy(d.data() + off + 8, name, len);
}

static bool count_entry(const ext4_dirent_t *e, void *ctx) {
    auto *n = static_cast<int *>(ctx);
    if (strcmp(e->name, "hello.txt") == 0) ++*n;
    return true;
}

static void host_tar_octal(u8 *dst, std::size_t width, std::size_t value) {
    memset(dst, '0', width);
    dst[width - 1] = 0;
    for (std::size_t pos = width - 2;; --pos) {
        dst[pos] = static_cast<u8>('0' + (value & 7u));
        value >>= 3u;
        if (pos == 0 || value == 0) break;
    }
}

static void host_tar_fix_checksum(std::vector<u8> &img) {
    memset(img.data() + 148, ' ', 8);
    unsigned sum = 0;
    for (std::size_t i = 0; i < 512 && i < img.size(); ++i) {
        sum += img[i];
    }
    host_tar_octal(img.data() + 148, 8, sum);
}

static void host_tar_header(std::vector<u8> &img, const char *name, std::size_t size) {
    memset(img.data(), 0, img.size());
    memcpy(img.data(), name, strlen(name));
    host_tar_octal(img.data() + 100, 8, 0644);
    host_tar_octal(img.data() + 108, 8, 0);
    host_tar_octal(img.data() + 116, 8, 0);
    host_tar_octal(img.data() + 124, 12, size);
    host_tar_octal(img.data() + 136, 12, 1);
    memset(img.data() + 148, ' ', 8);
    img[156] = '0';
    memcpy(img.data() + 257, "ustar", 5);
    memcpy(img.data() + 263, "00", 2);
    host_tar_fix_checksum(img);
}

static void test_tarfs_bounds() {
    std::vector<u8> img(2048);
    host_tar_header(img, "file.txt", 4);
    memcpy(img.data() + 512, "data", 4);
    tarfs_t *ok = tarfs_open(img.data(), img.size());
    require(ok != nullptr, "tarfs valid image opens");
    tarfs_destroy(ok);
    std::vector<u8> zero = img;
    memset(zero.data() + 148, '0', 8);
    require(tarfs_open(zero.data(), zero.size()) == nullptr, "tarfs rejects zero checksum");
    std::vector<u8> trunc(1024);
    host_tar_header(trunc, "big.bin", 900);
    require(tarfs_open(trunc.data(), trunc.size()) == nullptr, "tarfs rejects truncated payload");
    std::vector<u8> bad_octal = img;
    bad_octal[124] = '9';
    host_tar_fix_checksum(bad_octal);
    require(tarfs_open(bad_octal.data(), bad_octal.size()) == nullptr, "tarfs rejects non-octal size field");
    std::vector<u8> bad_type = img;
    bad_type[156] = '2';
    host_tar_fix_checksum(bad_type);
    require(tarfs_open(bad_type.data(), bad_type.size()) == nullptr, "tarfs rejects unsupported symlink typeflag");
    std::vector<u8> parent_ref(2048);
    host_tar_header(parent_ref, "../../escape", 0);
    require(tarfs_open(parent_ref.data(), parent_ref.size()) == nullptr, "tarfs rejects parent-directory references before normalize");
}

static void test_ext4_minimal() {
    MemDisk disk{std::vector<u8>(128 * 1024)};
    block_device_t dev{};
    strncpy(dev.name, "mem0", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;

    // Superblock at byte 1024, block size 1024.
    const std::size_t sb = 1024;
    put32(disk.data, sb + 0, 16);      // inodes
    put32(disk.data, sb + 4, 64);      // blocks
    put32(disk.data, sb + 20, 1);      // first_data_block
    put32(disk.data, sb + 24, 0);      // log_block_size
    put32(disk.data, sb + 32, 64);     // blocks_per_group
    put32(disk.data, sb + 40, 16);     // inodes_per_group
    put16(disk.data, sb + 56, 0xef53); // magic
    put32(disk.data, sb + 84, 11);     // first ino
    put16(disk.data, sb + 88, 128);    // inode size
    put16(disk.data, sb + 254, 32);    // desc size

    // Group descriptor table at block 2, bitmaps at 3/4, inode table at 5.
    const std::size_t gd = 2048;
    put32(disk.data, gd + 0, 3);
    put32(disk.data, gd + 4, 4);
    put32(disk.data, gd + 8, 5);
    put16(disk.data, gd + 12, 63 - 10);
    put16(disk.data, gd + 14, 16 - 12);
    for (u32 b = 0; b <= 10; ++b) disk.data[3 * 1024 + b / 8] |= static_cast<u8>(1u << (b % 8));
    for (u32 ino_b = 0; ino_b < 12; ++ino_b) disk.data[4 * 1024 + ino_b / 8] |= static_cast<u8>(1u << (ino_b % 8));

    // Root inode #2 at inode table + 1 * 128.
    const std::size_t root = 5 * 1024 + 128;
    put16(disk.data, root + 0, 0x4000 | 0755);
    put32(disk.data, root + 4, 1024);
    put32(disk.data, root + 40, 10);

    // Directory block 10.
    const std::size_t dir = 10 * 1024;
    make_dirent(disk.data, dir, 2, 12, 2, ".");
    make_dirent(disk.data, dir + 12, 2, 12, 2, "..");
    make_dirent(disk.data, dir + 24, 12, 1000, 1, "hello.txt");

    ext4_mount_t mnt;
    require(ext4_mount(&dev, 0, &mnt) == EXT4_OK, "ext4 mount");
    ext4_inode_disk_t inode;
    require(ext4_read_inode(&mnt, EXT4_ROOT_INO, &inode) == EXT4_OK, "ext4 read root");
    require(ext4_inode_is_dir(&inode), "root is dir");
    int found = 0;
    require(ext4_list_dir(&mnt, &inode, count_entry, &found) == EXT4_OK, "ext4 list dir");
    require(found == 1, "ext4 found hello.txt");
    u32 ino = 0;
    require(ext4_find_in_dir(&mnt, &inode, "hello.txt", &ino, nullptr) == EXT4_OK && ino == 12, "ext4 find in dir");
    const char payload[] = "persistent ext4 write";
    require(ext4_create_file(&mnt, "/new.txt", payload, sizeof(payload) - 1) == EXT4_OK, "ext4 create file");
    ext4_inode_disk_t created{};
    u32 created_ino = 0;
    require(ext4_lookup_path(&mnt, "/new.txt", &created, &created_ino) == EXT4_OK && ext4_inode_is_regular(&created), "ext4 lookup created file");
    char readback[64]{};
    usize rb = 0;
    require(ext4_read_file(&mnt, &created, 0, readback, sizeof(readback), &rb) == EXT4_OK && rb == sizeof(payload) - 1 && strcmp(readback, payload) == 0, "ext4 read created file");
    const char patch[] = "EXT4";
    require(ext4_write_file(&mnt, created_ino, &created, 11, patch, sizeof(patch) - 1, nullptr) == EXT4_OK, "ext4 overwrite created file");
    memset(readback, 0, sizeof(readback)); rb = 0;
    require(ext4_read_file(&mnt, &created, 0, readback, sizeof(readback), &rb) == EXT4_OK && strstr(readback, "EXT4") != nullptr, "ext4 read overwritten data");
    require(ext4_mkdir(&mnt, "/dir") == EXT4_OK, "ext4 mkdir");
    require(ext4_create_file(&mnt, "/dir/child", "x", 1) == EXT4_OK, "ext4 create child in directory");
    require(ext4_unlink(&mnt, "/dir") == EXT4_ERR_NOT_EMPTY, "ext4 refuses unlink non-empty dir");
    require(ext4_unlink(&mnt, "/dir/child") == EXT4_OK, "ext4 unlink child");
    require(ext4_unlink(&mnt, "/dir") == EXT4_OK, "ext4 unlink empty dir");
    require(ext4_unlink(&mnt, "/new.txt") == EXT4_OK, "ext4 unlink file");

    // 32-byte group descriptors must not read high fields from the next descriptor.
    put32(disk.data, gd + 40, 0x7fffffffu);
    require(ext4_read_inode(&mnt, EXT4_ROOT_INO, &inode) == EXT4_OK, "ext4 32-byte group desc ignores neighbor high fields");
}

static void make_ext4_base(std::vector<u8> &data) {
    data.assign(128 * 1024, 0);
    const std::size_t sb = 1024;
    put32(data, sb + 0, 16);
    put32(data, sb + 4, 64);
    put32(data, sb + 20, 1);
    put32(data, sb + 24, 0);
    put32(data, sb + 32, 64);
    put32(data, sb + 40, 16);
    put16(data, sb + 56, 0xef53);
    put32(data, sb + 84, 11);
    put16(data, sb + 88, 128);
    put16(data, sb + 254, 32);
    put32(data, 2048 + 8, 5);
}

static ext4_status_t mount_mem_ext4(std::vector<u8> &data) {
    MemDisk disk{data};
    block_device_t dev{};
    strncpy(dev.name, "memx", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;
    ext4_mount_t mnt;
    return ext4_mount(&dev, 0, &mnt);
}

static void test_ext4_corrupt_inputs() {
    std::vector<u8> data;
    make_ext4_base(data);
    put32(data, 1024 + 24, 64);
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects huge block-size shift");
    make_ext4_base(data);
    put32(data, 1024 + 20, 65);
    require(mount_mem_ext4(data) == EXT4_ERR_CORRUPT, "ext4 rejects first_data beyond blocks");
    make_ext4_base(data);
    put16(data, 1024 + 88, 64);
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects tiny inode size");
    make_ext4_base(data);
    put16(data, 1024 + 254, 34);
    put32(data, 1024 + 96, 0x80); // enable 64bit desc-size field
    require(mount_mem_ext4(data) == EXT4_ERR_UNSUPPORTED, "ext4 rejects unaligned group desc size");

    make_ext4_base(data);
    MemDisk disk{data};
    block_device_t dev{};
    strncpy(dev.name, "memx", sizeof(dev.name) - 1);
    dev.sector_count = disk.data.size() / 512;
    dev.sector_size = 512;
    dev.ctx = &disk;
    dev.read = mem_read;
    dev.write = mem_write;
    ext4_mount_t mnt;
    require(ext4_mount(&dev, 0, &mnt) == EXT4_OK, "ext4 corrupt extent base mount");
    ext4_inode_disk_t inode{};
    inode.i_mode = 0x8000;
    inode.i_size_lo = 4096;
    inode.i_flags = 0x00080000u;
    u8 extent_root[EXT4_N_BLOCKS * sizeof(u32)]{};
    extent_root[0] = 0x0a; extent_root[1] = 0xf3;
    extent_root[2] = 1; extent_root[3] = 0;
    extent_root[4] = 1; extent_root[5] = 0;
    extent_root[6] = 0xff; extent_root[7] = 0xff;
    memcpy(reinterpret_cast<u8 *>(&inode) + offsetof(ext4_inode_disk_t, i_block), extent_root, sizeof(extent_root));
    u8 out[16]{};
    usize got = 0;
    require(ext4_read_file(&mnt, &inode, 0, out, sizeof(out), &got) == EXT4_ERR_CORRUPT, "ext4 rejects excessive extent depth");
}

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
    std::puts("all tests passed");
    return 0;
}
