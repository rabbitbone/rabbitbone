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



void write_at_fd(int fd, std::uint64_t off, const std::vector<std::uint8_t> &data) {
    if (fd < 0) throw std::runtime_error("invalid output fd");
    if (off > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) throw std::runtime_error("write offset exceeds host limit");
    if (data.size() > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()) - off) throw std::runtime_error("write range exceeds host limit");
    std::size_t done = 0;
    while (done < data.size()) {
        const std::size_t remain = data.size() - done;
        const std::size_t chunk = remain > static_cast<std::size_t>(1u << 30) ? static_cast<std::size_t>(1u << 30) : remain;
        ssize_t wrote = ::pwrite(fd, reinterpret_cast<const char *>(data.data()) + done, chunk, static_cast<off_t>(off + done));
        if (wrote < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error(errno_message("write failed"));
        }
        if (wrote == 0) throw std::runtime_error("write failed: zero-length pwrite");
        done += static_cast<std::size_t>(wrote);
    }
}

void size_fd(int fd, std::uint64_t image_size) {
    if (fd < 0) throw std::runtime_error("invalid output fd");
    if (image_size > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max())) throw std::runtime_error("image size exceeds host limit");
    if (::ftruncate(fd, static_cast<off_t>(image_size)) != 0) throw std::runtime_error(errno_message("cannot size image"));
}

void flush_fd(int fd) {
    if (fd < 0) throw std::runtime_error("invalid output fd");
    while (::fsync(fd) != 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error(errno_message("flush failed"));
    }
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
    if (name.size() > 255u) throw std::runtime_error("dirent name too long");
    const std::uint16_t need = rec_len_for(name.size());
    if (rec_len < need) throw std::runtime_error("dirent rec_len too small");
    if (off > block.size() || rec_len > block.size() - off) throw std::runtime_error("dirent exceeds block");
    put_u32le(block.data() + off, ino);
    put_u16le(block.data() + off + 4, rec_len);
    block[off + 6] = static_cast<std::uint8_t>(name.size());
    block[off + 7] = type;
    std::memcpy(block.data() + off + 8, name.data(), name.size());
}

