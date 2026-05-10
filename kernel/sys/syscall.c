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

#define SYSCALL_MAX_HANDLES 32u
#define SYSCALL_PATH_MAX VFS_PATH_MAX
#define SYSCALL_IO_CHUNK 4096u
#define SYSCALL_VERSION_VALUE AURORA_SYSCALL_ABI_VERSION

typedef struct sys_handle {
    bool used;
    char path[VFS_PATH_MAX];
    u64 offset;
    vfs_node_type_t type;
    u64 size;
    u32 inode;
    u32 fs_id;
} sys_handle_t;

static sys_handle_t kernel_handles[SYSCALL_MAX_HANDLES];
static sys_handle_t user_handles[SYSCALL_MAX_HANDLES];
static bool initialized;
AURORA_STATIC_ASSERT(user_handle_snapshot_fits, sizeof(user_handles) <= SYSCALL_USER_HANDLE_SNAPSHOT_BYTES);

static syscall_result_t ok(i64 v) { syscall_result_t r = { v, 0 }; return r; }
static syscall_result_t err(i64 e) { syscall_result_t r = { -1, e }; return r; }
static sys_handle_t *active_handles(void) { return process_user_active() ? user_handles : kernel_handles; }
static bool add_user_ptr(u64 base, usize off, u64 *out) { return !__builtin_add_overflow(base, (u64)off, out); }

void syscall_reset_user_handles(void) {
    memset(user_handles, 0, sizeof(user_handles));
}

usize syscall_user_handle_snapshot_size(void) {
    return sizeof(user_handles);
}

void syscall_save_user_handles(void *dst, usize dst_size) {
    if (!dst || dst_size < sizeof(user_handles)) return;
    memcpy(dst, user_handles, sizeof(user_handles));
}

bool syscall_load_user_handles(const void *src, usize src_size) {
    if (!src || src_size < sizeof(user_handles)) return false;
    memcpy(user_handles, src, sizeof(user_handles));
    return true;
}

void syscall_init(void) {
    memset(kernel_handles, 0, sizeof(kernel_handles));
    memset(user_handles, 0, sizeof(user_handles));
    initialized = true;
    KLOG(LOG_INFO, "syscall", "Rust syscall dispatcher initialized handles=%u", SYSCALL_MAX_HANDLES);
}

const char *syscall_name(u64 no) {
    return (const char *)aurora_rust_syscall_name(no);
}

static void refresh_handle_stat(sys_handle_t *h) {
    if (!h || !h->used) return;
    vfs_stat_t st;
    if (vfs_stat(h->path, &st) == VFS_OK) {
        h->type = st.type;
        h->size = st.size;
        h->inode = st.inode;
        h->fs_id = st.fs_id;
    }
}

static i64 alloc_handle(sys_handle_t *handles, const char *path, const vfs_stat_t *st) {
    for (usize i = 1; i < SYSCALL_MAX_HANDLES; ++i) {
        if (!handles[i].used) {
            memset(&handles[i], 0, sizeof(handles[i]));
            handles[i].used = true;
            strncpy(handles[i].path, path, sizeof(handles[i].path) - 1u);
            if (st) {
                handles[i].type = st->type;
                handles[i].size = st->size;
                handles[i].inode = st->inode;
                handles[i].fs_id = st->fs_id;
            }
            return (i64)i;
        }
    }
    return -1;
}

static bool valid_handle(sys_handle_t *handles, usize h) {
    return h < SYSCALL_MAX_HANDLES && handles[h].used;
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

static syscall_result_t sys_read_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || (!user_buf && len)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        usize got = 0;
        vfs_status_t vs = vfs_read(handles[h].path, handles[h].offset, chunk, n, &got);
        if (vs != VFS_OK) return err(vs);
        u64 ptr = 0;
        if (got && (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, chunk, got))) return err(VFS_ERR_INVAL);
        handles[h].offset += got;
        total += got;
        if (got < n) break;
    }
    return ok((i64)total);
}

static syscall_result_t sys_write_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || (!user_buf && len)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        u64 ptr = 0;
        if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
        usize wrote = 0;
        vfs_status_t vs = vfs_write(handles[h].path, handles[h].offset, chunk, n, &wrote);
        if (vs != VFS_OK) return err(vs);
        handles[h].offset += wrote;
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
    memset(&handles[h], 0, sizeof(handles[h]));
    return ok(0);
}

syscall_result_t aurora_sys_read(u64 handle, u64 buf, u64 len) {
    return sys_read_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_write(u64 handle, u64 buf, u64 len) {
    return sys_write_impl(active_handles(), (usize)handle, buf, (usize)len);
}

syscall_result_t aurora_sys_seek(u64 handle, u64 off64, u64 whence) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    i64 off = (i64)off64;
    if (!valid_handle(handles, h) || off < 0 || whence != 0) return err(VFS_ERR_INVAL);
    handles[h].offset = (u64)off;
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
    bool found = false;
    if (pid == 0) {
        found = process_current_info(&info);
    } else {
        found = process_lookup(pid, &info);
    }
    if (!found) return err(VFS_ERR_NOENT);
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

syscall_result_t aurora_sys_wait(u64 pid64, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    process_info_t info;
    u32 pid = (u32)pid64;
    if (process_wait(pid, &info)) {
        return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    if (process_async_scheduler_active() && process_request_wait(pid, (uptr)out_ptr)) {
        return ok(0);
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
    i64 nh = alloc_handle(handles, handles[h].path, 0);
    if (nh < 0) return err(VFS_ERR_NOSPC);
    handles[(usize)nh] = handles[h];
    handles[(usize)nh].used = true;
    return ok(nh);
}

syscall_result_t aurora_sys_tell(u64 handle) {
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    return ok((i64)handles[h].offset);
}

syscall_result_t aurora_sys_fstat(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_stat(handles[h].path, &st);
    if (vs != VFS_OK) return err(vs);
    handles[h].type = st.type;
    handles[h].size = st.size;
    handles[h].inode = st.inode;
    handles[h].fs_id = st.fs_id;
    return copy_out_buf(out_ptr, &st, sizeof(st)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t aurora_sys_fdinfo(u64 handle, u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sys_handle_t *handles = active_handles();
    usize h = (usize)handle;
    if (!valid_handle(handles, h)) return err(VFS_ERR_INVAL);
    refresh_handle_stat(&handles[h]);
    aurora_fdinfo_t info;
    memset(&info, 0, sizeof(info));
    info.handle = (u32)h;
    info.type = (u32)handles[h].type;
    info.offset = handles[h].offset;
    info.size = handles[h].size;
    info.inode = handles[h].inode;
    info.fs_id = handles[h].fs_id;
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
    vfs_status_t vs = vfs_list(handles[h].path, readdir_pick, &ctx);
    if (vs != VFS_OK) return err(vs);
    if (!ctx.found) {
        aurora_dirent_t zero;
        memset(&zero, 0, sizeof(zero));
        return copy_out_buf(out_ptr, &zero, sizeof(zero)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    ctx.out.fs_id = handles[h].fs_id;
    return copy_out_buf(out_ptr, &ctx.out, sizeof(ctx.out)) ? ok(1) : err(VFS_ERR_INVAL);
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

    const char *path = "/tmp/syscall-selftest.txt";
    (void)syscall_dispatch(AURORA_SYS_UNLINK, (u64)(uptr)path, 0, 0, 0, 0, 0);
    static const char seed[] = "syscall seed";
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)path, (u64)(uptr)seed, sizeof(seed) - 1u, 0, 0, 0);
    if (r.error) return false;
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)path, 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0) return false;
    u64 h = (u64)r.value;
    vfs_stat_t fst;
    r = syscall_dispatch(AURORA_SYS_FSTAT, h, (u64)(uptr)&fst, 0, 0, 0, 0);
    if (r.error || fst.type != VFS_NODE_FILE || fst.size != sizeof(seed) - 1u) return false;
    aurora_fdinfo_t fi;
    r = syscall_dispatch(AURORA_SYS_FDINFO, h, (u64)(uptr)&fi, 0, 0, 0, 0);
    if (r.error || fi.handle != h || strcmp(fi.path, path) != 0) return false;
    r = syscall_dispatch(AURORA_SYS_DUP, h, 0, 0, 0, 0, 0);
    if (r.error || r.value <= 0 || (u64)r.value == h) return false;
    u64 duph = (u64)r.value;
    r = syscall_dispatch(AURORA_SYS_TELL, duph, 0, 0, 0, 0, 0);
    if (r.error || r.value != 0) return false;
    (void)syscall_dispatch(AURORA_SYS_CLOSE, duph, 0, 0, 0, 0, 0);
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
