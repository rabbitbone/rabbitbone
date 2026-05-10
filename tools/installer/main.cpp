#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Args {
    std::string out;
    std::string stage1;
    std::string stage2;
    std::string kernel;
    std::uint64_t size_mib = 64;
};

constexpr std::uint32_t kPartitionLba = 2048;
constexpr std::uint32_t kFsBlockSize = 1024;
constexpr std::uint32_t kFsBlocks = 8192;
constexpr std::uint32_t kFsInodes = 1024;
constexpr std::uint32_t kBlocksPerGroup = 8192;
constexpr std::uint32_t kInodesPerGroup = 1024;
constexpr std::uint32_t kBlockBitmapBlock = 3;
constexpr std::uint32_t kInodeBitmapBlock = 4;
constexpr std::uint32_t kInodeTableBlock = 5;
constexpr std::uint32_t kFirstDataBlock = 1;
constexpr std::uint32_t kRootDirBlock = 133;
constexpr std::uint32_t kEtcDirBlock = 134;
constexpr std::uint32_t kHelloBlock = 135;
constexpr std::uint32_t kIssueBlock = 136;
constexpr std::uint32_t kReadmeBlock = 137;
constexpr std::uint32_t kRootIno = 2;
constexpr std::uint32_t kEtcIno = 12;
constexpr std::uint32_t kHelloIno = 13;
constexpr std::uint32_t kIssueIno = 14;
constexpr std::uint32_t kReadmeIno = 15;
constexpr std::uint32_t kLastUsedBlock = kReadmeBlock;
constexpr std::uint32_t kLastUsedInode = kReadmeIno;

[[noreturn]] void usage() {
    std::cerr << "usage: aurora-install --out disk.img --stage1 stage1.bin --stage2 stage2.bin --kernel kernel.bin [--size-mib N]\n";
    std::exit(2);
}

std::vector<std::uint8_t> read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    auto size = in.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path);
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) in.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!in && !data.empty()) throw std::runtime_error("cannot read " + path);
    return data;
}


void write_at(std::fstream &io, std::uint64_t off, const std::vector<std::uint8_t> &data) {
    io.seekp(static_cast<std::streamoff>(off), std::ios::beg);
    if (!io) throw std::runtime_error("seek failed");
    io.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!io) throw std::runtime_error("write failed");
}

void put_u16le(std::uint8_t *p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xff);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
}

void put_u32le(std::uint8_t *p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xff);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xff);
}

void set_bit(std::vector<std::uint8_t> &bitmap, std::uint32_t index) {
    bitmap[index / 8u] |= static_cast<std::uint8_t>(1u << (index % 8u));
}

std::uint16_t rec_len_for(std::size_t name_len) {
    return static_cast<std::uint16_t>((8u + name_len + 3u) & ~3u);
}

void add_dirent(std::vector<std::uint8_t> &block, std::size_t off, std::uint32_t ino, std::uint16_t rec_len, std::uint8_t type, const std::string &name) {
    if (off + rec_len > block.size()) throw std::runtime_error("dirent exceeds block");
    put_u32le(block.data() + off, ino);
    put_u16le(block.data() + off + 4, rec_len);
    block[off + 6] = static_cast<std::uint8_t>(name.size());
    block[off + 7] = type;
    std::memcpy(block.data() + off + 8, name.data(), name.size());
}

void write_inode(std::vector<std::uint8_t> &fs, std::uint32_t ino, std::uint16_t mode, std::uint32_t size, std::uint32_t block, std::uint16_t links) {
    const std::uint64_t inode_off = static_cast<std::uint64_t>(kInodeTableBlock) * kFsBlockSize + static_cast<std::uint64_t>(ino - 1u) * 128u;
    if (inode_off + 128u > fs.size()) throw std::runtime_error("inode table overflow");
    auto *p = fs.data() + inode_off;
    put_u16le(p + 0, mode);
    put_u32le(p + 4, size);
    put_u32le(p + 8, 1);       // atime
    put_u32le(p + 12, 1);      // ctime
    put_u32le(p + 16, 1);      // mtime
    put_u16le(p + 26, links);
    put_u32le(p + 28, ((size + 511u) / 512u));
    put_u32le(p + 40, block);
}

std::vector<std::uint8_t> make_ext4_seed_fs(const std::vector<std::uint8_t> &kernel) {
    std::vector<std::uint8_t> fs(static_cast<std::size_t>(kFsBlocks) * kFsBlockSize, 0);
    const std::uint32_t used_blocks = kLastUsedBlock + 1u;
    const std::uint32_t free_blocks = kFsBlocks - used_blocks;
    const std::uint32_t free_inodes = kFsInodes - kLastUsedInode;

    // Superblock, offset 1024 for 1 KiB block size.
    auto *sb = fs.data() + 1024;
    put_u32le(sb + 0, kFsInodes);
    put_u32le(sb + 4, kFsBlocks);
    put_u32le(sb + 12, free_blocks);
    put_u32le(sb + 16, free_inodes);
    put_u32le(sb + 20, kFirstDataBlock);
    put_u32le(sb + 24, 0);                 // 1024-byte blocks
    put_u32le(sb + 32, kBlocksPerGroup);
    put_u32le(sb + 40, kInodesPerGroup);
    put_u16le(sb + 52, 1);                 // mnt count
    put_u16le(sb + 54, 20);                // max mnt count
    put_u16le(sb + 56, 0xef53);            // magic
    put_u16le(sb + 58, 1);                 // cleanly unmounted
    put_u16le(sb + 60, 1);                 // continue on errors
    put_u32le(sb + 76, 1);                 // dynamic revision
    put_u32le(sb + 84, 11);
    put_u16le(sb + 88, 128);               // inode size
    put_u32le(sb + 96, 0);                 // compat features
    put_u32le(sb + 100, 0x0002);           // incompat: dir filetype
    put_u32le(sb + 104, 0);                // ro compat
    const char label[] = "AURORA_BOOT";
    std::memcpy(sb + 120, label, sizeof(label) - 1u);
    put_u16le(sb + 254, 32);               // group descriptor size for readers that inspect it

    // Group descriptor table at block 2.
    auto *gd = fs.data() + 2u * kFsBlockSize;
    put_u32le(gd + 0, kBlockBitmapBlock);
    put_u32le(gd + 4, kInodeBitmapBlock);
    put_u32le(gd + 8, kInodeTableBlock);
    put_u16le(gd + 12, static_cast<std::uint16_t>(free_blocks));
    put_u16le(gd + 14, static_cast<std::uint16_t>(free_inodes));
    put_u16le(gd + 16, 2);                 // root and /etc

    // Block bitmap.
    std::vector<std::uint8_t> block_bitmap(kFsBlockSize, 0);
    for (std::uint32_t b = 0; b <= kLastUsedBlock; ++b) set_bit(block_bitmap, b);
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kBlockBitmapBlock) * kFsBlockSize, block_bitmap.data(), block_bitmap.size());

    // Inode bitmap.
    std::vector<std::uint8_t> inode_bitmap(kFsBlockSize, 0);
    for (std::uint32_t ino = 1; ino <= kLastUsedInode; ++ino) set_bit(inode_bitmap, ino - 1u);
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kInodeBitmapBlock) * kFsBlockSize, inode_bitmap.data(), inode_bitmap.size());

    const std::string hello = "Hello from the AuroraOS installer-created EXT4 partition.\n";
    const std::string issue = "AuroraOS Stage 1 EXT4 seed filesystem\n";
    const std::string readme =
        "This partition is generated by tools/installer/main.cpp.\n"
        "The kernel mounts it read-only as /disk0 when ATA PIO discovers the boot disk.\n";

    std::vector<std::uint8_t> root(kFsBlockSize, 0);
    std::size_t off = 0;
    add_dirent(root, off, kRootIno, rec_len_for(1), 2, "."); off += rec_len_for(1);
    add_dirent(root, off, kRootIno, rec_len_for(2), 2, ".."); off += rec_len_for(2);
    add_dirent(root, off, kEtcIno, rec_len_for(3), 2, "etc"); off += rec_len_for(3);
    add_dirent(root, off, kHelloIno, rec_len_for(9), 1, "hello.txt"); off += rec_len_for(9);
    add_dirent(root, off, kReadmeIno, static_cast<std::uint16_t>(kFsBlockSize - off), 1, "README");
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kRootDirBlock) * kFsBlockSize, root.data(), root.size());

    std::vector<std::uint8_t> etc(kFsBlockSize, 0);
    off = 0;
    add_dirent(etc, off, kEtcIno, rec_len_for(1), 2, "."); off += rec_len_for(1);
    add_dirent(etc, off, kRootIno, rec_len_for(2), 2, ".."); off += rec_len_for(2);
    add_dirent(etc, off, kIssueIno, static_cast<std::uint16_t>(kFsBlockSize - off), 1, "issue");
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kEtcDirBlock) * kFsBlockSize, etc.data(), etc.size());

    std::memcpy(fs.data() + static_cast<std::uint64_t>(kHelloBlock) * kFsBlockSize, hello.data(), hello.size());
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kIssueBlock) * kFsBlockSize, issue.data(), issue.size());
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kReadmeBlock) * kFsBlockSize, readme.data(), readme.size());

    write_inode(fs, kRootIno, 0040000 | 0755, kFsBlockSize, kRootDirBlock, 3);
    write_inode(fs, kEtcIno, 0040000 | 0755, kFsBlockSize, kEtcDirBlock, 2);
    write_inode(fs, kHelloIno, 0100000 | 0644, static_cast<std::uint32_t>(hello.size()), kHelloBlock, 1);
    write_inode(fs, kIssueIno, 0100000 | 0644, static_cast<std::uint32_t>(issue.size()), kIssueBlock, 1);
    write_inode(fs, kReadmeIno, 0100000 | 0644, static_cast<std::uint32_t>(readme.size()), kReadmeBlock, 1);

    (void)kernel;
    return fs;
}

Args parse(int argc, char **argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](const char *name) -> std::string {
            if (i + 1 >= argc) usage();
            if (k != name) usage();
            return argv[++i];
        };
        if (k == "--out") a.out = need("--out");
        else if (k == "--stage1") a.stage1 = need("--stage1");
        else if (k == "--stage2") a.stage2 = need("--stage2");
        else if (k == "--kernel") a.kernel = need("--kernel");
        else if (k == "--size-mib") a.size_mib = std::stoull(need("--size-mib"));
        else usage();
    }
    if (a.out.empty() || a.stage1.empty() || a.stage2.empty() || a.kernel.empty()) usage();
    if (a.size_mib < 16 || a.size_mib > 4096) throw std::runtime_error("size-mib must be 16..4096");
    return a;
}

} // namespace

int main(int argc, char **argv) {
    try {
        Args args = parse(argc, argv);
        auto stage1 = read_file(args.stage1);
        auto stage2 = read_file(args.stage2);
        auto kernel = read_file(args.kernel);
        if (stage1.size() != 512) throw std::runtime_error("stage1 must be 512 bytes");
        if (stage1[510] != 0x55 || stage1[511] != 0xaa) throw std::runtime_error("stage1 missing boot signature");
        if (stage2.size() > 64ull * 512ull) throw std::runtime_error("stage2 exceeds reserved sectors");
        if (kernel.size() > 1016ull * 512ull) throw std::runtime_error("kernel exceeds reserved sectors");

        const std::uint64_t image_size = args.size_mib * 1024ull * 1024ull;
        const std::uint64_t total_sectors = image_size / 512ull;
        if (total_sectors <= kPartitionLba + 4096) throw std::runtime_error("image too small");
        auto seed_fs = make_ext4_seed_fs(kernel);
        if (seed_fs.size() > (total_sectors - kPartitionLba) * 512ull) throw std::runtime_error("seed filesystem exceeds partition");

        std::array<std::uint8_t, 16> part{};
        part[0] = 0x80;
        part[1] = 0x20; part[2] = 0x21; part[3] = 0x00;
        part[4] = 0x83;
        part[5] = 0xfe; part[6] = 0xff; part[7] = 0xff;
        put_u32le(part.data() + 8, kPartitionLba);
        put_u32le(part.data() + 12, static_cast<std::uint32_t>(total_sectors - kPartitionLba));
        std::memcpy(stage1.data() + 446, part.data(), part.size());
        stage1[510] = 0x55;
        stage1[511] = 0xaa;

        {
            std::ofstream create(args.out, std::ios::binary | std::ios::trunc);
            if (!create) throw std::runtime_error("cannot create " + args.out);
            create.seekp(static_cast<std::streamoff>(image_size - 1), std::ios::beg);
            char zero = 0;
            create.write(&zero, 1);
            if (!create) throw std::runtime_error("cannot size image");
        }
        std::fstream io(args.out, std::ios::binary | std::ios::in | std::ios::out);
        if (!io) throw std::runtime_error("cannot reopen " + args.out);
        write_at(io, 0, stage1);
        write_at(io, 512, stage2);
        write_at(io, 65ull * 512ull, kernel);
        write_at(io, static_cast<std::uint64_t>(kPartitionLba) * 512ull, seed_fs);
        io.flush();
        if (!io) throw std::runtime_error("flush failed");
        std::cout << "created " << args.out << " size=" << args.size_mib << "MiB partition_lba=" << kPartitionLba
                  << " ext4_seed_blocks=" << kFsBlocks << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "aurora-install: " << e.what() << "\n";
        return 1;
    }
}
