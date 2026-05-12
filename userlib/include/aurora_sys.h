#ifndef AURORA_USER_SYS_H
#define AURORA_USER_SYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <aurora/abi.h>
#include <aurora/version.h>

typedef unsigned long long au_u64;
typedef long long au_i64;
typedef unsigned long au_usize;

enum au_syscall_id {
    AU_SYS_VERSION = AURORA_SYS_VERSION,
    AU_SYS_WRITE_CONSOLE = AURORA_SYS_WRITE_CONSOLE,
    AU_SYS_OPEN = AURORA_SYS_OPEN,
    AU_SYS_CLOSE = AURORA_SYS_CLOSE,
    AU_SYS_READ = AURORA_SYS_READ,
    AU_SYS_STAT = AURORA_SYS_STAT,
    AU_SYS_LIST = AURORA_SYS_LIST,
    AU_SYS_EXIT = AURORA_SYS_EXIT,
    AU_SYS_LOG = AURORA_SYS_LOG,
    AU_SYS_WRITE = AURORA_SYS_WRITE,
    AU_SYS_SEEK = AURORA_SYS_SEEK,
    AU_SYS_CREATE = AURORA_SYS_CREATE,
    AU_SYS_MKDIR = AURORA_SYS_MKDIR,
    AU_SYS_UNLINK = AURORA_SYS_UNLINK,
    AU_SYS_TICKS = AURORA_SYS_TICKS,
    AU_SYS_GETPID = AURORA_SYS_GETPID,
    AU_SYS_PROCINFO = AURORA_SYS_PROCINFO,
    AU_SYS_SPAWN = AURORA_SYS_SPAWN,
    AU_SYS_WAIT = AURORA_SYS_WAIT,
    AU_SYS_YIELD = AURORA_SYS_YIELD,
    AU_SYS_SLEEP = AURORA_SYS_SLEEP,
    AU_SYS_SCHEDINFO = AURORA_SYS_SCHEDINFO,
    AU_SYS_DUP = AURORA_SYS_DUP,
    AU_SYS_TELL = AURORA_SYS_TELL,
    AU_SYS_FSTAT = AURORA_SYS_FSTAT,
    AU_SYS_FDINFO = AURORA_SYS_FDINFO,
    AU_SYS_READDIR = AURORA_SYS_READDIR,
    AU_SYS_SPAWNV = AURORA_SYS_SPAWNV,
    AU_SYS_PREEMPTINFO = AURORA_SYS_PREEMPTINFO,
    AU_SYS_FORK = AURORA_SYS_FORK,
    AU_SYS_EXEC = AURORA_SYS_EXEC,
    AU_SYS_EXECV = AURORA_SYS_EXECV,
    AU_SYS_FDCTL = AURORA_SYS_FDCTL,
    AU_SYS_EXECVE = AURORA_SYS_EXECVE,
    AU_SYS_PIPE = AURORA_SYS_PIPE,
    AU_SYS_PIPEINFO = AURORA_SYS_PIPEINFO,
    AU_SYS_DUP2 = AURORA_SYS_DUP2,
    AU_SYS_POLL = AURORA_SYS_POLL,
    AU_SYS_TTY_GETINFO = AURORA_SYS_TTY_GETINFO,
    AU_SYS_TTY_SETMODE = AURORA_SYS_TTY_SETMODE,
    AU_SYS_TTY_READKEY = AURORA_SYS_TTY_READKEY,
    AU_SYS_TRUNCATE = AURORA_SYS_TRUNCATE,
    AU_SYS_RENAME = AURORA_SYS_RENAME,
    AU_SYS_SYNC = AURORA_SYS_SYNC,
    AU_SYS_FSYNC = AURORA_SYS_FSYNC,
    AU_SYS_STATVFS = AURORA_SYS_STATVFS,
    AU_SYS_INSTALL_COMMIT = AURORA_SYS_INSTALL_COMMIT,
    AU_SYS_PREALLOCATE = AURORA_SYS_PREALLOCATE,
    AU_SYS_FTRUNCATE = AURORA_SYS_FTRUNCATE,
    AU_SYS_FPREALLOCATE = AURORA_SYS_FPREALLOCATE,
    AU_SYS_CHDIR = AURORA_SYS_CHDIR,
    AU_SYS_GETCWD = AURORA_SYS_GETCWD,
    AU_SYS_FDATASYNC = AURORA_SYS_FDATASYNC,
    AU_SYS_SYMLINK = AURORA_SYS_SYMLINK,
    AU_SYS_READLINK = AURORA_SYS_READLINK,
    AU_SYS_LINK = AURORA_SYS_LINK,
    AU_SYS_LSTAT = AURORA_SYS_LSTAT,
    AU_SYS_THEME = AURORA_SYS_THEME,
    AU_SYS_CRED = AURORA_SYS_CRED,
    AU_SYS_SUDO = AURORA_SYS_SUDO,
    AU_SYS_CHMOD = AURORA_SYS_CHMOD,
    AU_SYS_CHOWN = AURORA_SYS_CHOWN,
    AU_SYS_KCTL = AURORA_SYS_KCTL,
};

typedef struct au_result {
    au_i64 value;
    au_i64 error;
} au_result_t;

static inline int au_result_ok(au_result_t r) { return r.error == 0; }
static inline au_i64 au_result_code(au_result_t r) { return r.error ? r.error : r.value; }

typedef aurora_procinfo_t au_procinfo_t;

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

typedef aurora_fdinfo_t au_fdinfo_t;
typedef aurora_dirent_t au_dirent_t;
typedef aurora_preemptinfo_t au_preemptinfo_t;
typedef aurora_schedinfo_t au_schedinfo_t;
typedef aurora_pipeinfo_t au_pipeinfo_t;
typedef aurora_ttyinfo_t au_ttyinfo_t;
typedef aurora_key_event_t au_key_event_t;
typedef aurora_statvfs_t au_statvfs_t;
typedef aurora_credinfo_t au_credinfo_t;
typedef aurora_userinfo_t au_userinfo_t;

#define AU_STDIN AURORA_STDIN
#define AU_STDOUT AURORA_STDOUT
#define AU_STDERR AURORA_STDERR

AURORA_ABI_STATIC_ASSERT(au_procinfo_alias_size, sizeof(au_procinfo_t) == sizeof(aurora_procinfo_t));
AURORA_ABI_STATIC_ASSERT(au_schedinfo_alias_size, sizeof(au_schedinfo_t) == sizeof(aurora_schedinfo_t));

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
au_i64 au_dup(au_i64 h);
au_i64 au_tell(au_i64 h);
au_i64 au_fstat(au_i64 h, au_stat_t *out);
au_i64 au_fdinfo(au_i64 h, au_fdinfo_t *out);
au_i64 au_readdir(au_i64 h, au_u64 index, au_dirent_t *out);
au_usize au_strlen(const char *s);
int au_strcmp(const char *a, const char *b);
void *au_memset(void *p, int c, au_usize n);
void *au_memcpy(void *d, const void *s, au_usize n);

#ifdef __cplusplus
}
#endif

#endif
