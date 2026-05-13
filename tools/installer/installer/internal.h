#pragma once

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <unistd.h>
#include <vector>

namespace {

struct InstallFile {
    std::filesystem::path source;
    std::string dest;
};

struct Args {
    std::string out;
    std::string stage1;
    std::string stage2;
    std::string kernel;
    std::uint64_t size_mib = 64;
    bool force = false;
    std::vector<InstallFile> install_files;
};

struct SeedFile {
    std::string path;
    std::vector<std::uint8_t> data;
    std::uint16_t mode = 0100644;
};

constexpr std::uint32_t kPartitionLba = 2048;
constexpr std::uint32_t kSectorSize = 512;
constexpr std::uint32_t kStage2Lba = 1;
constexpr std::uint32_t kStage2ReservedSectors = 64;
constexpr std::uint32_t kKernelLba = kStage2Lba + kStage2ReservedSectors;
constexpr std::uint32_t kKernelLoadBase = 0x10000;
constexpr std::uint32_t kKernelLoadLimit = 0x9f000;
constexpr std::uint32_t kKernelLoadMaxSectors = (kKernelLoadLimit - kKernelLoadBase) / kSectorSize;
constexpr std::uint32_t kKernelBootAreaSectors = kPartitionLba - kKernelLba;
static_assert(kKernelLba == 65, "boot config expects kernel at LBA 65");
static_assert(kKernelLoadMaxSectors <= kKernelBootAreaSectors, "kernel load window exceeds boot area before partition");
constexpr std::uint32_t kFsBlockSize = 1024;
constexpr std::uint32_t kFsBlocks = 8192;
constexpr std::uint32_t kFsInodes = 1024;
constexpr std::uint32_t kBlocksPerGroup = 8192;
constexpr std::uint32_t kInodesPerGroup = 1024;
constexpr std::uint32_t kBlockBitmapBlock = 3;
constexpr std::uint32_t kInodeBitmapBlock = 4;
constexpr std::uint32_t kInodeTableBlock = 5;
constexpr std::uint32_t kFirstDataBlock = 1;
constexpr std::uint32_t kDataBlockStart = 133;
constexpr std::uint32_t kRootIno = 2;
constexpr std::uint32_t kEtcIno = 12;
constexpr std::uint32_t kBinIno = 13;
constexpr std::uint32_t kSbinIno = 14;
constexpr std::uint32_t kFirstDynamicInode = 15;

[[noreturn]] void usage() {
    std::cerr << "usage: aurora-install --out disk.img --stage1 stage1.bin --stage2 stage2.bin --kernel kernel.bin [--size-mib N] [--install SRC:DEST]... [--force]\n";
    std::exit(2);
}


#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

std::string errno_message(const std::string &prefix) {
    return prefix + ": " + std::strerror(errno);
}

InstallFile parse_install_spec(const std::string &spec) {
    const std::size_t sep = spec.find(':');
    if (sep == std::string::npos || sep == 0 || sep + 1 >= spec.size()) {
        throw std::runtime_error("--install expects SRC:DEST");
    }
    InstallFile f;
    f.source = spec.substr(0, sep);
    f.dest = spec.substr(sep + 1);
    if (f.dest.empty() || f.dest[0] != '/') throw std::runtime_error("--install DEST must be absolute: " + f.dest);
    if (f.dest.find("//") != std::string::npos || f.dest.find("/../") != std::string::npos ||
        f.dest.find("/./") != std::string::npos || f.dest == "/." || f.dest == "/..") {
        throw std::runtime_error("--install DEST must be normalized: " + f.dest);
    }
    return f;
}

std::uint64_t parse_u64_strict(const std::string &s, const char *name) {
    if (s.empty()) throw std::runtime_error(std::string(name) + " must be a decimal integer");
    for (char c : s) {
        if (c < '0' || c > '9') throw std::runtime_error(std::string(name) + " must be a decimal integer");
    }
    std::size_t pos = 0;
    std::uint64_t v = 0;
    try {
        v = std::stoull(s, &pos, 10);
    } catch (const std::exception &) {
        throw std::runtime_error(std::string(name) + " is out of range");
    }
    if (pos != s.size()) throw std::runtime_error(std::string(name) + " must be a decimal integer");
    return v;
}

std::uint64_t ceil_div_u64(std::uint64_t value, std::uint64_t divisor) {
    if (divisor == 0) throw std::runtime_error("division by zero");
    return value / divisor + ((value % divisor) ? 1u : 0u);
}

std::filesystem::path unique_temp_path_for(const std::filesystem::path &out_path, unsigned int attempt) {
    const auto parent = out_path.parent_path();
    const std::string stem = out_path.filename().empty() ? "aurora.img" : out_path.filename().string();
    const std::string leaf = "." + stem + ".tmp." + std::to_string(static_cast<unsigned long long>(::getpid())) + "." + std::to_string(attempt);
    return parent.empty() ? std::filesystem::path(leaf) : parent / leaf;
}

class OwnedTempFile {
public:
    OwnedTempFile() = default;
    OwnedTempFile(const OwnedTempFile &) = delete;
    OwnedTempFile &operator=(const OwnedTempFile &) = delete;
    OwnedTempFile(OwnedTempFile &&other) noexcept { move_from(other); }
    OwnedTempFile &operator=(OwnedTempFile &&other) noexcept {
        if (this != &other) {
            cleanup_noexcept();
            move_from(other);
        }
        return *this;
    }

    ~OwnedTempFile() { cleanup_noexcept(); }

    static OwnedTempFile create_for(const std::filesystem::path &out_path) {
        OwnedTempFile tmp;
        for (unsigned int attempt = 0; attempt < 256; ++attempt) {
            auto candidate = unique_temp_path_for(out_path, attempt);
            int fd = ::open(candidate.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (fd >= 0) {
                tmp.fd_ = fd;
                tmp.path_ = candidate;
                tmp.owned_ = true;
                return tmp;
            }
            if (errno == EEXIST) continue;
            throw std::runtime_error(errno_message("cannot create temporary output " + candidate.string()));
        }
        throw std::runtime_error("cannot create unique temporary output for " + out_path.string());
    }

    int fd() const { return fd_; }
    const std::filesystem::path &path() const { return path_; }

    void close_checked() {
        if (fd_ < 0) return;
        if (::close(fd_) != 0) {
            fd_ = -1;
            throw std::runtime_error(errno_message("close failed for " + path_.string()));
        }
        fd_ = -1;
    }

    void disown() { owned_ = false; }

private:
    void cleanup_noexcept() noexcept {
        if (fd_ >= 0) (void)::close(fd_);
        fd_ = -1;
        if (owned_ && !path_.empty()) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
        owned_ = false;
    }

    void move_from(OwnedTempFile &other) noexcept {
        fd_ = other.fd_;
        path_ = std::move(other.path_);
        owned_ = other.owned_;
        other.fd_ = -1;
        other.owned_ = false;
    }

    int fd_ = -1;
    std::filesystem::path path_;
    bool owned_ = false;
};

std::filesystem::file_status symlink_status_allow_missing(const std::filesystem::path &path, const char *what) {
    std::error_code ec;
    auto status = std::filesystem::symlink_status(path, ec);
    if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
        throw std::runtime_error(std::string("cannot stat ") + what + " " + path.string() + ": " + ec.message());
    }
    if (ec) status = std::filesystem::file_status(std::filesystem::file_type::not_found);
    return status;
}

