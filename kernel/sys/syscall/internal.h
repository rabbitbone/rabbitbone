#pragma once

#include <rabbitbone/syscall.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/process.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/rust.h>
#include <rabbitbone/version.h>
#include <rabbitbone/scheduler.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/panic.h>
#include <rabbitbone/tty.h>
#include <rabbitbone/path.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/ktest.h>
#include <rabbitbone/block.h>
#include <rabbitbone/bootinfo.h>
#include <rabbitbone/pci.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/hpet.h>
#include <rabbitbone/smp.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/memory.h>

#define SYSCALL_MAX_HANDLES RABBITBONE_PROCESS_HANDLE_CAP
#define SYSCALL_PATH_MAX VFS_PATH_MAX
#define SYSCALL_IO_CHUNK 4096u
#define SYSCALL_MAX_IO_BYTES (1024u * 1024u)
#define SYSCALL_MMAP_MAX_BYTES (64ull * 4096ull)
#define SYSCALL_VERSION_VALUE RABBITBONE_SYSCALL_ABI_VERSION

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
#define SYSCALL_PIPE_BUFFER RABBITBONE_PIPE_BUF
#define SYSCALL_FILE_CAP 64u
#define SYSCALL_FILE_PER_PROCESS_CAP (SYSCALL_FILE_CAP / 2u)
#define SYSCALL_PIPE_PER_PROCESS_ENDPOINT_CAP (SYSCALL_PIPE_CAP / 2u)
#define SYSCALL_MAX_FILE_SIZE (1ull << 40)


typedef struct sys_file {
    bool used;
    u32 id;
    u32 refs;
    char path[VFS_PATH_MAX];
    u64 offset;
    u32 open_flags;
    vfs_node_type_t type;
    u64 size;
    u32 inode;
    u32 fs_id;
    vfs_node_ref_t ref;
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

static sys_handle_t *kernel_handles;
static sys_handle_t *user_handles;
static sys_file_t *files;
static sys_pipe_t *pipes;

#define SYSCALL_USER_LOG_SLOTS 32u
#define SYSCALL_USER_LOG_WINDOW_TICKS 64ull
#define SYSCALL_USER_LOG_BURST 8u

typedef struct sys_user_log_state {
    bool used;
    u32 pid;
    u64 window_start;
    u32 count;
    u32 suppressed;
} sys_user_log_state_t;

static sys_user_log_state_t user_log_states[SYSCALL_USER_LOG_SLOTS];
static u64 next_file_id = 1u;
static u64 next_pipe_id = 1u;
static spinlock_t file_table_lock;
static spinlock_t pipe_table_lock;
static spinlock_t user_log_lock;
static bool initialized;
static char kernel_cwd[SYSCALL_PATH_MAX] = "/";
#define SYSCALL_HANDLE_TABLE_BYTES (sizeof(sys_handle_t) * SYSCALL_MAX_HANDLES)
#define SYSCALL_PIPE_TABLE_BYTES (sizeof(sys_pipe_t) * SYSCALL_PIPE_CAP)
RABBITBONE_STATIC_ASSERT(user_handle_snapshot_fits, SYSCALL_HANDLE_TABLE_BYTES <= SYSCALL_USER_HANDLE_SNAPSHOT_BYTES);

static syscall_result_t ok(i64 v) { syscall_result_t r = { v, 0 }; return r; }
static syscall_result_t err(i64 e) { syscall_result_t r = { -1, e }; return r; }
static sys_handle_t *active_handles(void) { return process_user_active() ? user_handles : kernel_handles; }
static bool add_user_ptr(u64 base, usize off, u64 *out) { return !__builtin_add_overflow(base, (u64)off, out); }

static bool sys_current_cred(rabbitbone_credinfo_t *out) {
    if (process_user_active()) return process_current_credentials(out);
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->uid = RABBITBONE_UID_ROOT;
    out->euid = RABBITBONE_UID_ROOT;
    out->gid = RABBITBONE_GID_ROOT;
    out->egid = RABBITBONE_GID_ROOT;
    out->is_admin = 1u;
    out->sudo_ttl = RABBITBONE_SUDO_DEFAULT_TTL_TICKS;
    strncpy(out->user, "root", sizeof(out->user) - 1u);
    return true;
}

static bool sys_cred_root(void) {
    rabbitbone_credinfo_t c;
    return sys_current_cred(&c) && c.euid == RABBITBONE_UID_ROOT;
}

static bool sys_cred_can_access_stat(const vfs_stat_t *st, u32 need) {
    rabbitbone_credinfo_t c;
    if (!st || !sys_current_cred(&c)) return false;
    if (c.euid == RABBITBONE_UID_ROOT) return true;
    u32 mode = st->mode & 0777u;
    u32 bits = mode & 0007u;
    if (c.euid == st->uid) bits = (mode >> 6) & 0007u;
    else if (c.egid == st->gid) bits = (mode >> 3) & 0007u;
    return (bits & need) == need;
}

static bool sys_can_read_stat(const vfs_stat_t *st) { return sys_cred_can_access_stat(st, 04u); }
static bool sys_can_write_stat(const vfs_stat_t *st) { return sys_cred_can_access_stat(st, 02u); }
static bool sys_can_exec_stat(const vfs_stat_t *st) { return sys_cred_can_access_stat(st, 01u); }

static bool sys_can_traverse_path(const char *path, bool include_leaf_dir) {
    if (!path || path[0] != '/') return false;
    vfs_stat_t st;
    if (vfs_stat("/", &st) != VFS_OK || st.type != VFS_NODE_DIR || !sys_can_exec_stat(&st)) return false;
    if (path[1] == 0) return true;

    char cur[SYSCALL_PATH_MAX];
    memset(cur, 0, sizeof(cur));
    cur[0] = '/';
    usize cur_len = 1u;
    const char *p = path + 1u;
    while (*p) {
        const char *slash = strchr(p, '/');
        usize comp_len = slash ? (usize)(slash - p) : strlen(p);
        bool leaf = slash == 0;
        if (comp_len == 0 || cur_len + comp_len + 1u >= sizeof(cur)) return false;
        if (leaf && !include_leaf_dir) break;
        if (cur_len > 1u) cur[cur_len++] = '/';
        memcpy(cur + cur_len, p, comp_len);
        cur_len += comp_len;
        cur[cur_len] = 0;
        if (vfs_stat(cur, &st) != VFS_OK || st.type != VFS_NODE_DIR || !sys_can_exec_stat(&st)) return false;
        if (leaf) break;
        p = slash + 1u;
    }
    return true;
}

static bool sys_can_traverse_parent(const char *path) { return sys_can_traverse_path(path, false); }
static bool sys_can_traverse_dir(const char *path) { return sys_can_traverse_path(path, true); }

static bool sys_parent_path(char *out, usize out_size, const char *path) {
    if (!out || out_size == 0 || !path || !path[0]) return false;
    usize n = strnlen(path, SYSCALL_PATH_MAX);
    if (n == 0 || n >= SYSCALL_PATH_MAX || n + 1u > out_size) return false;
    memcpy(out, path, n + 1u);
    char *slash = 0;
    for (char *p = out; *p; ++p) if (*p == '/') slash = p;
    if (!slash || slash == out) { out[0] = '/'; out[1] = 0; return true; }
    *slash = 0;
    return true;
}

static bool sys_can_modify_parent(const char *path) {
    if (!sys_can_traverse_parent(path)) return false;
    char parent[SYSCALL_PATH_MAX];
    if (!sys_parent_path(parent, sizeof(parent), path)) return false;
    vfs_stat_t st;
    if (vfs_stat(parent, &st) != VFS_OK || st.type != VFS_NODE_DIR) return false;
    return sys_can_write_stat(&st) && sys_can_exec_stat(&st);
}

static bool sys_sticky_dir_allows_child_removal(const char *path, bool allow_missing_child) {
    char parent[SYSCALL_PATH_MAX];
    if (!sys_parent_path(parent, sizeof(parent), path)) return false;

    vfs_stat_t parent_st;
    if (vfs_stat(parent, &parent_st) != VFS_OK || parent_st.type != VFS_NODE_DIR) return false;
    if ((parent_st.mode & 01000u) == 0) return true;

    vfs_stat_t child_st;
    vfs_status_t child_status = vfs_lstat(path, &child_st);
    if (child_status == VFS_ERR_NOENT && allow_missing_child) return true;
    if (child_status != VFS_OK) return false;

    rabbitbone_credinfo_t c;
    if (!sys_current_cred(&c)) return false;
    if (c.euid == RABBITBONE_UID_ROOT) return true;
    return c.euid == child_st.uid || c.euid == parent_st.uid;
}

static bool sys_can_unlink_path(const char *path) {
    return sys_can_modify_parent(path) && sys_sticky_dir_allows_child_removal(path, false);
}

static bool sys_can_rename_paths(const char *old_path, const char *new_path) {
    if (!sys_can_modify_parent(old_path) || !sys_can_modify_parent(new_path)) return false;
    if (!sys_sticky_dir_allows_child_removal(old_path, false)) return false;
    if (!sys_sticky_dir_allows_child_removal(new_path, true)) return false;
    return true;
}

static bool sys_can_create_hardlink_to(const vfs_stat_t *st) {
    if (!st || st->type == VFS_NODE_DIR) return false;
    rabbitbone_credinfo_t c;
    if (!sys_current_cred(&c)) return false;
    if (c.euid == RABBITBONE_UID_ROOT) return true;
    if (c.euid != st->uid) return false;
    if ((st->mode & 06000u) != 0) return false;
    return sys_can_read_stat(st);
}

static bool open_flags_valid(u32 flags) {
    if (flags & ~RABBITBONE_O_SUPPORTED) return false;
    u32 acc = flags & RABBITBONE_O_ACCMODE;
    if (acc == RABBITBONE_O_ACCMODE) return false;
    if ((flags & RABBITBONE_O_EXCL) && !(flags & RABBITBONE_O_CREAT)) return false;
    if ((flags & RABBITBONE_O_TRUNC) && acc == RABBITBONE_O_RDONLY) return false;
    return true;
}

static bool open_flags_can_read(u32 flags) {
    u32 acc = flags & RABBITBONE_O_ACCMODE;
    return acc == RABBITBONE_O_RDONLY || acc == RABBITBONE_O_RDWR;
}

static bool open_flags_can_write(u32 flags) {
    u32 acc = flags & RABBITBONE_O_ACCMODE;
    return acc == RABBITBONE_O_WRONLY || acc == RABBITBONE_O_RDWR;
}

static u32 open_flags_to_fd_flags(u32 flags) {
    return (flags & RABBITBONE_O_CLOEXEC) ? RABBITBONE_FD_CLOEXEC : 0u;
}

static void init_stdio_handles(sys_handle_t *handles) {
    if (!handles) return;
    memset(&handles[RABBITBONE_STDIN], 0, sizeof(handles[RABBITBONE_STDIN]));
    handles[RABBITBONE_STDIN].used = true;
    handles[RABBITBONE_STDIN].kind = SYS_HANDLE_CONSOLE_IN;
    handles[RABBITBONE_STDIN].type = VFS_NODE_DEV;
    handles[RABBITBONE_STDIN].fs_id = 0x434F4E53u;
    handles[RABBITBONE_STDIN].inode = RABBITBONE_STDIN;
    strncpy(handles[RABBITBONE_STDIN].path, "console:[stdin]", sizeof(handles[RABBITBONE_STDIN].path) - 1u);

    memset(&handles[RABBITBONE_STDOUT], 0, sizeof(handles[RABBITBONE_STDOUT]));
    handles[RABBITBONE_STDOUT].used = true;
    handles[RABBITBONE_STDOUT].kind = SYS_HANDLE_CONSOLE_OUT;
    handles[RABBITBONE_STDOUT].type = VFS_NODE_DEV;
    handles[RABBITBONE_STDOUT].fs_id = 0x434F4E53u;
    handles[RABBITBONE_STDOUT].inode = RABBITBONE_STDOUT;
    strncpy(handles[RABBITBONE_STDOUT].path, "console:[stdout]", sizeof(handles[RABBITBONE_STDOUT].path) - 1u);

    memset(&handles[RABBITBONE_STDERR], 0, sizeof(handles[RABBITBONE_STDERR]));
    handles[RABBITBONE_STDERR].used = true;
    handles[RABBITBONE_STDERR].kind = SYS_HANDLE_CONSOLE_ERR;
    handles[RABBITBONE_STDERR].type = VFS_NODE_DEV;
    handles[RABBITBONE_STDERR].fs_id = 0x434F4E53u;
    handles[RABBITBONE_STDERR].inode = RABBITBONE_STDERR;
    strncpy(handles[RABBITBONE_STDERR].path, "console:[stderr]", sizeof(handles[RABBITBONE_STDERR].path) - 1u);
}

