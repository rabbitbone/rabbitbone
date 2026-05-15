void write_inode_blocks(std::vector<std::uint8_t> &fs, std::uint32_t ino, std::uint16_t mode,
                        std::uint32_t size, const std::vector<std::uint32_t> &blocks,
                        std::uint16_t links, std::uint32_t indirect_block = 0) {
    const std::uint64_t inode_off = static_cast<std::uint64_t>(kInodeTableBlock) * kFsBlockSize + static_cast<std::uint64_t>(ino - 1u) * 128u;
    if (inode_off + 128u > fs.size()) throw std::runtime_error("inode table overflow");
    auto *p = fs.data() + inode_off;
    put_u16le(p + 0, mode);
    put_u32le(p + 4, size);
    put_u32le(p + 8, 1);
    put_u32le(p + 12, 1);
    put_u32le(p + 16, 1);
    put_u16le(p + 26, links);
    const std::uint32_t allocated = static_cast<std::uint32_t>(blocks.size()) + (indirect_block ? 1u : 0u);
    put_u32le(p + 28, allocated * (kFsBlockSize / 512u));
    for (std::size_t i = 0; i < blocks.size() && i < 12u; ++i) put_u32le(p + 40 + i * 4u, blocks[i]);
    if (indirect_block) put_u32le(p + 40 + 12u * 4u, indirect_block);
}

void write_inode(std::vector<std::uint8_t> &fs, std::uint32_t ino, std::uint16_t mode, std::uint32_t size, std::uint32_t block, std::uint16_t links) {
    std::vector<std::uint32_t> blocks;
    if (block) blocks.push_back(block);
    write_inode_blocks(fs, ino, mode, size, blocks, links, 0);
}

struct SeedNode {
    std::uint32_t ino = 0;
    std::string name;
    std::string path;
    std::vector<std::uint8_t> data;
    std::uint16_t mode = 0100644;
    std::uint8_t file_type = 1;
};

static std::vector<std::uint8_t> bytes_from_string(const std::string &s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

static void write_file_payload(std::vector<std::uint8_t> &fs, const SeedNode &node,
                               std::uint32_t &next_block, std::vector<std::uint32_t> &blocks,
                               std::uint32_t &indirect_block) {
    const std::size_t block_count = node.data.empty() ? 0u : static_cast<std::size_t>(ceil_div_u64(node.data.size(), kFsBlockSize));
    if (block_count > 12u + kFsBlockSize / 4u) throw std::runtime_error("seed file too large for legacy direct+indirect inode: " + node.path);
    blocks.reserve(block_count);
    for (std::size_t i = 0; i < block_count; ++i) {
        if (next_block >= kFsBlocks) throw std::runtime_error("seed filesystem is full while writing " + node.path);
        std::uint32_t b = next_block++;
        blocks.push_back(b);
        const std::size_t src_off = i * kFsBlockSize;
        const std::size_t take = std::min<std::size_t>(kFsBlockSize, node.data.size() - src_off);
        std::memcpy(fs.data() + static_cast<std::uint64_t>(b) * kFsBlockSize, node.data.data() + src_off, take);
    }
    if (blocks.size() > 12u) {
        if (next_block >= kFsBlocks) throw std::runtime_error("seed filesystem is full while allocating indirect block for " + node.path);
        indirect_block = next_block++;
        auto *ind = fs.data() + static_cast<std::uint64_t>(indirect_block) * kFsBlockSize;
        for (std::size_t i = 12u; i < blocks.size(); ++i) put_u32le(ind + (i - 12u) * 4u, blocks[i]);
    }
}

static std::vector<std::uint8_t> build_dir_block(const std::vector<SeedNode> &children, std::uint32_t self_ino, std::uint32_t parent_ino) {
    std::vector<std::uint8_t> block(kFsBlockSize, 0);
    std::size_t off = 0;
    add_dirent(block, off, self_ino, rec_len_for(1), 2, "."); off += rec_len_for(1);
    add_dirent(block, off, parent_ino, rec_len_for(2), 2, ".."); off += rec_len_for(2);
    for (std::size_t i = 0; i < children.size(); ++i) {
        const bool last = i + 1u == children.size();
        const std::uint16_t need = rec_len_for(static_cast<std::uint8_t>(children[i].name.size()));
        if (off + need > kFsBlockSize) throw std::runtime_error("seed directory too large: " + children[i].path);
        const std::uint16_t reclen = last ? static_cast<std::uint16_t>(kFsBlockSize - off) : need;
        add_dirent(block, off, children[i].ino, reclen, children[i].file_type, children[i].name);
        off += reclen;
    }
    return block;
}

static void add_seed_file(std::vector<SeedNode> &root_files, std::vector<SeedNode> &etc_files,
                          std::vector<SeedNode> &bin_files, std::vector<SeedNode> &sbin_files,
                          const SeedFile &file, std::uint32_t &next_ino, std::set<std::string> &seen_paths) {
    validate_seed_path(file.path, "seed path");
    if (!seen_paths.insert(file.path).second) throw std::runtime_error("duplicate seed path: " + file.path);
    if (file.data.size() > kMaxSeedFileBytes) throw std::runtime_error("seed file too large: " + file.path);
    if (next_ino > kFsInodes) throw std::runtime_error("seed filesystem inode table exhausted");
    const std::size_t slash = file.path.find_last_of('/');
    const std::string dir = slash == 0 ? "/" : file.path.substr(0, slash);
    const std::string name = file.path.substr(slash + 1u);
    SeedNode node;
    node.ino = next_ino++;
    node.name = name;
    node.path = file.path;
    node.data = file.data;
    node.mode = file.mode;
    node.file_type = 1;
    if (dir == "/") root_files.push_back(std::move(node));
    else if (dir == "/etc") etc_files.push_back(std::move(node));
    else if (dir == "/bin") bin_files.push_back(std::move(node));
    else if (dir == "/sbin") sbin_files.push_back(std::move(node));
    else throw std::runtime_error("unsupported seed directory for " + file.path + " (only /, /etc, /bin, /sbin)");
}

std::vector<std::uint8_t> make_ext4_seed_fs(const std::vector<SeedFile> &userland_files) {
    std::vector<std::uint8_t> fs(static_cast<std::size_t>(kFsBlocks) * kFsBlockSize, 0);

    std::vector<SeedNode> root_files;
    std::vector<SeedNode> etc_files;
    std::vector<SeedNode> bin_files;
    std::vector<SeedNode> sbin_files;
    std::uint32_t next_ino = kFirstDynamicInode;
    std::set<std::string> seen_paths = { "/", "/etc", "/bin", "/sbin" };

    const std::string hello = "Hello from the Rabbitbone installer-created EXT4 partition.\n";
    const std::string issue = "Rabbitbone disk-backed userland EXT4 filesystem\n";
    const std::string readme = "Rabbitbone live root.\n";
    const std::string profile = "PATH=/disk0/bin:/disk0/sbin:/bin:/sbin\nSHELL=/disk0/bin/sh\n";
    const std::string passwd = "root:x:0:0:root:/root:/disk0/bin/sh\nrabbitbone:x:1000:1000:Rabbitbone User:/home/rabbitbone:/disk0/bin/sh\nguest:x:1001:1000:Guest:/tmp:/disk0/bin/sh\n";
    const std::string group = "root:x:0:\nusers:x:1000:rabbitbone,guest\nadmin:x:27:rabbitbone\n";
    const std::string sudoers = "root ALL=(ALL) ALL\nrabbitbone ALL=(ALL) ALL\n";

    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/hello.txt", bytes_from_string(hello), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/README", bytes_from_string(readme), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/etc/issue", bytes_from_string(issue), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/etc/profile", bytes_from_string(profile), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/etc/passwd", bytes_from_string(passwd), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/etc/group", bytes_from_string(group), 0100644 }, next_ino, seen_paths);
    add_seed_file(root_files, etc_files, bin_files, sbin_files, SeedFile{ "/etc/sudoers", bytes_from_string(sudoers), 0100440 }, next_ino, seen_paths);
    for (const auto &f : userland_files) add_seed_file(root_files, etc_files, bin_files, sbin_files, f, next_ino, seen_paths);

    std::vector<SeedNode> root_dirs;
    root_dirs.push_back(SeedNode{ kEtcIno, "etc", "/etc", {}, 0040755, 2 });
    root_dirs.push_back(SeedNode{ kBinIno, "bin", "/bin", {}, 0040755, 2 });
    root_dirs.push_back(SeedNode{ kSbinIno, "sbin", "/sbin", {}, 0040755, 2 });
    std::vector<SeedNode> root_children = root_dirs;
    root_children.insert(root_children.end(), root_files.begin(), root_files.end());

    std::uint32_t next_block = kDataBlockStart;
    const std::uint32_t root_block = next_block++;
    const std::uint32_t etc_block = next_block++;
    const std::uint32_t bin_block = next_block++;
    const std::uint32_t sbin_block = next_block++;
    auto root_block_data = build_dir_block(root_children, kRootIno, kRootIno);
    auto etc_block_data = build_dir_block(etc_files, kEtcIno, kRootIno);
    auto bin_block_data = build_dir_block(bin_files, kBinIno, kRootIno);
    auto sbin_block_data = build_dir_block(sbin_files, kSbinIno, kRootIno);
    std::memcpy(fs.data() + static_cast<std::uint64_t>(root_block) * kFsBlockSize, root_block_data.data(), root_block_data.size());
    std::memcpy(fs.data() + static_cast<std::uint64_t>(etc_block) * kFsBlockSize, etc_block_data.data(), etc_block_data.size());
    std::memcpy(fs.data() + static_cast<std::uint64_t>(bin_block) * kFsBlockSize, bin_block_data.data(), bin_block_data.size());
    std::memcpy(fs.data() + static_cast<std::uint64_t>(sbin_block) * kFsBlockSize, sbin_block_data.data(), sbin_block_data.size());

    auto write_payload_inodes = [&](const std::vector<SeedNode> &nodes) {
        for (const auto &node : nodes) {
            std::vector<std::uint32_t> blocks;
            std::uint32_t indirect = 0;
            write_file_payload(fs, node, next_block, blocks, indirect);
            write_inode_blocks(fs, node.ino, node.mode, static_cast<std::uint32_t>(node.data.size()), blocks, 1, indirect);
        }
    };
    write_payload_inodes(root_files);
    write_payload_inodes(etc_files);
    write_payload_inodes(bin_files);
    write_payload_inodes(sbin_files);

    write_inode(fs, kRootIno, 0040000 | 0755, kFsBlockSize, root_block, 5);
    write_inode(fs, kEtcIno, 0040000 | 0755, kFsBlockSize, etc_block, 2);
    write_inode(fs, kBinIno, 0040000 | 0755, kFsBlockSize, bin_block, 2);
    write_inode(fs, kSbinIno, 0040000 | 0755, kFsBlockSize, sbin_block, 2);

    const std::uint32_t used_blocks = next_block;
    const std::uint32_t used_inodes = next_ino - 1u;
    if (used_blocks > kFsBlocks || used_inodes > kFsInodes) throw std::runtime_error("seed filesystem metadata exhausted");
    const std::uint32_t free_blocks = kFsBlocks - used_blocks;
    const std::uint32_t free_inodes = kFsInodes - used_inodes;

    auto *sb = fs.data() + 1024;
    put_u32le(sb + 0, kFsInodes);
    put_u32le(sb + 4, kFsBlocks);
    put_u32le(sb + 12, free_blocks);
    put_u32le(sb + 16, free_inodes);
    put_u32le(sb + 20, kFirstDataBlock);
    put_u32le(sb + 24, 0);
    put_u32le(sb + 32, kBlocksPerGroup);
    put_u32le(sb + 40, kInodesPerGroup);
    put_u16le(sb + 52, 1);
    put_u16le(sb + 54, 20);
    put_u16le(sb + 56, 0xef53);
    put_u16le(sb + 58, 1);
    put_u16le(sb + 60, 1);
    put_u32le(sb + 76, 1);
    put_u32le(sb + 84, 11);
    put_u16le(sb + 88, 128);
    put_u32le(sb + 96, 0);
    put_u32le(sb + 100, 0x0002);
    put_u32le(sb + 104, 0);
    const char label[] = "RABBITBONE_BOOT";
    std::memcpy(sb + 120, label, sizeof(label) - 1u);
    put_u16le(sb + 254, 32);

    auto *gd = fs.data() + 2u * kFsBlockSize;
    put_u32le(gd + 0, kBlockBitmapBlock);
    put_u32le(gd + 4, kInodeBitmapBlock);
    put_u32le(gd + 8, kInodeTableBlock);
    put_u16le(gd + 12, static_cast<std::uint16_t>(free_blocks));
    put_u16le(gd + 14, static_cast<std::uint16_t>(free_inodes));
    put_u16le(gd + 16, 4);

    std::vector<std::uint8_t> block_bitmap(kFsBlockSize, 0);
    for (std::uint32_t b = 0; b < used_blocks; ++b) set_bit(block_bitmap, b);
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kBlockBitmapBlock) * kFsBlockSize, block_bitmap.data(), block_bitmap.size());

    std::vector<std::uint8_t> inode_bitmap(kFsBlockSize, 0);
    for (std::uint32_t ino = 1; ino <= used_inodes; ++ino) set_bit(inode_bitmap, ino - 1u);
    std::memcpy(fs.data() + static_cast<std::uint64_t>(kInodeBitmapBlock) * kFsBlockSize, inode_bitmap.data(), inode_bitmap.size());

    return fs;
}
