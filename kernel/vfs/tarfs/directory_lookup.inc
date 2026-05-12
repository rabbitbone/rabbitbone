static bool is_direct_child(const char *dir, const char *path, const char **name_out) {
    if (strcmp(dir, "/") == 0) {
        if (path[0] != '/' || path[1] == 0) return false;
        const char *name = path + 1;
        if (strchr(name, '/')) return false;
        *name_out = name;
        return true;
    }
    usize dl = strlen(dir);
    if (strncmp(dir, path, dl) != 0 || path[dl] != '/') return false;
    const char *name = path + dl + 1u;
    if (!*name || strchr(name, '/')) return false;
    *name_out = name;
    return true;
}


static bool immediate_child_component(const char *dir, const char *path, char *name_out, usize cap, bool *implicit_dir_out) {
    if (!dir || !path || !name_out || cap == 0) return false;
    const char *start = 0;
    if (strcmp(dir, "/") == 0) {
        if (path[0] != '/' || path[1] == 0) return false;
        start = path + 1;
    } else {
        usize dl = strlen(dir);
        if (strncmp(dir, path, dl) != 0 || path[dl] != '/') return false;
        start = path + dl + 1u;
        if (!*start) return false;
    }
    const char *slash = strchr(start, '/');
    usize n = slash ? (usize)(slash - start) : strlen(start);
    if (n == 0 || n >= cap) return false;
    memcpy(name_out, start, n);
    name_out[n] = 0;
    if (implicit_dir_out) *implicit_dir_out = slash != 0 && slash[1] != 0;
    return true;
}

static bool has_children(tarfs_t *fs, const char *dir) {
    for (usize i = 0; i < fs->count; ++i) {
        const char *name = 0;
        if (is_direct_child(dir, fs->entries[i].path, &name)) return true;
    }
    return false;
}

