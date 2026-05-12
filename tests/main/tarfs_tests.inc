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

