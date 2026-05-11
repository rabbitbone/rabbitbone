#include <aurora/syscall.h>
#include <aurora/vfs.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/libc.h>
#include <aurora/drivers.h>
#include <aurora/process.h>
#include <aurora/kmem.h>
#include <aurora/rust.h>
#include <aurora/version.h>
#include <aurora/scheduler.h>
#include <aurora/timer.h>
#include <aurora/arch/io.h>
#include <aurora/spinlock.h>
#include <aurora/panic.h>
#include <aurora/tty.h>

#define SYSCALL_MAX_HANDLES 32u
#define SYSCALL_PATH_MAX VFS_PATH_MAX
#define SYSCALL_IO_CHUNK 4096u
#define SYSCALL_MAX_IO_BYTES (1024u * 1024u)
#define SYSCALL_VERSION_VALUE AURORA_SYSCALL_ABI_VERSION

typedef enum sys_handle_kind {
    SYS_HANDLE_EMPTY = 0,
    SYS_HANDLE_VFS = 1,
    SYS_HANDLE_PIPE_READ = 2,
    SYS_HANDLE_PIPE_WRITE = 3,
    SYS_HANDLE_CONSOLE_IN = 4,
    SYS_HANDLE_CONSOLE_OUT = 5,
    SYS_HANDLE_CONSOLE_ERR = 6,
} sys_handle_kind_t;

typedef struct sys_handle {
    bool used;
    char path[VFS_PATH_MAX];
    u64 offset;
    vfs_node_type_t type;
    u64 size;
    u32 inode;
    u32 fs_id;
    u32 flags;
    u32 kind;
    u32 pipe_id;
    u32 file_id;
} sys_handle_t;

#define SYSCALL_PIPE_CAP 32u
#define SYSCALL_PIPE_BUFFER AURORA_PIPE_BUF
#define SYSCALL_FILE_CAP 64u


typedef struct sys_file {
    bool used;
    u32 id;
    u32 refs;
    char path[VFS_PATH_MAX];
    u64 offset;
    vfs_node_type_t type;
    u64 size;
    u32 inode;
    u32 fs_id;
    spinlock_t lock;
} sys_file_t;

typedef struct sys_pipe {
    bool used;
    u32 id;
    u8 *data;
    usize read_pos;
    usize len;
    u64 total_read;
    u64 total_written;
    u32 read_refs;
    u32 write_refs;
    spinlock_t lock;
} sys_pipe_t;

static sys_handle_t kernel_handles[SYSCALL_MAX_HANDLES];
static sys_handle_t user_handles[SYSCALL_MAX_HANDLES];
static sys_file_t *files;
static sys_pipe_t pipes[SYSCALL_PIPE_CAP];
static u32 next_file_id = 1u;
static u32 next_pipe_id = 1u;
static spinlock_t file_table_lock;
static spinlock_t pipe_table_lock;
static bool initialized;
AURORA_STATIC_ASSERT(user_handle_snapshot_fits, sizeof(user_handles) <= SYSCALL_USER_HANDLE_SNAPSHOT_BYTES);

static syscall_result_t ok(i64 v) { syscall_result_t r = { v, 0 }; return r; }
static syscall_result_t err(i64 e) { syscall_result_t r = { -1, e }; return r; }
static sys_handle_t *active_handles(void) { return process_user_active() ? user_handles : kernel_handles; }
static bool add_user_ptr(u64 base, usize off, u64 *out) { return !__builtin_add_overflow(base, (u64)off, out); }

static void init_stdio_handles(sys_handle_t *handles) {
    if (!handles) return;
    memset(&handles[AURORA_STDIN], 0, sizeof(handles[AURORA_STDIN]));
    handles[AURORA_STDIN].used = true;
    handles[AURORA_STDIN].kind = SYS_HANDLE_CONSOLE_IN;
    handles[AURORA_STDIN].type = VFS_NODE_DEV;
    handles[AURORA_STDIN].fs_id = 0x434F4E53u;
    handles[AURORA_STDIN].inode = AURORA_STDIN;
    strncpy(handles[AURORA_STDIN].path, "console:[stdin]", sizeof(handles[AURORA_STDIN].path) - 1u);

    memset(&handles[AURORA_STDOUT], 0, sizeof(handles[AURORA_STDOUT]));
    handles[AURORA_STDOUT].used = true;
    handles[AURORA_STDOUT].kind = SYS_HANDLE_CONSOLE_OUT;
    handles[AURORA_STDOUT].type = VFS_NODE_DEV;
    handles[AURORA_STDOUT].fs_id = 0x434F4E53u;
    handles[AURORA_STDOUT].inode = AURORA_STDOUT;
    strncpy(handles[AURORA_STDOUT].path, "console:[stdout]", sizeof(handles[AURORA_STDOUT].path) - 1u);

    memset(&handles[AURORA_STDERR], 0, sizeof(handles[AURORA_STDERR]));
    handles[AURORA_STDERR].used = true;
    handles[AURORA_STDERR].kind = SYS_HANDLE_CONSOLE_ERR;
    handles[AURORA_STDERR].type = VFS_NODE_DEV;
    handles[AURORA_STDERR].fs_id = 0x434F4E53u;
    handles[AURORA_STDERR].inode = AURORA_STDERR;
    strncpy(handles[AURORA_STDERR].path, "console:[stderr]", sizeof(handles[AURORA_STDERR].path) - 1u);
}

static void handle_release_resource(sys_handle_t *h);
static bool handle_retain_resource(const sys_handle_t *h);
static void close_handle(sys_handle_t *handles, usize h);
static sys_file_t *file_by_id(u32 id);
static sys_file_t *handle_file(sys_handle_t *h);
static sys_pipe_t *alloc_pipe(void);
static void release_pipe(sys_pipe_t *p);

void syscall_reset_user_handles(void) {
    memset(user_handles, 0, sizeof(user_handles));
    init_stdio_handles(user_handles);
}

void syscall_prepare_user_handle_snapshot(void *dst, usize dst_size) {
    if (!dst || dst_size < sizeof(user_handles)) return;
    sys_handle_t tmp[SYSCALL_MAX_HANDLES];
    memset(tmp, 0, sizeof(tmp));
    init_stdio_handles(tmp);
    memcpy(dst, tmp, sizeof(tmp));
}

usize syscall_user_handle_snapshot_size(void) {
    return sizeof(user_handles);
}

void syscall_save_user_handles(void *dst, usize dst_size) {
    if (!dst || dst_size < sizeof(user_handles)) return;
    memcpy(dst, user_handles, sizeof(user_handles));
}

bool syscall_retain_user_handle_snapshot(const void *src, usize src_size) {
    if (!src || src_size < sizeof(user_handles)) return false;
    const sys_handle_t *handles = (const sys_handle_t *)src;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && !handle_retain_resource(&handles[i])) {
            for (usize j = 0; j < i; ++j) {
                if (handles[j].used) {
                    sys_handle_t tmp = handles[j];
                    handle_release_resource(&tmp);
                }
            }
            return false;
        }
    }
    return true;
}

void syscall_release_user_handle_snapshot(void *src, usize src_size) {
    if (!src || src_size < sizeof(user_handles)) return;
    sys_handle_t *handles = (sys_handle_t *)src;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used) close_handle(handles, i);
    }
}

void syscall_close_user_handles_with_flags(u32 flags) {
    if (!flags) return;
    for (usize i = 1; i < SYSCALL_MAX_HANDLES; ++i) {
        if (user_handles[i].used && (user_handles[i].flags & flags)) close_handle(user_handles, i);
    }
}

bool syscall_load_user_handles(const void *src, usize src_size) {
    if (!src || src_size < sizeof(user_handles)) return false;
    memcpy(user_handles, src, sizeof(user_handles));
    return true;
}

void syscall_init(void) {
    memset(kernel_handles, 0, sizeof(kernel_handles));
    memset(user_handles, 0, sizeof(user_handles));
    init_stdio_handles(kernel_handles);
    init_stdio_handles(user_handles);
    if (!files) {
        files = (sys_file_t *)kmalloc(sizeof(sys_file_t) * SYSCALL_FILE_CAP);
        if (!files) PANIC("syscall open-file table allocation failed");
    }
    memset(files, 0, sizeof(sys_file_t) * SYSCALL_FILE_CAP);
    memset(pipes, 0, sizeof(pipes));
    spinlock_init(&file_table_lock);
    spinlock_init(&pipe_table_lock);
    next_file_id = 1u;
    next_pipe_id = 1u;
    initialized = true;
    KLOG(LOG_INFO, "syscall", "Rust syscall dispatcher initialized handles=%u", SYSCALL_MAX_HANDLES);
}

const char *syscall_name(u64 no) {
    return (const char *)aurora_rust_syscall_name(no);
}

static sys_file_t *file_by_id(u32 id) {
    if (!id) return 0;
    for (usize i = 0; i < SYSCALL_FILE_CAP; ++i) {
        if (files[i].used && files[i].id == id) return &files[i];
    }
    return 0;
}

static sys_file_t *handle_file(sys_handle_t *h) {
    if (!h || !h->used || h->kind != SYS_HANDLE_VFS || !h->file_id) return 0;
    return file_by_id(h->file_id);
}

static bool file_retain(u32 id) {
    u64 flags = spin_lock_irqsave(&file_table_lock);
    sys_file_t *f = file_by_id(id);
    if (!f || f->refs == 0xffffffffu) {
        spin_unlock_irqrestore(&file_table_lock, flags);
        return false;
    }
    ++f->refs;
    spin_unlock_irqrestore(&file_table_lock, flags);
    return true;
}

static void file_release(u32 id) {
    if (!id) return;
    u64 flags = spin_lock_irqsave(&file_table_lock);
    sys_file_t *f = file_by_id(id);
    if (!f) {
        spin_unlock_irqrestore(&file_table_lock, flags);
        return;
    }
    if (f->refs == 0) PANIC("open-file object double close");
    --f->refs;
    if (f->refs == 0) memset(f, 0, sizeof(*f));
    spin_unlock_irqrestore(&file_table_lock, flags);
}

static sys_file_t *alloc_file_object(const char *path, const vfs_stat_t *st) {
    if (!path || !st) return 0;
    u64 flags = spin_lock_irqsave(&file_table_lock);
    for (usize i = 0; i < SYSCALL_FILE_CAP; ++i) {
        if (!files[i].used) {
            memset(&files[i], 0, sizeof(files[i]));
            files[i].used = true;
            files[i].id = next_file_id++;
            if (next_file_id == 0) next_file_id = 1u;
            files[i].refs = 1u;
            strncpy(files[i].path, path, sizeof(files[i].path) - 1u);
            files[i].offset = 0;
            files[i].type = st->type;
            files[i].size = st->size;
            files[i].inode = st->inode;
            files[i].fs_id = st->fs_id;
            spinlock_init(&files[i].lock);
            spin_unlock_irqrestore(&file_table_lock, flags);
            return &files[i];
        }
    }
    spin_unlock_irqrestore(&file_table_lock, flags);
    return 0;
}

static void sync_handle_from_file(sys_handle_t *h, const sys_file_t *f) {
    if (!h || !f) return;
    h->type = f->type;
    h->size = f->size;
    h->inode = f->inode;
    h->fs_id = f->fs_id;
    h->offset = f->offset;
    strncpy(h->path, f->path, sizeof(h->path) - 1u);
}

static void refresh_handle_stat(sys_handle_t *h) {
    if (!h || !h->used || h->kind != SYS_HANDLE_VFS) return;
    sys_file_t *f = handle_file(h);
    if (!f) return;
    vfs_stat_t st;
    if (vfs_stat(f->path, &st) == VFS_OK) {
        u64 flags = spin_lock_irqsave(&f->lock);
        f->type = st.type;
        f->size = st.size;
        f->inode = st.inode;
        f->fs_id = st.fs_id;
        sync_handle_from_file(h, f);
        spin_unlock_irqrestore(&f->lock, flags);
    }
}

static i64 alloc_handle(sys_handle_t *handles, const char *path, const vfs_stat_t *st) {
    sys_file_t *file = alloc_file_object(path, st);
    if (!file) return -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) {
            memset(&handles[i], 0, sizeof(handles[i]));
            handles[i].used = true;
            handles[i].kind = SYS_HANDLE_VFS;
            handles[i].file_id = file->id;
            sync_handle_from_file(&handles[i], file);
            return (i64)i;
        }
    }
    file_release(file->id);
    return -1;
}

static bool valid_handle(sys_handle_t *handles, usize h) {
    return h < SYSCALL_MAX_HANDLES && handles[h].used;
}

static sys_pipe_t *pipe_by_id(u32 id) {
    if (!id) return 0;
    for (usize i = 0; i < SYSCALL_PIPE_CAP; ++i) {
        if (pipes[i].used && pipes[i].id == id) return &pipes[i];
    }
    return 0;
}

static bool pipe_is_endpoint(const sys_handle_t *h) {
    return h && h->used && (h->kind == SYS_HANDLE_PIPE_READ || h->kind == SYS_HANDLE_PIPE_WRITE);
}

static void release_pipe_locked(sys_pipe_t *p) {
    if (!p) return;
    u8 *data = p->data;
    memset(p, 0, sizeof(*p));
    if (data) kfree(data);
}

static bool pipe_ref(sys_pipe_t *p, sys_handle_kind_t kind) {
    if (!p || !p->used) return false;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (kind == SYS_HANDLE_PIPE_READ) {
        if (p->read_refs == 0xffffffffu) { spin_unlock_irqrestore(&p->lock, flags); return false; }
        ++p->read_refs;
    } else if (kind == SYS_HANDLE_PIPE_WRITE) {
        if (p->write_refs == 0xffffffffu) { spin_unlock_irqrestore(&p->lock, flags); return false; }
        ++p->write_refs;
    } else {
        spin_unlock_irqrestore(&p->lock, flags);
        return false;
    }
    spin_unlock_irqrestore(&p->lock, flags);
    return true;
}

static void pipe_unref(sys_pipe_t *p, sys_handle_kind_t kind) {
    if (!p || !p->used) return;
    bool free_now = false;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (kind == SYS_HANDLE_PIPE_READ) {
        if (p->read_refs == 0) PANIC("pipe read endpoint double close");
        --p->read_refs;
    } else if (kind == SYS_HANDLE_PIPE_WRITE) {
        if (p->write_refs == 0) PANIC("pipe write endpoint double close");
        --p->write_refs;
    }
    free_now = p->read_refs == 0 && p->write_refs == 0;
    spin_unlock_irqrestore(&p->lock, flags);
    if (free_now && p->used && p->read_refs == 0 && p->write_refs == 0) release_pipe_locked(p);
}

static bool handle_retain_resource(const sys_handle_t *h) {
    if (!h || !h->used) return true;
    if (h->kind == SYS_HANDLE_VFS) return h->file_id && file_retain(h->file_id);
    if (!pipe_is_endpoint(h)) return true;
    u64 table_flags = spin_lock_irqsave(&pipe_table_lock);
    sys_pipe_t *p = pipe_by_id(h->pipe_id);
    bool ok = p && pipe_ref(p, (sys_handle_kind_t)h->kind);
    spin_unlock_irqrestore(&pipe_table_lock, table_flags);
    return ok;
}

static void handle_release_resource(sys_handle_t *h) {
    if (!h || !h->used) return;
    if (h->kind == SYS_HANDLE_VFS) {
        file_release(h->file_id);
        return;
    }
    if (!pipe_is_endpoint(h)) return;
    u64 table_flags = spin_lock_irqsave(&pipe_table_lock);
    sys_pipe_t *p = pipe_by_id(h->pipe_id);
    if (p) pipe_unref(p, (sys_handle_kind_t)h->kind);
    spin_unlock_irqrestore(&pipe_table_lock, table_flags);
}

static void close_handle(sys_handle_t *handles, usize h) {
    if (!handles || h >= SYSCALL_MAX_HANDLES || !handles[h].used) return;
    handle_release_resource(&handles[h]);
    memset(&handles[h], 0, sizeof(handles[h]));
}

static bool snapshot_valid(void *snapshot, usize snapshot_size) {
    return snapshot && snapshot_size >= sizeof(user_handles);
}

static bool install_owned_handle(sys_handle_t *handles, u32 target_fd, sys_handle_t *owned) {
    if (!handles || !owned || !owned->used || target_fd >= SYSCALL_MAX_HANDLES) return false;
    if (handles[target_fd].used) close_handle(handles, target_fd);
    handles[target_fd] = *owned;
    memset(owned, 0, sizeof(*owned));
    return true;
}

bool syscall_snapshot_open_vfs(void *snapshot, usize snapshot_size, u32 target_fd, const char *path, u32 flags, bool create_truncate) {
    if (!snapshot_valid(snapshot, snapshot_size) || !path || !*path || target_fd >= SYSCALL_MAX_HANDLES) return false;
    if (flags & ~AURORA_FD_CLOEXEC) return false;
    if (create_truncate) {
        vfs_status_t cs = vfs_create(path, "", 0);
        if (cs != VFS_OK && cs != VFS_ERR_PERM) return false;
    }
    vfs_stat_t st;
    if (vfs_stat(path, &st) != VFS_OK || st.type == VFS_NODE_DIR) return false;
    sys_file_t *file = alloc_file_object(path, &st);
    if (!file) return false;
    sys_handle_t h;
    memset(&h, 0, sizeof(h));
    h.used = true;
    h.kind = SYS_HANDLE_VFS;
    h.flags = flags;
    h.file_id = file->id;
    sync_handle_from_file(&h, file);
    if (!install_owned_handle((sys_handle_t *)snapshot, target_fd, &h)) {
        file_release(file->id);
        return false;
    }
    return true;
}

bool syscall_snapshot_pipe_between(void *writer_snapshot, usize writer_size, u32 writer_fd, void *reader_snapshot, usize reader_size, u32 reader_fd) {
    if (!snapshot_valid(writer_snapshot, writer_size) || !snapshot_valid(reader_snapshot, reader_size)) return false;
    if (writer_fd >= SYSCALL_MAX_HANDLES || reader_fd >= SYSCALL_MAX_HANDLES) return false;
    sys_pipe_t *p = alloc_pipe();
    if (!p) return false;
    sys_handle_t rh;
    sys_handle_t wh;
    memset(&rh, 0, sizeof(rh));
    memset(&wh, 0, sizeof(wh));
    rh.used = true;
    rh.kind = SYS_HANDLE_PIPE_READ;
    rh.type = VFS_NODE_DEV;
    rh.pipe_id = p->id;
    rh.fs_id = 0x50495045u;
    rh.inode = p->id;
    ksnprintf(rh.path, sizeof(rh.path), "pipe:[%u]:read", p->id);
    wh.used = true;
    wh.kind = SYS_HANDLE_PIPE_WRITE;
    wh.type = VFS_NODE_DEV;
    wh.pipe_id = p->id;
    wh.fs_id = 0x50495045u;
    wh.inode = p->id;
    ksnprintf(wh.path, sizeof(wh.path), "pipe:[%u]:write", p->id);
    if (!handle_retain_resource(&rh)) { release_pipe(p); return false; }
    if (!handle_retain_resource(&wh)) { handle_release_resource(&rh); return false; }
    if (!install_owned_handle((sys_handle_t *)reader_snapshot, reader_fd, &rh)) {
        handle_release_resource(&rh);
        handle_release_resource(&wh);
        return false;
    }
    if (!install_owned_handle((sys_handle_t *)writer_snapshot, writer_fd, &wh)) {
        close_handle((sys_handle_t *)reader_snapshot, reader_fd);
        handle_release_resource(&wh);
        return false;
    }
    return true;
}

static sys_pipe_t *alloc_pipe(void) {
    u64 flags = spin_lock_irqsave(&pipe_table_lock);
    for (usize i = 0; i < SYSCALL_PIPE_CAP; ++i) {
        if (!pipes[i].used) {
            memset(&pipes[i], 0, sizeof(pipes[i]));
            pipes[i].data = (u8 *)kmalloc(SYSCALL_PIPE_BUFFER);
            if (!pipes[i].data) { spin_unlock_irqrestore(&pipe_table_lock, flags); return 0; }
            memset(pipes[i].data, 0, SYSCALL_PIPE_BUFFER);
            pipes[i].used = true;
            pipes[i].id = next_pipe_id++;
            if (next_pipe_id == 0) next_pipe_id = 1u;
            spinlock_init(&pipes[i].lock);
            spin_unlock_irqrestore(&pipe_table_lock, flags);
            return &pipes[i];
        }
    }
    spin_unlock_irqrestore(&pipe_table_lock, flags);
    return 0;
}

static void release_pipe(sys_pipe_t *p) {
    if (!p) return;
    u64 flags = spin_lock_irqsave(&pipe_table_lock);
    release_pipe_locked(p);
    spin_unlock_irqrestore(&pipe_table_lock, flags);
}

static i64 alloc_pipe_handle(sys_handle_t *handles, sys_handle_kind_t kind, u32 pipe_id) {
    if (kind != SYS_HANDLE_PIPE_READ && kind != SYS_HANDLE_PIPE_WRITE) return -1;
    sys_handle_t h;
    memset(&h, 0, sizeof(h));
    h.used = true;
    h.kind = (u32)kind;
    h.type = VFS_NODE_DEV;
    h.pipe_id = pipe_id;
    h.fs_id = 0x50495045u;
    h.inode = pipe_id;
    const char *suffix = kind == SYS_HANDLE_PIPE_READ ? ":read" : ":write";
    ksnprintf(h.path, sizeof(h.path), "pipe:[%u]%s", pipe_id, suffix);
    if (!handle_retain_resource(&h)) return -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) {
            handles[i] = h;
            return (i64)i;
        }
    }
    handle_release_resource(&h);
    return -1;
}

static usize pipe_write_bytes(sys_pipe_t *p, const u8 *src, usize len) {
    if (!p || !p->data || !src || !len) return 0;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (p->read_refs == 0) { spin_unlock_irqrestore(&p->lock, flags); return 0; }
    usize room = SYSCALL_PIPE_BUFFER - p->len;
    if (len > room) len = room;
    for (usize i = 0; i < len; ++i) {
        usize pos = (p->read_pos + p->len) % SYSCALL_PIPE_BUFFER;
        p->data[pos] = src[i];
        ++p->len;
    }
    p->total_written += len;
    spin_unlock_irqrestore(&p->lock, flags);
    return len;
}

static usize pipe_read_bytes(sys_pipe_t *p, u8 *dst, usize len) {
    if (!p || !p->data || !dst || !len) return 0;
    u64 flags = spin_lock_irqsave(&p->lock);
    if (len > p->len) len = p->len;
    for (usize i = 0; i < len; ++i) {
        dst[i] = p->data[p->read_pos];
        p->read_pos = (p->read_pos + 1u) % SYSCALL_PIPE_BUFFER;
        --p->len;
    }
    p->total_read += len;
    spin_unlock_irqrestore(&p->lock, flags);
    return len;
}

static void pipe_snapshot(sys_pipe_t *p, aurora_pipeinfo_t *info) {
    if (!p || !info) return;
    u64 flags = spin_lock_irqsave(&p->lock);
    info->pipe_id = p->id;
    info->capacity = SYSCALL_PIPE_BUFFER;
    info->bytes_available = p->len;
    info->total_read = p->total_read;
    info->total_written = p->total_written;
    info->read_refs = p->read_refs;
    info->write_refs = p->write_refs;
    spin_unlock_irqrestore(&p->lock, flags);
}

static bool copy_string_arg(u64 user_or_kernel_ptr, char *out, usize max_len) {
    if (!out || max_len == 0) return false;
    if (process_user_active()) return process_copy_string_from_user((uptr)user_or_kernel_ptr, out, max_len);
    const char *s = (const char *)(uptr)user_or_kernel_ptr;
    if (!s) return false;
    usize n = strnlen(s, max_len);
    if (n >= max_len) return false;
    memcpy(out, s, n + 1u);
    return true;
}

static bool copy_path_arg(u64 user_or_kernel_ptr, char *out) {
    if (!copy_string_arg(user_or_kernel_ptr, out, SYSCALL_PATH_MAX)) return false;
    return aurora_rust_path_policy_check((const u8 *)out, SYSCALL_PATH_MAX);
}

static bool copy_in_buf(void *dst, u64 src, usize len) {
    if (!len) return true;
    if (!dst || !src) return false;
    if (process_user_active()) return process_copy_from_user(dst, (uptr)src, len);
    memcpy(dst, (const void *)(uptr)src, len);
    return true;
}

static bool copy_out_buf(u64 dst, const void *src, usize len) {
    if (!len) return true;
    if (!dst || !src) return false;
    if (process_user_active()) return process_copy_to_user((uptr)dst, src, len);
    memcpy((void *)(uptr)dst, src, len);
    return true;
}

static bool validate_user_read_range(u64 src, usize len) {
    if (!len) return true;
    if (!src) return false;
    u64 end = 0;
    if (__builtin_add_overflow(src, (u64)len - 1u, &end)) return false;
    if (process_user_active()) return process_validate_user_range((uptr)src, len, false);
    return true;
}

static bool validate_user_write_range(u64 dst, usize len) {
    if (!len) return true;
    if (!dst) return false;
    u64 end = 0;
    if (__builtin_add_overflow(dst, (u64)len - 1u, &end)) return false;
    if (process_user_active()) return process_validate_user_range((uptr)dst, len, true);
    return true;
}

static syscall_result_t sys_read_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || !validate_user_write_range(user_buf, len)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_PIPE_WRITE || handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) return err(VFS_ERR_PERM);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN) {
        u32 mode = tty_get_mode();
        while (total < len) {
            char c = 0;
            if (!keyboard_getc(&c)) break;
            if ((mode & AURORA_TTY_MODE_ECHO) && c >= 32) console_putc(c);
            if ((mode & AURORA_TTY_MODE_ECHO) && c == '\n') console_putc('\n');
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, &c, 1u)) return err(VFS_ERR_INVAL);
            ++total;
            if ((mode & AURORA_TTY_MODE_CANON) && c == '\n') break;
        }
        handles[h].offset += total;
        return ok((i64)total);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        while (total < len && p->len > 0) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            usize got = pipe_read_bytes(p, chunk, n);
            u64 ptr = 0;
            if (got && (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, chunk, got))) return err(VFS_ERR_INVAL);
            handles[h].offset += got;
            total += got;
            if (got < n) break;
        }
        return ok((i64)total);
    }
    refresh_handle_stat(&handles[h]);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        usize got = 0;
        u64 ff = spin_lock_irqsave(&f->lock);
        vfs_status_t vs = vfs_read(f->path, f->offset, chunk, n, &got);
        if (vs != VFS_OK) { spin_unlock_irqrestore(&f->lock, ff); return err(vs); }
        u64 ptr = 0;
        if (got && (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, chunk, got))) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_INVAL); }
        f->offset += got;
        sync_handle_from_file(&handles[h], f);
        spin_unlock_irqrestore(&f->lock, ff);
        total += got;
        if (got < n) break;
    }
    return ok((i64)total);
}

static syscall_result_t sys_write_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || !validate_user_read_range(user_buf, len)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_CONSOLE_IN) return err(VFS_ERR_PERM);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        while (total < len) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
            for (usize i = 0; i < n; ++i) console_putc((char)chunk[i]);
            total += n;
        }
        handles[h].offset += total;
        return ok((i64)total);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        while (total < len && p->len < SYSCALL_PIPE_BUFFER) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
            usize wrote = pipe_write_bytes(p, chunk, n);
            handles[h].offset += wrote;
            total += wrote;
            if (wrote < n) break;
        }
        return ok((i64)total);
    }
    refresh_handle_stat(&handles[h]);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        u64 ptr = 0;
        if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
        usize wrote = 0;
        u64 ff = spin_lock_irqsave(&f->lock);
        vfs_status_t vs = vfs_write(f->path, f->offset, chunk, n, &wrote);
        if (vs != VFS_OK) { spin_unlock_irqrestore(&f->lock, ff); return err(vs); }
        f->offset += wrote;
        sync_handle_from_file(&handles[h], f);
        spin_unlock_irqrestore(&f->lock, ff);
        total += wrote;
        if (wrote < n) break;
    }
    return ok((i64)total);
}

syscall_result_t aurora_sys_version(void) {
    return ok(SYSCALL_VERSION_VALUE);
}

syscall_result_t aurora_sys_write_console(u64 ptr, u64 len64) {
    usize n = (usize)len64;
    if ((!ptr && n) || len64 > 65536u) return err(VFS_ERR_INVAL);
    char chunk[256];
    usize done = 0;
    while (done < n) {
        usize step = n - done;
        if (step > sizeof(chunk)) step = sizeof(chunk);
        u64 cptr = 0;
        if (!add_user_ptr(ptr, done, &cptr) || !copy_in_buf(chunk, cptr, step)) return err(VFS_ERR_INVAL);
        for (usize i = 0; i < step; ++i) console_putc(chunk[i]);
        done += step;
    }
    return ok((i64)n);
}

syscall_result_t aurora_sys_open(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_stat(path, &st);
    if (vs != VFS_OK) return err(vs);
    i64 h = alloc_handle(active_handles(), path, &st);
    return h >= 0 ? ok(h) : err(VFS_ERR_NOSPC);
}

syscall_result_t aurora_sys_close(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    close_handle(handles, h);
    return ok(0);
}

syscall_result_t aurora_sys_read(u64 handle, u64 buf, u64 len) {
    if (len > SYSCALL_MAX_IO_BYTES || (usize)len != len) return err(VFS_ERR_INVAL);
    return sys_read_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_write(u64 handle, u64 buf, u64 len) {
    if (len > SYSCALL_MAX_IO_BYTES || (usize)len != len) return err(VFS_ERR_INVAL);
    return sys_write_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_seek(u64 handle, u64 off64, u64 whence) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    i64 off = (i64)off64;
    if (!valid_handle(handles, h) || off < 0 || whence != 0 || handles[h].kind != SYS_HANDLE_VFS) return err(VFS_ERR_INVAL);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    u64 ff = spin_lock_irqsave(&f->lock);
    f->offset = (u64)off;
    sync_handle_from_file(&handles[h], f);
    spin_unlock_irqrestore(&f->lock, ff);
    return ok((i64)handles[h].offset);
}

syscall_result_t aurora_sys_stat(u64 path_ptr, u64 stat_out) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path) || !stat_out) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_stat(path, &st);
    if (vs != VFS_OK) return err(vs);
    return copy_out_buf(stat_out, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_list(u64 path_ptr, u64 callback, u64 ctx64) {
    if (process_user_active()) return err(VFS_ERR_UNSUPPORTED);
    const char *path = (const char *)(uptr)path_ptr;
    vfs_dir_iter_fn fn = (vfs_dir_iter_fn)(uptr)callback;
    void *ctx = (void *)(uptr)ctx64;
    if (!path || !fn) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_list(path, fn, ctx);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_create(u64 path_ptr, u64 data_ptr, u64 len64) {
    char path[SYSCALL_PATH_MAX];
    usize len = (usize)len64;
    if (!copy_path_arg(path_ptr, path) || (!data_ptr && len) || len64 > 65536u) return err(VFS_ERR_INVAL);
    void *tmp = 0;
    if (len) {
        tmp = kmalloc(len);
        if (!tmp) return err(VFS_ERR_NOMEM);
        if (!copy_in_buf(tmp, data_ptr, len)) { kfree(tmp); return err(VFS_ERR_INVAL); }
    }
    vfs_status_t vs = vfs_create(path, tmp, len);
    if (tmp) kfree(tmp);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_mkdir(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_mkdir(path);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_unlink(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_unlink(path);
    return vs == VFS_OK ? ok(0) : err(vs);
}


syscall_result_t aurora_sys_truncate(u64 path_ptr, u64 size) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_truncate(path, size);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_rename(u64 old_path_ptr, u64 new_path_ptr) {
    char old_path[SYSCALL_PATH_MAX];
    char new_path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(old_path_ptr, old_path) || !copy_path_arg(new_path_ptr, new_path)) return err(VFS_ERR_INVAL);
    vfs_status_t vs = vfs_rename(old_path, new_path);
    return vs == VFS_OK ? ok(0) : err(vs);
}

syscall_result_t aurora_sys_ticks(void) {
    return ok((i64)pit_ticks());
}

syscall_result_t aurora_sys_getpid(void) {
    return ok((i64)process_current_pid());
}

syscall_result_t aurora_sys_procinfo(u64 pid64, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    process_info_t info;
    u32 pid = (u32)pid64;
    if (process_user_active()) {
        u32 self = process_current_pid();
        if (pid == 0) pid = self;
        if (pid != self) return err(VFS_ERR_PERM);
    }
    bool found = false;
    if (pid == 0) {
        found = process_current_info(&info);
    } else {
        found = process_lookup(pid, &info);
    }
    if (!found) return err(VFS_ERR_NOENT);
    info.address_space = 0;
    if (!copy_out_buf(out_ptr, &info, sizeof(info))) return err(VFS_ERR_INVAL);
    return ok(0);
}

static bool copy_argv_vector(u64 argv_ptr, u32 argc, char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX], const char *argv[PROCESS_ARG_MAX]) {
    if (argc == 0 || argc > PROCESS_ARG_MAX || !argv_ptr) return false;
    for (u32 i = 0; i < argc; ++i) {
        u64 arg_ptr = 0;
        u64 slot_ptr = 0;
        if (!add_user_ptr(argv_ptr, (usize)i * sizeof(u64), &slot_ptr) || !copy_in_buf(&arg_ptr, slot_ptr, sizeof(arg_ptr)) || !arg_ptr) return false;
        if (!copy_string_arg(arg_ptr, storage[i], SYSCALL_PATH_MAX)) return false;
        if (i == 0 && storage[i][0] == 0) return false;
        argv[i] = storage[i];
    }
    return true;
}


static bool copy_env_vector(u64 env_ptr, u32 envc, char storage[PROCESS_ENV_MAX][SYSCALL_PATH_MAX], const char *envp[PROCESS_ENV_MAX]) {
    if (envc > PROCESS_ENV_MAX) return false;
    if (envc == 0) return true;
    if (!env_ptr) return false;
    for (u32 i = 0; i < envc; ++i) {
        u64 item_ptr = 0;
        u64 slot_ptr = 0;
        if (!add_user_ptr(env_ptr, (usize)i * sizeof(u64), &slot_ptr) || !copy_in_buf(&item_ptr, slot_ptr, sizeof(item_ptr)) || !item_ptr) return false;
        if (!copy_string_arg(item_ptr, storage[i], SYSCALL_PATH_MAX)) return false;
        if (storage[i][0] == 0) return false;
        envp[i] = storage[i];
    }
    return true;
}

syscall_result_t aurora_sys_spawn(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    const char *argv[] = { path };
    u32 pid = 0;
    process_status_t st;
    if (process_async_scheduler_active()) {
        st = process_spawn_async(path, 1, argv, &pid);
    } else {
        process_result_t result;
        st = process_spawn(path, 1, argv, &pid, &result);
    }
    return st == PROC_OK ? ok((i64)pid) : err(VFS_ERR_IO);
}

syscall_result_t aurora_sys_spawnv(u64 path_ptr, u64 argc64, u64 argv_ptr) {
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    memset(storage, 0, sizeof(storage));
    memset(argv, 0, sizeof(argv));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, storage, argv)) return err(VFS_ERR_INVAL);
    u32 pid = 0;
    process_status_t st;
    if (process_async_scheduler_active()) {
        st = process_spawn_async(path, (int)argc64, argv, &pid);
    } else {
        process_result_t result;
        st = process_spawn(path, (int)argc64, argv, &pid, &result);
    }
    return st == PROC_OK ? ok((i64)pid) : err(VFS_ERR_IO);
}

static i64 process_status_to_vfs_error(process_status_t st) {
    switch (st) {
        case PROC_OK: return 0;
        case PROC_ERR_INVAL: return VFS_ERR_INVAL;
        case PROC_ERR_NOMEM: return VFS_ERR_NOMEM;
        case PROC_ERR_RANGE: return VFS_ERR_INVAL;
        case PROC_ERR_IO: return VFS_ERR_IO;
        case PROC_ERR_FORMAT: return VFS_ERR_IO;
        case PROC_ERR_FAULT: return VFS_ERR_IO;
        case PROC_ERR_BUSY: return VFS_ERR_INVAL;
        default: return VFS_ERR_IO;
    }
}

syscall_result_t aurora_sys_exec(u64 path_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    const char *argv[] = { path };
    process_status_t st = process_request_exec(path, 1, argv);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_execv(u64 path_ptr, u64 argc64, u64 argv_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    memset(storage, 0, sizeof(storage));
    memset(argv, 0, sizeof(argv));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, storage, argv)) return err(VFS_ERR_INVAL);
    process_status_t st = process_request_exec(path, (int)argc64, argv);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_execve(u64 path_ptr, u64 argc64, u64 argv_ptr, u64 envc64, u64 envp_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr || envc64 > PROCESS_ENV_MAX || (envc64 && !envp_ptr)) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char argv_storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    char env_storage[PROCESS_ENV_MAX][SYSCALL_PATH_MAX];
    const char *envp[PROCESS_ENV_MAX];
    memset(argv_storage, 0, sizeof(argv_storage));
    memset(argv, 0, sizeof(argv));
    memset(env_storage, 0, sizeof(env_storage));
    memset(envp, 0, sizeof(envp));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, argv_storage, argv)) return err(VFS_ERR_INVAL);
    if (!copy_env_vector(envp_ptr, (u32)envc64, env_storage, envp)) return err(VFS_ERR_INVAL);
    process_status_t st = process_request_execve(path, (int)argc64, argv, (int)envc64, envp);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t aurora_sys_wait(u64 pid64, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    process_info_t info;
    u32 pid = (u32)pid64;
    if (process_user_active()) {
        if (process_async_scheduler_active() && process_request_wait(pid, (uptr)out_ptr)) return ok(0);
        return err(VFS_ERR_NOENT);
    }
    if (process_wait(pid, &info)) {
        return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    return err(VFS_ERR_NOENT);
}


syscall_result_t aurora_sys_yield(void) {
    scheduler_note_yield();
    if (process_async_scheduler_active()) process_request_reschedule();
    return ok(0);
}

syscall_result_t aurora_sys_sleep(u64 ticks) {
    if (ticks > 10000u) return err(VFS_ERR_INVAL);
    scheduler_note_sleep(ticks);
    if (process_async_scheduler_active()) {
        process_request_sleep(ticks);
    } else if (ticks) {
        cpu_sti();
        timer_sleep_ticks(ticks);
    }
    return ok((i64)pit_ticks());
}

syscall_result_t aurora_sys_schedinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sched_stats_t stats;
    scheduler_stats(&stats);
    return copy_out_buf(out_ptr, &stats, sizeof(stats)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_preemptinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    aurora_preemptinfo_t info;
    if (!scheduler_preempt_info(&info)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}


syscall_result_t aurora_sys_dup(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    sys_handle_t cloned = handles[h];
    if (!handle_retain_resource(&cloned)) return err(VFS_ERR_NOENT);
    i64 nh = -1;
    for (usize i = 3; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) { nh = (i64)i; break; }
    }
    if (nh < 0) { handle_release_resource(&cloned); return err(VFS_ERR_NOSPC); }
    cloned.used = true;
    handles[(usize)nh] = cloned;
    return ok(nh);
}


syscall_result_t aurora_sys_dup2(u64 src_handle, u64 target_handle, u64 flags64) {
    sys_handle_t *handles = active_handles();
    usize src = (usize)src_handle;
    usize target = (usize)target_handle;
    if (!valid_handle(handles, src) || target >= SYSCALL_MAX_HANDLES || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 flags = (u32)flags64;
    if (flags & ~AURORA_FD_CLOEXEC) return err(VFS_ERR_INVAL);
    if (src == target) {
        handles[target].flags = flags;
        return ok((i64)target);
    }
    sys_handle_t cloned = handles[src];
    cloned.used = true;
    cloned.flags = flags;
    if (!handle_retain_resource(&cloned)) return err(VFS_ERR_NOENT);
    if (handles[target].used) close_handle(handles, target);
    handles[target] = cloned;
    return ok((i64)target);
}

syscall_result_t aurora_sys_poll(u64 handle, u64 events64) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || events64 == 0 || events64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 events = (u32)events64;
    if (events & ~(AURORA_POLL_READ | AURORA_POLL_WRITE | AURORA_POLL_HUP)) return err(VFS_ERR_INVAL);
    u32 ready = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN) {
        if ((events & AURORA_POLL_READ) && keyboard_pending() > 0) ready |= AURORA_POLL_READ;
        return ok((i64)ready);
    }
    if (handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        if (events & AURORA_POLL_WRITE) ready |= AURORA_POLL_WRITE;
        return ok((i64)ready);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return ok(AURORA_POLL_BADF);
        u64 pf = spin_lock_irqsave(&p->lock);
        if (handles[h].kind == SYS_HANDLE_PIPE_READ) {
            if ((events & AURORA_POLL_READ) && p->len > 0) ready |= AURORA_POLL_READ;
            if ((events & AURORA_POLL_HUP) && p->write_refs == 0) ready |= AURORA_POLL_HUP;
        } else {
            if ((events & AURORA_POLL_WRITE) && p->read_refs > 0 && p->len < SYSCALL_PIPE_BUFFER) ready |= AURORA_POLL_WRITE;
            if ((events & AURORA_POLL_HUP) && p->read_refs == 0) ready |= AURORA_POLL_HUP;
        }
        spin_unlock_irqrestore(&p->lock, pf);
        return ok((i64)ready);
    }
    refresh_handle_stat(&handles[h]);
    if (handles[h].type == VFS_NODE_DIR) return ok(0);
    if ((events & AURORA_POLL_READ) && (handles[h].type == VFS_NODE_FILE || handles[h].type == VFS_NODE_DEV)) {
        if (handles[h].type == VFS_NODE_DEV || handles[h].offset < handles[h].size) ready |= AURORA_POLL_READ;
    }
    if ((events & AURORA_POLL_WRITE) && (handles[h].type == VFS_NODE_FILE || handles[h].type == VFS_NODE_DEV)) ready |= AURORA_POLL_WRITE;
    return ok((i64)ready);
}

syscall_result_t aurora_sys_fdctl(u64 handle, u64 op, u64 flags64) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    u32 flags = (u32)flags64;
    if (flags & ~AURORA_FD_CLOEXEC) return err(VFS_ERR_INVAL);
    switch ((u32)op) {
        case AURORA_FDCTL_GET:
            return ok((i64)handles[h].flags);
        case AURORA_FDCTL_SET:
            handles[h].flags = flags;
            return ok((i64)handles[h].flags);
        default:
            return err(VFS_ERR_INVAL);
    }
}

syscall_result_t aurora_sys_tell(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_VFS) {
        sys_file_t *f = handle_file(&handles[h]);
        if (!f) return err(VFS_ERR_NOENT);
        u64 ff = spin_lock_irqsave(&f->lock);
        sync_handle_from_file(&handles[h], f);
        i64 off = (i64)f->offset;
        spin_unlock_irqrestore(&f->lock, ff);
        return ok(off);
    }
    return ok((i64)handles[h].offset);
}

syscall_result_t aurora_sys_fstat(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    memset(&st, 0, sizeof(st));
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN || handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        st.type = VFS_NODE_DEV;
        st.size = 0;
        st.mode = handles[h].kind == SYS_HANDLE_CONSOLE_IN ? 0400u : 0200u;
        st.inode = handles[h].inode;
        st.fs_id = handles[h].fs_id;
        return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        st.type = VFS_NODE_DEV;
        st.size = p->len;
        st.mode = handles[h].kind == SYS_HANDLE_PIPE_READ ? 0400u : 0200u;
        st.inode = p->id;
        st.fs_id = handles[h].fs_id;
        handles[h].size = p->len;
        return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    vfs_status_t vs = vfs_stat(f->path, &st);
    if (vs != VFS_OK) return err(vs);
    u64 ff = spin_lock_irqsave(&f->lock);
    f->type = st.type;
    f->size = st.size;
    f->inode = st.inode;
    f->fs_id = st.fs_id;
    sync_handle_from_file(&handles[h], f);
    spin_unlock_irqrestore(&f->lock, ff);
    return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_fdinfo(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        handles[h].size = p->len;
        handles[h].inode = p->id;
    }
    aurora_fdinfo_t info;
    memset(&info, 0, sizeof(info));
    info.handle = (u32)h;
    info.type = (u32)handles[h].type;
    info.offset = handles[h].offset;
    info.size = handles[h].size;
    info.inode = handles[h].inode;
    info.fs_id = handles[h].fs_id;
    info.flags = handles[h].flags;
    strncpy(info.path, handles[h].path, sizeof(info.path) - 1u);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

typedef struct readdir_ctx {
    u64 target;
    u64 current;
    bool found;
    aurora_dirent_t out;
} readdir_ctx_t;

static bool readdir_pick(const vfs_dirent_t *entry, void *raw) {
    readdir_ctx_t *ctx = (readdir_ctx_t *)raw;
    if (ctx->current == ctx->target) {
        memset(&ctx->out, 0, sizeof(ctx->out));
        strncpy(ctx->out.name, entry->name, sizeof(ctx->out.name) - 1u);
        ctx->out.type = (u32)entry->type;
        ctx->out.size = entry->size;
        ctx->out.inode = entry->inode;
        ctx->found = true;
        return false;
    }
    ++ctx->current;
    return true;
}

syscall_result_t aurora_sys_readdir(u64 handle, u64 index, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].type != VFS_NODE_DIR) return err(VFS_ERR_NOTDIR);
    readdir_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.target = index;
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    vfs_status_t vs = vfs_list(f->path, readdir_pick, &ctx);
    if (vs != VFS_OK) return err(vs);
    if (!ctx.found) {
        aurora_dirent_t zero;
        memset(&zero, 0, sizeof(zero));
        return copy_out_buf(out_ptr, &zero, sizeof(zero)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    ctx.out.fs_id = handles[h].fs_id;
    return copy_out_buf(out_ptr, &ctx.out, sizeof(ctx.out)) ? ok(1) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_pipe(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    sys_pipe_t *p = alloc_pipe();
    if (!p) return err(VFS_ERR_NOSPC);
    i64 rh = alloc_pipe_handle(handles, SYS_HANDLE_PIPE_READ, p->id);
    if (rh < 0) { release_pipe(p); return err(VFS_ERR_NOSPC); }
    i64 wh = alloc_pipe_handle(handles, SYS_HANDLE_PIPE_WRITE, p->id);
    if (wh < 0) {
        close_handle(handles, (usize)rh);
        return err(VFS_ERR_NOSPC);
    }
    u32 pair[2] = { (u32)rh, (u32)wh };
    if (!copy_out_buf(out_ptr, pair, sizeof(pair))) {
        close_handle(handles, (usize)rh);
        close_handle(handles, (usize)wh);
        return err(VFS_ERR_INVAL);
    }
    return ok(0);
}

syscall_result_t aurora_sys_pipeinfo(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h) || (handles[h].kind != SYS_HANDLE_PIPE_READ && handles[h].kind != SYS_HANDLE_PIPE_WRITE)) return err(VFS_ERR_INVAL);
    sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
    if (!p) return err(VFS_ERR_NOENT);
    aurora_pipeinfo_t info;
    memset(&info, 0, sizeof(info));
    pipe_snapshot(p, &info);
    info.endpoint = handles[h].kind == SYS_HANDLE_PIPE_READ ? 1u : 2u;
    for (usize i = 1; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && handles[i].pipe_id == p->id) {
            if (handles[i].kind == SYS_HANDLE_PIPE_READ && !info.read_handle) info.read_handle = (u32)i;
            if (handles[i].kind == SYS_HANDLE_PIPE_WRITE && !info.write_handle) info.write_handle = (u32)i;
        }
    }
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_exit(u64 code) {
    if (process_user_active() && process_async_scheduler_active()) {
        process_request_exit((i32)code);
        return ok((i64)code);
    }
    if (process_user_active()) process_exit_from_interrupt((i32)code);
    return ok((i64)code);
}

syscall_result_t aurora_sys_fork(void) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (!process_request_fork()) return err(VFS_ERR_NOMEM);
    return ok(0);
}

syscall_result_t aurora_sys_log(u64 msg_ptr) {
    char msg[160];
    if (!copy_string_arg(msg_ptr, msg, sizeof(msg))) return err(VFS_ERR_INVAL);
    KLOG(LOG_INFO, "usersys", "%s", msg);
    return ok(0);
}


syscall_result_t aurora_sys_tty_getinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    aurora_ttyinfo_t info;
    if (!tty_getinfo(&info)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_tty_setmode(u64 mode64) {
    if (mode64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    return tty_set_mode((u32)mode64) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_tty_readkey(u64 out_ptr, u64 flags64) {
    if (!out_ptr || flags64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    aurora_key_event_t ev;
    if (!tty_read_key(&ev, (u32)flags64)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &ev, sizeof(ev)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t syscall_dispatch(u64 no, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    if (!initialized) syscall_init();
    aurora_rust_sysargs_t args = { a0, a1, a2, a3, a4, a5 };
    return aurora_rust_syscall_dispatch(no, args);
}

bool syscall_selftest(void) {
    if (!aurora_rust_syscall_selftest()) return false;
    syscall_result_t r = syscall_dispatch(AURORA_SYS_VERSION, 0, 0, 0, 0, 0, 0);
    if (r.error || (u64)r.value != SYSCALL_VERSION_VALUE) return false;
    r = syscall_dispatch(999, 0, 0, 0, 0, 0, 0);
    if (r.value != -1 || r.error != VFS_ERR_UNSUPPORTED) return false;
    r = syscall_dispatch(AURORA_SYS_WRITE_CONSOLE, 0, 1, 0, 0, 0, 0);
    if (r.value != -1 || r.error != VFS_ERR_INVAL) return false;
    aurora_fdinfo_t stdinfo;
    memset(&stdinfo, 0, sizeof(stdinfo));
    r = syscall_dispatch(AURORA_SYS_FDINFO, AURORA_STDOUT, (u64)(uptr)&stdinfo, 0, 0, 0, 0);
    if (r.error || stdinfo.handle != AURORA_STDOUT || strcmp(stdinfo.path, "console:[stdout]") != 0) return false;
    static const char stdiomsg[] = "";
    r = syscall_dispatch(AURORA_SYS_WRITE, AURORA_STDOUT, (u64)(uptr)stdiomsg, 0, 0, 0, 0);
    if (r.error || r.value != 0) return false;

    const char *path = "/tmp/syscall-selftest.txt";
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)path, 0, 0, 0, 0, 0);
    static const char seed[] = "syscall seed";
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)path, (u64)(uptr)seed, sizeof(seed) - 1u, 0, 0, 0);
    if (r.error) return false;
    const char *rename_src = "/tmp/syscall-rename-src.txt";
    const char *rename_dst = "/tmp/syscall-rename-dst.txt";
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)rename_src, 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)rename_dst, 0, 0, 0, 0, 0);
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)rename_src, (u64)(uptr)seed, sizeof(seed) - 1u, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_TRUNCATE, (u64)(uptr)rename_src, 4u, 0, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_RENAME, (u64)(uptr)rename_src, (u64)(uptr)rename_dst, 0, 0, 0, 0);
    if (r.error) return false;
    vfs_stat_t rename_st;
    if (vfs_stat(rename_dst, &rename_st) != VFS_OK || rename_st.size != 4u || vfs_stat(rename_src, &rename_st) != VFS_ERR_NOENT) return false;
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)rename_dst, 0, 0, 0, 0, 0);
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)path, 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0) return false;
    u64 h = (u64)r.value;
    vfs_stat_t fst;
    r = syscall_dispatch(AURORA_SYS_FSTAT, h, (u64)(uptr)&fst, 0, 0, 0, 0);
    if (r.error || fst.type != VFS_NODE_FILE || fst.size != sizeof(seed) - 1u) return false;
    aurora_fdinfo_t fi;
    r = syscall_dispatch(AURORA_SYS_FDINFO, h, (u64)(uptr)&fi, 0, 0, 0, 0);
    if (r.error || fi.handle != h || strcmp(fi.path, path) != 0 || fi.flags != 0) return false;
    r = syscall_dispatch(AURORA_SYS_FDCTL, h, AURORA_FDCTL_SET, AURORA_FD_CLOEXEC, 0, 0, 0);
    if (r.error || r.value != AURORA_FD_CLOEXEC) return false;
    r = syscall_dispatch(AURORA_SYS_FDCTL, h, AURORA_FDCTL_GET, 0, 0, 0, 0);
    if (r.error || r.value != AURORA_FD_CLOEXEC) return false;
    r = syscall_dispatch(AURORA_SYS_FDCTL, h, AURORA_FDCTL_SET, 0, 0, 0, 0);
    if (r.error || r.value != 0) return false;
    r = syscall_dispatch(AURORA_SYS_DUP, h, 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0 || (u64)r.value == h) return false;
    u64 duph = (u64)r.value;
    r = syscall_dispatch(AURORA_SYS_TELL, duph, 0, 0, 0, 0, 0);
    if (r.error || r.value != 0) return false;
    r = syscall_dispatch(AURORA_SYS_DUP2, h, 30u, AURORA_FD_CLOEXEC, 0, 0, 0);
    if (r.error || r.value != 30) return false;
    aurora_fdinfo_t remap_info;
    memset(&remap_info, 0, sizeof(remap_info));
    r = syscall_dispatch(AURORA_SYS_FDINFO, 30u, (u64)(uptr)&remap_info, 0, 0, 0, 0);
    if (r.error || remap_info.handle != 30u || !(remap_info.flags & AURORA_FD_CLOEXEC)) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, 30u, 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_CLOSE, duph, 0, 0, 0, 0, 0);
    u32 pipefds[2] = {0, 0};
    r = syscall_dispatch(AURORA_SYS_PIPE, (u64)(uptr)pipefds, 0, 0, 0, 0, 0);
    if (r.error || pipefds[0] == 0 || pipefds[1] == 0 || pipefds[0] == pipefds[1]) return false;
    static const char pipeseed[] = "pipe-selftest";
    r = syscall_dispatch(AURORA_SYS_WRITE, pipefds[1], (u64)(uptr)pipeseed, sizeof(pipeseed) - 1u, 0, 0, 0);
    if (r.error || r.value != (i64)(sizeof(pipeseed) - 1u)) return false;
    aurora_pipeinfo_t pipeinfo;
    r = syscall_dispatch(AURORA_SYS_PIPEINFO, pipefds[0], (u64)(uptr)&pipeinfo, 0, 0, 0, 0);
    if (r.error || pipeinfo.bytes_available != sizeof(pipeseed) - 1u || pipeinfo.capacity != AURORA_PIPE_BUF || pipeinfo.read_refs != 1u || pipeinfo.write_refs != 1u) return false;
    r = syscall_dispatch(AURORA_SYS_POLL, pipefds[0], AURORA_POLL_READ, 0, 0, 0, 0);
    if (r.error || (r.value & AURORA_POLL_READ) == 0) return false;
    r = syscall_dispatch(AURORA_SYS_POLL, pipefds[1], AURORA_POLL_WRITE, 0, 0, 0, 0);
    if (r.error || (r.value & AURORA_POLL_WRITE) == 0) return false;
    char pipebuf[32];
    memset(pipebuf, 0, sizeof(pipebuf));
    r = syscall_dispatch(AURORA_SYS_READ, pipefds[0], (u64)(uptr)pipebuf, sizeof(pipeseed) - 1u, 0, 0, 0);
    if (r.error || r.value != (i64)(sizeof(pipeseed) - 1u) || strcmp(pipebuf, pipeseed) != 0) return false;
    r = syscall_dispatch(AURORA_SYS_READ, pipefds[1], (u64)(uptr)pipebuf, 1, 0, 0, 0);
    if (r.value != -1 || r.error != VFS_ERR_PERM) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, pipefds[0], 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_CLOSE, pipefds[1], 0, 0, 0, 0, 0);
    u32 hupfds[2] = {0, 0};
    r = syscall_dispatch(AURORA_SYS_PIPE, (u64)(uptr)hupfds, 0, 0, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_CLOSE, hupfds[1], 0, 0, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_POLL, hupfds[0], AURORA_POLL_READ | AURORA_POLL_HUP, 0, 0, 0, 0);
    if (r.error || (r.value & AURORA_POLL_HUP) == 0) return false;
    r = syscall_dispatch(AURORA_SYS_CLOSE, hupfds[0], 0, 0, 0, 0, 0);
    if (r.error) return false;
    u32 leakfds[2] = {0, 0};
    for (usize i = 0; i < SYSCALL_PIPE_CAP + 1u; ++i) {
        r = syscall_dispatch(AURORA_SYS_PIPE, (u64)(uptr)leakfds, 0, 0, 0, 0, 0);
        if (r.error) return false;
        if (syscall_dispatch(AURORA_SYS_CLOSE, leakfds[0], 0, 0, 0, 0, 0).error) return false;
        if (syscall_dispatch(AURORA_SYS_CLOSE, leakfds[1], 0, 0, 0, 0, 0).error) return false;
    }
    u32 p1[2] = {0, 0};
    u32 p2[2] = {0, 0};
    if (syscall_dispatch(AURORA_SYS_PIPE, (u64)(uptr)p1, 0, 0, 0, 0, 0).error) return false;
    if (syscall_dispatch(AURORA_SYS_PIPE, (u64)(uptr)p2, 0, 0, 0, 0, 0).error) return false;
    r = syscall_dispatch(AURORA_SYS_DUP2, p1[0], p2[0], 0, 0, 0, 0);
    if (r.error || r.value != (i64)p2[0]) return false;
    r = syscall_dispatch(AURORA_SYS_POLL, p2[1], AURORA_POLL_WRITE | AURORA_POLL_HUP, 0, 0, 0, 0);
    if (r.error || (r.value & AURORA_POLL_HUP) == 0) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, p1[0], 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_CLOSE, p1[1], 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_CLOSE, p2[0], 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_CLOSE, p2[1], 0, 0, 0, 0, 0);
    char buf[32];
    memset(buf, 0, sizeof(buf));
    r = syscall_dispatch(AURORA_SYS_READ, h, (u64)(uptr)buf, sizeof(buf) - 1u, 0, 0, 0);
    if (r.error || strcmp(buf, seed) != 0) return false;
    r = syscall_dispatch(AURORA_SYS_SEEK, h, 0, 0, 0, 0, 0);
    if (r.error) return false;
    static const char patch[] = "RUST";
    r = syscall_dispatch(AURORA_SYS_WRITE, h, (u64)(uptr)patch, sizeof(patch) - 1u, 0, 0, 0);
    if (r.error) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, h, 0, 0, 0, 0, 0);
    memset(buf, 0, sizeof(buf));
    usize got = 0;
    if (vfs_read(path, 0, buf, sizeof(buf) - 1u, &got) != VFS_OK || strstr(buf, "RUST") == 0) return false;
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)"/", 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0) return false;
    u64 dh = (u64)r.value;
    aurora_dirent_t de;
    r = syscall_dispatch(AURORA_SYS_READDIR, dh, 0, (u64)(uptr)&de, 0, 0, 0);
    if (r.error || r.value != 1 || de.name[0] == 0) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, dh, 0, 0, 0, 0, 0);
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)path, 0, 0, 0, 0, 0);
    process_info_t pi;
    r = syscall_dispatch(AURORA_SYS_PROCINFO, 0xffffffffu, (u64)(uptr)&pi, 0, 0, 0, 0);
    if (r.value != -1 || r.error != VFS_ERR_NOENT) return false;
    r = syscall_dispatch(AURORA_SYS_GETPID, 0, 0, 0, 0, 0, 0);
    if (r.error || r.value != 0) return false;
    r = syscall_dispatch(AURORA_SYS_SPAWN, (u64)(uptr)"/bin/hello", 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0) return false;
    process_info_t waited;
    r = syscall_dispatch(AURORA_SYS_WAIT, (u64)r.value, (u64)(uptr)&waited, 0, 0, 0, 0);
    if (r.error || waited.exit_code != 7 || waited.state != PROCESS_STATE_EXITED) return false;
    const char *spawnv_argv[] = { "/bin/fscheck", "/disk0/hello.txt" };
    r = syscall_dispatch(AURORA_SYS_SPAWNV, (u64)(uptr)"/bin/fscheck", 2u, (u64)(uptr)spawnv_argv, 0, 0, 0);
    if (r.error || r.value <= 0) return false;
    r = syscall_dispatch(AURORA_SYS_WAIT, (u64)r.value, (u64)(uptr)&waited, 0, 0, 0, 0);
    if (r.error || waited.exit_code != 0 || waited.state != PROCESS_STATE_EXITED) return false;
    sched_stats_t st;
    r = syscall_dispatch(AURORA_SYS_SCHEDINFO, (u64)(uptr)&st, 0, 0, 0, 0, 0);
    if (r.error || st.queue_capacity != SCHED_QUEUE_CAP || st.quantum_ticks == 0) return false;
    aurora_preemptinfo_t preempt;
    r = syscall_dispatch(AURORA_SYS_PREEMPTINFO, (u64)(uptr)&preempt, 0, 0, 0, 0, 0);
    if (r.error || !preempt.enabled || preempt.quantum_ticks == 0) return false;
    r = syscall_dispatch(AURORA_SYS_YIELD, 0, 0, 0, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_SLEEP, 0, 0, 0, 0, 0, 0);
    if (r.error) return false;
    return true;
}
