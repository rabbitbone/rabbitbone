#ifndef RABBITBONE_USER_SYS_H
#define RABBITBONE_USER_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <rabbitbone/abi.h>
#include <rabbitbone/version.h>

typedef unsigned long long au_u64;
typedef long long au_i64;
typedef unsigned long au_usize;

enum au_syscall_id {
    AU_SYS_VERSION = RABBITBONE_SYS_VERSION,
    AU_SYS_WRITE_CONSOLE = RABBITBONE_SYS_WRITE_CONSOLE,
    AU_SYS_OPEN = RABBITBONE_SYS_OPEN,
    AU_SYS_CLOSE = RABBITBONE_SYS_CLOSE,
    AU_SYS_READ = RABBITBONE_SYS_READ,
    AU_SYS_STAT = RABBITBONE_SYS_STAT,
    AU_SYS_LIST = RABBITBONE_SYS_LIST,
    AU_SYS_EXIT = RABBITBONE_SYS_EXIT,
    AU_SYS_LOG = RABBITBONE_SYS_LOG,
    AU_SYS_WRITE = RABBITBONE_SYS_WRITE,
    AU_SYS_SEEK = RABBITBONE_SYS_SEEK,
    AU_SYS_CREATE = RABBITBONE_SYS_CREATE,
    AU_SYS_MKDIR = RABBITBONE_SYS_MKDIR,
    AU_SYS_UNLINK = RABBITBONE_SYS_UNLINK,
    AU_SYS_TICKS = RABBITBONE_SYS_TICKS,
    AU_SYS_GETPID = RABBITBONE_SYS_GETPID,
    AU_SYS_PROCINFO = RABBITBONE_SYS_PROCINFO,
    AU_SYS_SPAWN = RABBITBONE_SYS_SPAWN,
    AU_SYS_WAIT = RABBITBONE_SYS_WAIT,
    AU_SYS_YIELD = RABBITBONE_SYS_YIELD,
    AU_SYS_SLEEP = RABBITBONE_SYS_SLEEP,
    AU_SYS_SCHEDINFO = RABBITBONE_SYS_SCHEDINFO,
    AU_SYS_DUP = RABBITBONE_SYS_DUP,
    AU_SYS_TELL = RABBITBONE_SYS_TELL,
    AU_SYS_FSTAT = RABBITBONE_SYS_FSTAT,
    AU_SYS_FDINFO = RABBITBONE_SYS_FDINFO,
    AU_SYS_READDIR = RABBITBONE_SYS_READDIR,
    AU_SYS_SPAWNV = RABBITBONE_SYS_SPAWNV,
    AU_SYS_PREEMPTINFO = RABBITBONE_SYS_PREEMPTINFO,
    AU_SYS_FORK = RABBITBONE_SYS_FORK,
    AU_SYS_EXEC = RABBITBONE_SYS_EXEC,
    AU_SYS_EXECV = RABBITBONE_SYS_EXECV,
    AU_SYS_FDCTL = RABBITBONE_SYS_FDCTL,
    AU_SYS_EXECVE = RABBITBONE_SYS_EXECVE,
    AU_SYS_PIPE = RABBITBONE_SYS_PIPE,
    AU_SYS_PIPEINFO = RABBITBONE_SYS_PIPEINFO,
    AU_SYS_DUP2 = RABBITBONE_SYS_DUP2,
    AU_SYS_POLL = RABBITBONE_SYS_POLL,
    AU_SYS_TTY_GETINFO = RABBITBONE_SYS_TTY_GETINFO,
    AU_SYS_TTY_SETMODE = RABBITBONE_SYS_TTY_SETMODE,
    AU_SYS_TTY_READKEY = RABBITBONE_SYS_TTY_READKEY,
    AU_SYS_TRUNCATE = RABBITBONE_SYS_TRUNCATE,
    AU_SYS_RENAME = RABBITBONE_SYS_RENAME,
    AU_SYS_SYNC = RABBITBONE_SYS_SYNC,
    AU_SYS_FSYNC = RABBITBONE_SYS_FSYNC,
    AU_SYS_STATVFS = RABBITBONE_SYS_STATVFS,
    AU_SYS_INSTALL_COMMIT = RABBITBONE_SYS_INSTALL_COMMIT,
    AU_SYS_PREALLOCATE = RABBITBONE_SYS_PREALLOCATE,
    AU_SYS_FTRUNCATE = RABBITBONE_SYS_FTRUNCATE,
    AU_SYS_FPREALLOCATE = RABBITBONE_SYS_FPREALLOCATE,
    AU_SYS_CHDIR = RABBITBONE_SYS_CHDIR,
    AU_SYS_GETCWD = RABBITBONE_SYS_GETCWD,
    AU_SYS_FDATASYNC = RABBITBONE_SYS_FDATASYNC,
    AU_SYS_SYMLINK = RABBITBONE_SYS_SYMLINK,
    AU_SYS_READLINK = RABBITBONE_SYS_READLINK,
    AU_SYS_LINK = RABBITBONE_SYS_LINK,
    AU_SYS_LSTAT = RABBITBONE_SYS_LSTAT,
    AU_SYS_THEME = RABBITBONE_SYS_THEME,
    AU_SYS_CRED = RABBITBONE_SYS_CRED,
    AU_SYS_SUDO = RABBITBONE_SYS_SUDO,
    AU_SYS_CHMOD = RABBITBONE_SYS_CHMOD,
    AU_SYS_CHOWN = RABBITBONE_SYS_CHOWN,
    AU_SYS_KCTL = RABBITBONE_SYS_KCTL,
    AU_SYS_TTY_SCROLL = RABBITBONE_SYS_TTY_SCROLL,
    AU_SYS_TTY_SETCURSOR = RABBITBONE_SYS_TTY_SETCURSOR,
    AU_SYS_TTY_CLEAR_LINE = RABBITBONE_SYS_TTY_CLEAR_LINE,
    AU_SYS_TTY_CLEAR = RABBITBONE_SYS_TTY_CLEAR,
    AU_SYS_TTY_CURSOR_VISIBLE = RABBITBONE_SYS_TTY_CURSOR_VISIBLE,
    AU_SYS_BRK = RABBITBONE_SYS_BRK,
    AU_SYS_SBRK = RABBITBONE_SYS_SBRK,
    AU_SYS_MMAP = RABBITBONE_SYS_MMAP,
    AU_SYS_MUNMAP = RABBITBONE_SYS_MUNMAP,
    AU_SYS_MPROTECT = RABBITBONE_SYS_MPROTECT,
    AU_SYS_SIGNAL = RABBITBONE_SYS_SIGNAL,
    AU_SYS_SIGACTION = RABBITBONE_SYS_SIGACTION,
    AU_SYS_SIGPROCMASK = RABBITBONE_SYS_SIGPROCMASK,
    AU_SYS_SIGPENDING = RABBITBONE_SYS_SIGPENDING,
    AU_SYS_KILL = RABBITBONE_SYS_KILL,
    AU_SYS_RAISE = RABBITBONE_SYS_RAISE,
    AU_SYS_GETPGRP = RABBITBONE_SYS_GETPGRP,
    AU_SYS_SETPGID = RABBITBONE_SYS_SETPGID,
    AU_SYS_GETPGID = RABBITBONE_SYS_GETPGID,
    AU_SYS_SETSID = RABBITBONE_SYS_SETSID,
    AU_SYS_GETSID = RABBITBONE_SYS_GETSID,
    AU_SYS_TCGETPGRP = RABBITBONE_SYS_TCGETPGRP,
    AU_SYS_TCSETPGRP = RABBITBONE_SYS_TCSETPGRP,
    AU_SYS_SIGRETURN = RABBITBONE_SYS_SIGRETURN,
};

typedef struct au_result {
    au_i64 value;
    au_i64 error;
} au_result_t;

static inline int au_result_ok(au_result_t r) { return r.error == 0; }
static inline au_i64 au_result_code(au_result_t r) { return r.error ? r.error : r.value; }

typedef rabbitbone_procinfo_t au_procinfo_t;

typedef struct au_stat {
    unsigned int type;
    au_u64 size;
    unsigned int mode;
    unsigned int inode;
    unsigned int fs_id;
    unsigned int nlink;
    unsigned int uid;
    unsigned int gid;
} au_stat_t;

typedef rabbitbone_fdinfo_t au_fdinfo_t;
typedef rabbitbone_dirent_t au_dirent_t;
typedef rabbitbone_preemptinfo_t au_preemptinfo_t;
typedef rabbitbone_schedinfo_t au_schedinfo_t;
typedef rabbitbone_pipeinfo_t au_pipeinfo_t;
typedef rabbitbone_ttyinfo_t au_ttyinfo_t;
typedef rabbitbone_key_event_t au_key_event_t;
typedef rabbitbone_statvfs_t au_statvfs_t;
typedef rabbitbone_credinfo_t au_credinfo_t;
typedef rabbitbone_userinfo_t au_userinfo_t;
typedef rabbitbone_sigaction_t au_sigaction_t;
typedef rabbitbone_jobinfo_t au_jobinfo_t;
typedef void (*au_sighandler_t)(int);

#define AU_STDIN RABBITBONE_STDIN
#define AU_STDOUT RABBITBONE_STDOUT
#define AU_STDERR RABBITBONE_STDERR

RABBITBONE_ABI_STATIC_ASSERT(au_procinfo_alias_size, sizeof(au_procinfo_t) == sizeof(rabbitbone_procinfo_t));
RABBITBONE_ABI_STATIC_ASSERT(au_schedinfo_alias_size, sizeof(au_schedinfo_t) == sizeof(rabbitbone_schedinfo_t));

static inline au_result_t au_syscall6(au_u64 id, au_u64 a0, au_u64 a1, au_u64 a2, au_u64 a3, au_u64 a4, au_u64 a5) {
    au_result_t ret;
#if defined(__x86_64__)
    register au_u64 r10 __asm__("r10") = a3;
    register au_u64 r8 __asm__("r8") = a4;
    register au_u64 r9 __asm__("r9") = a5;
    au_i64 value;
    au_i64 error;
    __asm__ volatile("int $0x80"
                     : "=a"(value), "=d"(error)
                     : "a"(id), "D"(a0), "S"(a1), "d"(a2), "r"(r10), "r"(r8), "r"(r9)
                     : "memory", "cc");
    ret.value = value;
    ret.error = error;
#else
    (void)id; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    ret.value = -1;
    ret.error = -11;
#endif
    return ret;
}

static inline au_result_t au_syscall3(au_u64 id, au_u64 a0, au_u64 a1, au_u64 a2) {
    return au_syscall6(id, a0, a1, a2, 0, 0, 0);
}

static inline void au_exit(int code) {
    for (;;) {
        (void)au_syscall3(AU_SYS_EXIT, (au_u64)(unsigned int)code, 0, 0);
        __asm__ volatile("pause" ::: "memory");
    }
}

au_i64 au_write_console(const char *s, au_usize n);
au_i64 au_open(const char *path);
au_i64 au_open2(const char *path, unsigned int flags);
au_i64 au_close(au_i64 h);
au_i64 au_read(au_i64 h, void *buf, au_usize n);
au_i64 au_write(au_i64 h, const void *buf, au_usize n);
au_i64 au_seek(au_i64 h, au_u64 off);
au_i64 au_seek_ex(au_i64 h, au_i64 off, unsigned int whence);
au_i64 au_create(const char *path, const void *data, au_usize n);
au_i64 au_mkdir(const char *path);
au_i64 au_unlink(const char *path);
au_i64 au_truncate(const char *path, au_u64 size);
au_i64 au_rename(const char *old_path, const char *new_path);
au_i64 au_sync(void);
au_i64 au_fsync(au_i64 h);
au_i64 au_fdatasync(au_i64 h);
au_i64 au_statvfs(const char *path, au_statvfs_t *out);
au_i64 au_install_commit(const char *staged_path, const char *final_path);
au_i64 au_preallocate(const char *path, au_u64 size);
au_i64 au_ftruncate(au_i64 h, au_u64 size);
au_i64 au_fpreallocate(au_i64 h, au_u64 size);
au_i64 au_chdir(const char *path);
au_i64 au_getcwd(char *out, au_usize size);
au_i64 au_stat(const char *path, au_stat_t *out);
au_i64 au_lstat(const char *path, au_stat_t *out);
au_i64 au_symlink(const char *target, const char *link_path);
au_i64 au_readlink(const char *path, char *out, au_usize size);
au_i64 au_link(const char *old_path, const char *new_path);
au_i64 au_log(const char *msg);
au_i64 au_ticks(void);
au_i64 au_getpid(void);
au_i64 au_procinfo(unsigned int pid, au_procinfo_t *out);
au_i64 au_spawn(const char *path);
au_i64 au_spawnv(const char *path, unsigned int argc, const char *const *argv);
au_i64 au_spawn_wait(const char *path, au_procinfo_t *out);
au_i64 au_spawnv_wait(const char *path, unsigned int argc, const char *const *argv, au_procinfo_t *out);
au_i64 au_wait(unsigned int pid, au_procinfo_t *out);
au_i64 au_yield(void);
au_i64 au_sleep(au_u64 ticks);
au_i64 au_schedinfo(au_schedinfo_t *out);
au_i64 au_preemptinfo(au_preemptinfo_t *out);
au_i64 au_theme(unsigned int op, unsigned int value);
au_i64 au_cred(unsigned int op, const void *arg0, const void *arg1, au_u64 value);
au_i64 au_sudo(unsigned int op, const char *password, au_u64 value);
au_i64 au_chmod(const char *path, unsigned int mode);
au_i64 au_chown(const char *path, unsigned int uid, unsigned int gid);
au_i64 au_kctl(unsigned int op, char *out, au_usize out_size, const char *arg);
au_i64 au_fork(void);
au_i64 au_exec(const char *path);
au_i64 au_execv(const char *path, unsigned int argc, const char *const *argv);
au_i64 au_execve(const char *path, unsigned int argc, const char *const *argv, unsigned int envc, const char *const *envp);
au_i64 au_fdctl(au_i64 h, unsigned int op, unsigned int flags);
au_i64 au_pipe(unsigned int out_handles[2]);
au_i64 au_pipeinfo(au_i64 h, au_pipeinfo_t *out);
au_i64 au_dup2(au_i64 src, au_i64 target, unsigned int flags);
au_i64 au_poll(au_i64 h, unsigned int events);
au_i64 au_tty_getinfo(au_ttyinfo_t *out);
au_i64 au_tty_setmode(unsigned int mode);
au_i64 au_tty_readkey(au_key_event_t *out, unsigned int flags);
au_i64 au_tty_scroll(int lines);
au_i64 au_tty_setcursor(unsigned int row, unsigned int col);
au_i64 au_tty_clearline(void);
au_i64 au_tty_clear(void);
au_i64 au_tty_cursor_visible(unsigned int visible);
au_i64 au_brk(void *addr);
au_i64 au_sbrk(au_i64 increment);
void *sbrk(long increment);
void *mmap(void *addr, au_usize length, unsigned int prot, unsigned int flags, au_i64 fd, au_u64 offset);
au_i64 munmap(void *addr, au_usize length);
au_i64 mprotect(void *addr, au_usize length, unsigned int prot);
void *au_mmap(void *addr, au_usize length, unsigned int prot, unsigned int flags, au_i64 fd, au_u64 offset);
au_i64 au_munmap(void *addr, au_usize length);
au_i64 au_mprotect(void *addr, au_usize length, unsigned int prot);
au_sighandler_t au_signal(unsigned int sig, au_sighandler_t handler);
au_i64 au_sigaction(unsigned int sig, const au_sigaction_t *act, au_sigaction_t *oldact);
au_i64 au_sigprocmask(unsigned int how, const au_u64 *set, au_u64 *oldset);
au_i64 au_sigpending(au_u64 *out);
au_i64 au_kill(int pid, unsigned int sig);
au_i64 au_raise(unsigned int sig);
au_i64 au_getpgrp(void);
au_i64 au_setpgid(unsigned int pid, unsigned int pgid);
au_i64 au_getpgid(unsigned int pid);
au_i64 au_setsid(void);
au_i64 au_getsid(unsigned int pid);
au_i64 au_tcgetpgrp(void);
au_i64 au_tcsetpgrp(unsigned int pgid);
void *malloc(au_usize size);
void free(void *ptr);
void *calloc(au_usize count, au_usize size);
void *realloc(void *ptr, au_usize size);
au_i64 au_dup(au_i64 h);
au_i64 au_tell(au_i64 h);
au_i64 au_fstat(au_i64 h, au_stat_t *out);
au_i64 au_fdinfo(au_i64 h, au_fdinfo_t *out);
au_i64 au_readdir(au_i64 h, au_u64 index, au_dirent_t *out);
au_usize au_strlen(const char *s);
int au_strcmp(const char *a, const char *b);
void *au_memset(void *p, int c, au_usize n);
void *au_memcpy(void *d, const void *s, au_usize n);
void *au_memmove(void *d, const void *s, au_usize n);
void *memcpy(void *d, const void *s, au_usize n);
void *memmove(void *d, const void *s, au_usize n);
void *memset(void *p, int c, au_usize n);
au_usize strlen(const char *s);
int strcmp(const char *a, const char *b);

#ifdef __cplusplus
}
#endif

#endif
