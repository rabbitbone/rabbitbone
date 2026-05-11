#ifndef AURORA_SYSCALL_H
#define AURORA_SYSCALL_H
#include <aurora/types.h>
#include <aurora/abi.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef enum aurora_syscall_no {
    AURORA_SYSCALL_VERSION = AURORA_SYS_VERSION,
    AURORA_SYSCALL_WRITE_CONSOLE = AURORA_SYS_WRITE_CONSOLE,
    AURORA_SYSCALL_OPEN = AURORA_SYS_OPEN,
    AURORA_SYSCALL_CLOSE = AURORA_SYS_CLOSE,
    AURORA_SYSCALL_READ = AURORA_SYS_READ,
    AURORA_SYSCALL_STAT = AURORA_SYS_STAT,
    AURORA_SYSCALL_LIST = AURORA_SYS_LIST,
    AURORA_SYSCALL_EXIT = AURORA_SYS_EXIT,
    AURORA_SYSCALL_LOG = AURORA_SYS_LOG,
    AURORA_SYSCALL_WRITE = AURORA_SYS_WRITE,
    AURORA_SYSCALL_SEEK = AURORA_SYS_SEEK,
    AURORA_SYSCALL_CREATE = AURORA_SYS_CREATE,
    AURORA_SYSCALL_MKDIR = AURORA_SYS_MKDIR,
    AURORA_SYSCALL_UNLINK = AURORA_SYS_UNLINK,
    AURORA_SYSCALL_TICKS = AURORA_SYS_TICKS,
    AURORA_SYSCALL_GETPID = AURORA_SYS_GETPID,
    AURORA_SYSCALL_PROCINFO = AURORA_SYS_PROCINFO,
    AURORA_SYSCALL_SPAWN = AURORA_SYS_SPAWN,
    AURORA_SYSCALL_WAIT = AURORA_SYS_WAIT,
    AURORA_SYSCALL_YIELD = AURORA_SYS_YIELD,
    AURORA_SYSCALL_SLEEP = AURORA_SYS_SLEEP,
    AURORA_SYSCALL_SCHEDINFO = AURORA_SYS_SCHEDINFO,
    AURORA_SYSCALL_DUP = AURORA_SYS_DUP,
    AURORA_SYSCALL_TELL = AURORA_SYS_TELL,
    AURORA_SYSCALL_FSTAT = AURORA_SYS_FSTAT,
    AURORA_SYSCALL_FDINFO = AURORA_SYS_FDINFO,
    AURORA_SYSCALL_READDIR = AURORA_SYS_READDIR,
    AURORA_SYSCALL_SPAWNV = AURORA_SYS_SPAWNV,
    AURORA_SYSCALL_PREEMPTINFO = AURORA_SYS_PREEMPTINFO,
    AURORA_SYSCALL_FORK = AURORA_SYS_FORK,
    AURORA_SYSCALL_EXEC = AURORA_SYS_EXEC,
    AURORA_SYSCALL_EXECV = AURORA_SYS_EXECV,
    AURORA_SYSCALL_FDCTL = AURORA_SYS_FDCTL,
    AURORA_SYSCALL_EXECVE = AURORA_SYS_EXECVE,
    AURORA_SYSCALL_PIPE = AURORA_SYS_PIPE,
    AURORA_SYSCALL_PIPEINFO = AURORA_SYS_PIPEINFO,
    AURORA_SYSCALL_DUP2 = AURORA_SYS_DUP2,
    AURORA_SYSCALL_POLL = AURORA_SYS_POLL,
    AURORA_SYSCALL_TTY_GETINFO = AURORA_SYS_TTY_GETINFO,
    AURORA_SYSCALL_TTY_SETMODE = AURORA_SYS_TTY_SETMODE,
    AURORA_SYSCALL_TTY_READKEY = AURORA_SYS_TTY_READKEY,
    AURORA_SYSCALL_TRUNCATE = AURORA_SYS_TRUNCATE,
    AURORA_SYSCALL_RENAME = AURORA_SYS_RENAME,
    AURORA_SYSCALL_MAX = AURORA_SYS_MAX
} aurora_syscall_no_t;

typedef struct syscall_result {
    i64 value;
    i64 error;
} syscall_result_t;

void syscall_init(void);
#define SYSCALL_USER_HANDLE_SNAPSHOT_BYTES 12288u

void syscall_reset_user_handles(void);
void syscall_prepare_user_handle_snapshot(void *dst, usize dst_size);
void syscall_save_user_handles(void *dst, usize dst_size);
bool syscall_retain_user_handle_snapshot(const void *src, usize src_size);
void syscall_release_user_handle_snapshot(void *src, usize src_size);
void syscall_close_user_handles_with_flags(u32 flags);
bool syscall_load_user_handles(const void *src, usize src_size);
usize syscall_user_handle_snapshot_size(void);

bool syscall_snapshot_open_vfs(void *snapshot, usize snapshot_size, u32 target_fd, const char *path, u32 flags, bool create_truncate);
bool syscall_snapshot_pipe_between(void *writer_snapshot, usize writer_size, u32 writer_fd, void *reader_snapshot, usize reader_size, u32 reader_fd);
syscall_result_t syscall_dispatch(u64 no, u64 a0, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5);
const char *syscall_name(u64 no);
bool syscall_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
