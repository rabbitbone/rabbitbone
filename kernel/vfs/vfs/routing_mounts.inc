static bool fs_id_in_use(u32 id) {
    if (id == 0) return true;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) if (mounts[i].ops && mounts[i].fs_id == id) return true;
    return false;
}

static u32 alloc_fs_id(void) {
    for (u32 tries = 0; tries < 0xffffffffu; ++tries) {
        u32 id = next_fs_id++;
        if (next_fs_id == 0) next_fs_id = 1;
        if (!fs_id_in_use(id)) return id;
    }
    return 0;
}

static u32 alloc_mount_generation(void) {
    u32 gen = next_mount_generation++;
    if (next_mount_generation == 0) next_mount_generation = 1;
    return gen ? gen : alloc_mount_generation();
}

typedef struct vfs_route {
    bool found;
    usize mount_index;
    u32 mount_generation;
    char normalized[VFS_PATH_MAX];
    char relative[VFS_PATH_MAX];
} vfs_route_t;

static bool vfs_disk0_mount_present(const rabbitbone_rust_mount_view_t *view) {
    if (!view) return false;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (view[i].active && strcmp(view[i].path, "/disk0") == 0) return true;
    }
    return false;
}

static bool vfs_rewrite_disk_userland_alias(const char *path, const rabbitbone_rust_mount_view_t *view, char *out, usize out_size) {
    if (!path || !out || out_size == 0 || !vfs_disk0_mount_present(view)) return false;
    const char *suffix = 0;
    if (strcmp(path, "/bin") == 0) {
        suffix = "/bin";
    } else if (strncmp(path, "/bin/", 5u) == 0) {
        suffix = path;
    } else if (strcmp(path, "/sbin") == 0) {
        suffix = "/sbin";
    } else if (strncmp(path, "/sbin/", 6u) == 0) {
        suffix = path;
    }
    if (!suffix) return false;
    int written = ksnprintf(out, out_size, "/disk0%s", suffix);
    return written > 0 && (usize)written < out_size;
}

static vfs_status_t resolve_route(const char *path, vfs_route_t *route) {
    if (!path || !route) return VFS_ERR_INVAL;
    memset(route, 0, sizeof(*route));

    if (!rabbitbone_rust_path_policy_check((const u8 *)path, VFS_PATH_MAX)) return VFS_ERR_INVAL;
    usize len = strnlen(path, VFS_PATH_MAX);
    if (len == 0 || len >= VFS_PATH_MAX || path[0] != '/') return VFS_ERR_INVAL;

    rabbitbone_rust_mount_view_t view[VFS_MAX_MOUNTS];
    u32 generations[VFS_MAX_MOUNTS];
    memset(view, 0, sizeof(view));
    memset(generations, 0, sizeof(generations));
    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || mounts[i].unmounting) continue;
        view[i].active = 1;
        strlcpy(view[i].path, mounts[i].path, sizeof(view[i].path));
        generations[i] = mounts[i].generation;
    }
    spin_unlock_irqrestore(&mounts_lock, flags);

    char alias_path[VFS_PATH_MAX];
    const char *route_path = path;
    usize route_len = len;
    if (vfs_rewrite_disk_userland_alias(path, view, alias_path, sizeof(alias_path))) {
        route_path = alias_path;
        route_len = strlen(alias_path);
    }

    rabbitbone_rust_route_out_t out;
    memset(&out, 0, sizeof(out));
    i32 rc = rabbitbone_rust_vfs_route(view, (const u8 *)route_path, route_len + 1u, &out);
    if (rc == VFS_ERR_NOENT) return VFS_ERR_NOENT;
    if (rc != VFS_OK) return VFS_ERR_INVAL;
    if (!out.found || out.mount_index >= VFS_MAX_MOUNTS || generations[out.mount_index] == 0) return VFS_ERR_NOENT;

    flags = spin_lock_irqsave(&mounts_lock);
    bool still_valid = mounts[out.mount_index].ops && !mounts[out.mount_index].unmounting &&
                       mounts[out.mount_index].generation == generations[out.mount_index];
    spin_unlock_irqrestore(&mounts_lock, flags);
    if (!still_valid) return VFS_ERR_NOENT;

    route->found = true;
    route->mount_index = out.mount_index;
    route->mount_generation = generations[out.mount_index];
    memcpy(route->normalized, out.normalized, sizeof(route->normalized));
    memcpy(route->relative, out.relative, sizeof(route->relative));
    route->normalized[sizeof(route->normalized) - 1u] = 0;
    route->relative[sizeof(route->relative) - 1u] = 0;
    return VFS_OK;
}


static void fill_default_statvfs(const vfs_mount_t *mnt, vfs_statvfs_t *out) {
    if (!mnt || !out) return;
    memset(out, 0, sizeof(*out));
    out->block_size = 4096u;
    out->fs_id = mnt->fs_id;
    out->flags = mnt->writable ? 0u : VFS_STATVFS_FLAG_READONLY;
    out->max_name_len = VFS_NAME_MAX - 1u;
    strlcpy(out->mount_path, mnt->path, sizeof(out->mount_path));
    strlcpy(out->fs_name, mnt->name, sizeof(out->fs_name));
}

static vfs_mount_t *mount_acquire_index(usize idx, u32 generation) {
    if (idx >= VFS_MAX_MOUNTS || generation == 0) return 0;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    vfs_mount_t *mnt = 0;
    if (mounts[idx].ops && !mounts[idx].unmounting && mounts[idx].generation == generation) {
        ++mounts[idx].active_refs;
        mnt = &mounts[idx];
    }
    spin_unlock_irqrestore(&mounts_lock, flags);
    return mnt;
}

static void mount_release(vfs_mount_t *mnt) {
    if (!mnt) return;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    if (mnt->active_refs) --mnt->active_refs;
    spin_unlock_irqrestore(&mounts_lock, flags);
}

static bool mount_release_by_fs_id(u32 fs_id, u32 generation) {
    if (fs_id == 0 || generation == 0) return false;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    bool released = false;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && mounts[i].fs_id == fs_id && mounts[i].generation == generation) {
            if (mounts[i].active_refs) {
                --mounts[i].active_refs;
                released = true;
            }
            break;
        }
    }
    spin_unlock_irqrestore(&mounts_lock, flags);
    return released;
}

static vfs_mount_t *route_mount(vfs_route_t *route) {
    if (!route || !route->found) return 0;
    return mount_acquire_index(route->mount_index, route->mount_generation);
}

static vfs_mount_t *mount_by_fs_id(u32 fs_id, u32 generation) {
    if (fs_id == 0 || generation == 0) return 0;
    u64 flags = spin_lock_irqsave(&mounts_lock);
    vfs_mount_t *mnt = 0;
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (mounts[i].ops && !mounts[i].unmounting && mounts[i].fs_id == fs_id && mounts[i].generation == generation) {
            ++mounts[i].active_refs;
            mnt = &mounts[i];
            break;
        }
    }
    spin_unlock_irqrestore(&mounts_lock, flags);
    return mnt;
}

static void fill_ref_from_stat(vfs_node_ref_t *out, const char *path, const vfs_stat_t *st, const vfs_route_t *route) {
    if (!out || !st) return;
    memset(out, 0, sizeof(*out));
    out->fs_id = st->fs_id;
    out->inode = st->inode;
    out->type = st->type;
    out->size = st->size;
    out->mount_generation = route ? route->mount_generation : 0;
    if (path) strlcpy(out->path, path, sizeof(out->path));
}

typedef struct root_list_ctx {
    vfs_dir_iter_fn fn;
    void *user;
    char seen[VFS_MAX_MOUNTS * 2u][VFS_NAME_MAX];
    usize seen_count;
    bool stopped;
} root_list_ctx_t;

static bool root_name_seen(const root_list_ctx_t *ctx, const char *name) {
    if (!ctx || !name) return false;
    for (usize i = 0; i < ctx->seen_count; ++i) {
        if (strcmp(ctx->seen[i], name) == 0) return true;
    }
    return false;
}

static void root_remember_name(root_list_ctx_t *ctx, const char *name) {
    if (!ctx || !name || !name[0] || ctx->seen_count >= VFS_MAX_MOUNTS * 2u) return;
    if (root_name_seen(ctx, name)) return;
    strlcpy(ctx->seen[ctx->seen_count], name, VFS_NAME_MAX);
    ctx->seen[ctx->seen_count][VFS_NAME_MAX - 1u] = 0;
    ++ctx->seen_count;
}

static bool root_forward_record(const vfs_dirent_t *entry, void *raw) {
    root_list_ctx_t *ctx = (root_list_ctx_t *)raw;
    if (!ctx || !entry) return false;
    root_remember_name(ctx, entry->name);
    if (!ctx->fn(entry, ctx->user)) {
        ctx->stopped = true;
        return false;
    }
    return true;
}

static vfs_status_t root_list(vfs_dir_iter_fn fn, void *ctx) {
    if (!fn) return VFS_ERR_INVAL;

    usize root_idx = VFS_MAX_MOUNTS;
    u32 root_gen = 0;
    vfs_dirent_t mount_entries[VFS_MAX_MOUNTS];
    usize mount_count = 0;

    u64 flags = spin_lock_irqsave(&mounts_lock);
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || mounts[i].unmounting) continue;
        if (strcmp(mounts[i].path, "/") == 0) {
            root_idx = i;
            root_gen = mounts[i].generation;
            break;
        }
    }
    for (usize i = 0; i < VFS_MAX_MOUNTS; ++i) {
        if (!mounts[i].ops || mounts[i].unmounting || strcmp(mounts[i].path, "/") == 0) continue;
        const char *p = mounts[i].path;
        if (p[0] != '/' || p[1] == 0) continue;
        const char *start = p + 1u;
        const char *slash = strchr(start, '/');
        usize name_len = slash ? (usize)(slash - start) : strlen(start);
        if (name_len == 0 || name_len >= VFS_NAME_MAX) continue;
        bool duplicate = false;
        for (usize j = 0; j < mount_count; ++j) {
            if (strlen(mount_entries[j].name) == name_len && memcmp(mount_entries[j].name, start, name_len) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        vfs_dirent_t de;
        memset(&de, 0, sizeof(de));
        memcpy(de.name, start, name_len);
        de.name[name_len] = 0;
        de.type = VFS_NODE_DIR;
        de.inode = mounts[i].fs_id;
        mount_entries[mount_count++] = de;
    }
    spin_unlock_irqrestore(&mounts_lock, flags);

    root_list_ctx_t forward;
    memset(&forward, 0, sizeof(forward));
    forward.fn = fn;
    forward.user = ctx;

    if (root_idx < VFS_MAX_MOUNTS) {
        vfs_mount_t *mnt = mount_acquire_index(root_idx, root_gen);
        if (mnt && mnt->ops->list) {
            vfs_status_t st = mnt->ops->list(mnt, "/", root_forward_record, &forward);
            mount_release(mnt);
            if (st != VFS_OK) return st;
            if (forward.stopped) return VFS_OK;
        } else if (mnt) {
            mount_release(mnt);
        }
    }
    for (usize i = 0; i < mount_count; ++i) {
        if (root_name_seen(&forward, mount_entries[i].name)) continue;
        root_remember_name(&forward, mount_entries[i].name);
        if (!fn(&mount_entries[i], ctx)) break;
    }
    return VFS_OK;
}
