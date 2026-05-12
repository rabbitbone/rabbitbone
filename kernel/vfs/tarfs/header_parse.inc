static bool tar_octal(const char *s, usize n, usize *out) {
    if (!s || !out) return false;
    usize v = 0;
    bool saw_digit = false;
    for (usize i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0 || c == ' ') {
            for (usize j = i + 1u; j < n; ++j) {
                unsigned char t = (unsigned char)s[j];
                if (t != 0 && t != ' ') return false;
            }
            *out = v;
            return true;
        }
        if (c < '0' || c > '7') return false;
        saw_digit = true;
        if (v > (((usize)-1) - (usize)(c - '0')) / 8u) return false;
        v = v * 8u + (usize)(c - '0');
    }
    if (!saw_digit) return false;
    *out = v;
    return true;
}

static bool tar_type_supported(char typeflag) {
    return typeflag == 0 || typeflag == '0' || typeflag == '5';
}

static bool tar_name_has_parent_ref(const char *s, usize n) {
    usize i = 0;
    while (i < n && s[i]) {
        while (i < n && s[i] == '/') ++i;
        usize start = i;
        while (i < n && s[i] && s[i] != '/') ++i;
        usize len = i - start;
        if (len == 2u && s[start] == '.' && s[start + 1u] == '.') return true;
    }
    return false;
}

static bool block_empty(const u8 *p) {
    for (usize i = 0; i < 512u; ++i) if (p[i]) return false;
    return true;
}

static bool tarfs_path_seen(const tarfs_t *fs, const char *path) {
    if (!fs || !path) return false;
    for (usize i = 0; i < fs->count; ++i) {
        if (strcmp(fs->entries[i].path, path) == 0) return true;
    }
    return false;
}

static bool checksum_ok(const tar_header_t *h) {
    const u8 *p = (const u8 *)h;
    usize sum = 0;
    for (usize i = 0; i < 512u; ++i) sum += p[i];
    for (usize i = 148; i < 156; ++i) sum = sum - p[i] + ' ';
    usize stored = 0;
    if (!tar_octal(h->chksum, sizeof(h->chksum), &stored)) return false;
    return stored != 0 && stored == sum;
}

static bool header_path(const tar_header_t *h, char *out, usize out_size) {
    if (!tar_type_supported(h->typeflag)) return false;
    char tmp[VFS_PATH_MAX];
    usize pos = 0;
    tmp[pos++] = '/';
    if (h->prefix[0]) {
        usize pl = strnlen(h->prefix, sizeof(h->prefix));
        if (tar_name_has_parent_ref(h->prefix, pl)) return false;
        if (pos + pl + 1u >= sizeof(tmp)) return false;
        memcpy(tmp + pos, h->prefix, pl);
        pos += pl;
        tmp[pos++] = '/';
    }
    usize nl = strnlen(h->name, sizeof(h->name));
    if (nl == 0 || tar_name_has_parent_ref(h->name, nl) || pos + nl + 1u >= sizeof(tmp)) return false;
    memcpy(tmp + pos, h->name, nl);
    pos += nl;
    tmp[pos] = 0;
    return path_normalize(tmp, out, out_size);
}

tarfs_t *tarfs_open(const void *image, usize size) {
    if (!image || size < 1024u) return 0;
    tarfs_t *fs = (tarfs_t *)kcalloc(1, sizeof(*fs));
    if (!fs) return 0;
    fs->image = (const u8 *)image;
    fs->size = size;
    usize count = 0;
    for (usize off = 0; off + 512u <= size;) {
        const tar_header_t *h = (const tar_header_t *)(fs->image + off);
        if (block_empty((const u8 *)h)) break;
        if (!checksum_ok(h) || !tar_type_supported(h->typeflag)) { kfree(fs); return 0; }
        usize fsize = 0;
        if (!tar_octal(h->size, sizeof(h->size), &fsize)) { kfree(fs); return 0; }
        usize aligned = AURORA_ALIGN_UP(fsize, 512u);
        if (aligned < fsize || off > size - 512u || aligned > size - off - 512u) { kfree(fs); return 0; }
        ++count;
        off += 512u + aligned;
    }
    fs->entries = (tarfs_entry_t *)kcalloc(count ? count : 1u, sizeof(tarfs_entry_t));
    if (!fs->entries) { kfree(fs); return 0; }
    fs->count = 0;
    u32 inode = 2;
    for (usize off = 0; off + 512u <= size;) {
        const tar_header_t *h = (const tar_header_t *)(fs->image + off);
        if (block_empty((const u8 *)h)) break;
        if (!checksum_ok(h) || !tar_type_supported(h->typeflag)) { kfree(fs->entries); kfree(fs); return 0; }
        usize fsize = 0;
        if (!tar_octal(h->size, sizeof(h->size), &fsize)) { kfree(fs->entries); kfree(fs); return 0; }
        usize aligned = AURORA_ALIGN_UP(fsize, 512u);
        if (aligned < fsize || off > size - 512u || aligned > size - off - 512u) { kfree(fs->entries); kfree(fs); return 0; }
        tarfs_entry_t *e = &fs->entries[fs->count];
        if (!header_path(h, e->path, sizeof(e->path))) { kfree(fs->entries); kfree(fs); return 0; }
        if (tarfs_path_seen(fs, e->path)) { kfree(fs->entries); kfree(fs); return 0; }
        ++fs->count;
        e->data = fs->image + off + 512u;
        e->size = fsize;
        e->type = h->typeflag == '5' ? VFS_NODE_DIR : VFS_NODE_FILE;
        e->inode = inode++;
        off += 512u + aligned;
    }
    return fs;
}

static tarfs_entry_t *find_exact(tarfs_t *fs, const char *path) {
    char norm[VFS_PATH_MAX];
    if (!path_normalize(path, norm, sizeof(norm))) return 0;
    for (usize i = 0; i < fs->count; ++i) if (strcmp(fs->entries[i].path, norm) == 0) return &fs->entries[i];
    return 0;
}

