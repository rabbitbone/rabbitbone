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
        else if (k == "--size-mib") a.size_mib = parse_u64_strict(need("--size-mib"), "size-mib");
        else if (k == "--force") a.force = true;
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
        if (stage2.size() > static_cast<std::uint64_t>(kStage2ReservedSectors) * kSectorSize) throw std::runtime_error("stage2 exceeds reserved sectors");
        const std::uint64_t kernel_sectors = ceil_div_u64(kernel.size(), kSectorSize);
        if (kernel_sectors == 0) throw std::runtime_error("kernel image is empty");
        if (kernel_sectors > kKernelLoadMaxSectors) {
            throw std::runtime_error("kernel exceeds stage2 low-memory load window: " + std::to_string(kernel.size()) +
                                     " bytes need " + std::to_string(kernel_sectors) + " sectors, max " +
                                     std::to_string(kKernelLoadMaxSectors));
        }
        if (kernel_sectors > kKernelBootAreaSectors) throw std::runtime_error("kernel boot area overlaps partition");

        const std::uint64_t image_size = args.size_mib * 1024ull * 1024ull;
        const std::uint64_t total_sectors = image_size / 512ull;
        if (total_sectors <= kPartitionLba + 4096) throw std::runtime_error("image too small");
        auto seed_fs = make_ext4_seed_fs();
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

        const std::filesystem::path out_path(args.out);
        const auto out_status = symlink_status_allow_missing(out_path, "output path");
        const bool out_exists = std::filesystem::exists(out_status);
        if (std::filesystem::is_symlink(out_status)) throw std::runtime_error("refusing to overwrite symlink output " + args.out);
        if (out_exists && std::filesystem::is_directory(out_status)) throw std::runtime_error("refusing to overwrite directory " + args.out);
        if (out_exists && !args.force) throw std::runtime_error("output exists; pass --force to replace " + args.out);

        OwnedTempFile tmp = OwnedTempFile::create_for(out_path);
        size_fd(tmp.fd(), image_size);
        write_at_fd(tmp.fd(), 0, stage1);
        write_at_fd(tmp.fd(), static_cast<std::uint64_t>(kStage2Lba) * kSectorSize, stage2);
        write_at_fd(tmp.fd(), static_cast<std::uint64_t>(kKernelLba) * kSectorSize, kernel);
        write_at_fd(tmp.fd(), static_cast<std::uint64_t>(kPartitionLba) * 512ull, seed_fs);
        flush_fd(tmp.fd());
        tmp.close_checked();

        const auto final_status = symlink_status_allow_missing(out_path, "output path");
        const bool final_exists = std::filesystem::exists(final_status);
        if (std::filesystem::is_symlink(final_status)) throw std::runtime_error("refusing to overwrite symlink output " + args.out);
        if (final_exists && std::filesystem::is_directory(final_status)) throw std::runtime_error("refusing to overwrite directory " + args.out);
        if (final_exists && !args.force) throw std::runtime_error("output exists; pass --force to replace " + args.out);

        std::filesystem::rename(tmp.path(), out_path);
        tmp.disown();
        std::cout << "created " << args.out << " size=" << args.size_mib << "MiB partition_lba=" << kPartitionLba
                  << " kernel_sectors=" << kernel_sectors
                  << " ext4_seed_blocks=" << kFsBlocks << "\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "aurora-install: " << e.what() << "\n";
        return 1;
    }
}
