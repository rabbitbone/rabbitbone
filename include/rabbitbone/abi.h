#ifndef RABBITBONE_ABI_SHARED_H
#define RABBITBONE_ABI_SHARED_H

#define RABBITBONE_SYS_VERSION 0u
#define RABBITBONE_SYS_WRITE_CONSOLE 1u
#define RABBITBONE_SYS_OPEN 2u
#define RABBITBONE_SYS_CLOSE 3u
#define RABBITBONE_SYS_READ 4u
#define RABBITBONE_SYS_STAT 5u
#define RABBITBONE_SYS_LIST 6u
#define RABBITBONE_SYS_EXIT 7u
#define RABBITBONE_SYS_LOG 8u
#define RABBITBONE_SYS_WRITE 9u
#define RABBITBONE_SYS_SEEK 10u
#define RABBITBONE_SYS_CREATE 11u
#define RABBITBONE_SYS_MKDIR 12u
#define RABBITBONE_SYS_UNLINK 13u
#define RABBITBONE_SYS_TICKS 14u
#define RABBITBONE_SYS_GETPID 15u
#define RABBITBONE_SYS_PROCINFO 16u
#define RABBITBONE_SYS_SPAWN 17u
#define RABBITBONE_SYS_WAIT 18u
#define RABBITBONE_SYS_YIELD 19u
#define RABBITBONE_SYS_SLEEP 20u
#define RABBITBONE_SYS_SCHEDINFO 21u
#define RABBITBONE_SYS_DUP 22u
#define RABBITBONE_SYS_TELL 23u
#define RABBITBONE_SYS_FSTAT 24u
#define RABBITBONE_SYS_FDINFO 25u
#define RABBITBONE_SYS_READDIR 26u
#define RABBITBONE_SYS_SPAWNV 27u
#define RABBITBONE_SYS_PREEMPTINFO 28u
#define RABBITBONE_SYS_FORK 29u
#define RABBITBONE_SYS_EXEC 30u
#define RABBITBONE_SYS_EXECV 31u
#define RABBITBONE_SYS_FDCTL 32u
#define RABBITBONE_SYS_EXECVE 33u
#define RABBITBONE_SYS_PIPE 34u
#define RABBITBONE_SYS_PIPEINFO 35u
#define RABBITBONE_SYS_DUP2 36u
#define RABBITBONE_SYS_POLL 37u
#define RABBITBONE_SYS_TTY_GETINFO 38u
#define RABBITBONE_SYS_TTY_SETMODE 39u
#define RABBITBONE_SYS_TTY_READKEY 40u
#define RABBITBONE_SYS_TRUNCATE 41u
#define RABBITBONE_SYS_RENAME 42u
#define RABBITBONE_SYS_SYNC 43u
#define RABBITBONE_SYS_FSYNC 44u
#define RABBITBONE_SYS_STATVFS 45u
#define RABBITBONE_SYS_INSTALL_COMMIT 46u
#define RABBITBONE_SYS_PREALLOCATE 47u
#define RABBITBONE_SYS_FTRUNCATE 48u
#define RABBITBONE_SYS_FPREALLOCATE 49u
#define RABBITBONE_SYS_CHDIR 50u
#define RABBITBONE_SYS_GETCWD 51u
#define RABBITBONE_SYS_FDATASYNC 52u
#define RABBITBONE_SYS_SYMLINK 53u
#define RABBITBONE_SYS_READLINK 54u
#define RABBITBONE_SYS_LINK 55u
#define RABBITBONE_SYS_LSTAT 56u
#define RABBITBONE_SYS_THEME 57u
#define RABBITBONE_SYS_CRED 58u
#define RABBITBONE_SYS_SUDO 59u
#define RABBITBONE_SYS_CHMOD 60u
#define RABBITBONE_SYS_CHOWN 61u
#define RABBITBONE_SYS_KCTL 62u
#define RABBITBONE_SYS_TTY_SCROLL 63u
#define RABBITBONE_SYS_TTY_SETCURSOR 64u
#define RABBITBONE_SYS_TTY_CLEAR_LINE 65u
#define RABBITBONE_SYS_TTY_CLEAR 66u
#define RABBITBONE_SYS_TTY_CURSOR_VISIBLE 67u
#define RABBITBONE_SYS_BRK 68u
#define RABBITBONE_SYS_SBRK 69u
#define RABBITBONE_SYS_MMAP 70u
#define RABBITBONE_SYS_MUNMAP 71u
#define RABBITBONE_SYS_MPROTECT 72u
#define RABBITBONE_SYS_SIGNAL 73u
#define RABBITBONE_SYS_SIGACTION 74u
#define RABBITBONE_SYS_SIGPROCMASK 75u
#define RABBITBONE_SYS_SIGPENDING 76u
#define RABBITBONE_SYS_KILL 77u
#define RABBITBONE_SYS_RAISE 78u
#define RABBITBONE_SYS_GETPGRP 79u
#define RABBITBONE_SYS_SETPGID 80u
#define RABBITBONE_SYS_GETPGID 81u
#define RABBITBONE_SYS_SETSID 82u
#define RABBITBONE_SYS_GETSID 83u
#define RABBITBONE_SYS_TCGETPGRP 84u
#define RABBITBONE_SYS_TCSETPGRP 85u
#define RABBITBONE_SYS_SIGRETURN 86u
#define RABBITBONE_SYS_MAX 87u

#define RABBITBONE_NAME_MAX 64u
#define RABBITBONE_PATH_MAX 256u
#define RABBITBONE_PROCESS_NAME_MAX 48u
#define RABBITBONE_ENV_MAX 8u
#define RABBITBONE_PIPE_BUF 4096u
#define RABBITBONE_PROCESS_HANDLE_CAP 34u

#define RABBITBONE_ERR_NOENT       -1
#define RABBITBONE_ERR_NOMEM       -2
#define RABBITBONE_ERR_INVAL       -3
#define RABBITBONE_ERR_IO          -4
#define RABBITBONE_ERR_NOTDIR      -5
#define RABBITBONE_ERR_ISDIR       -6
#define RABBITBONE_ERR_EXIST       -7
#define RABBITBONE_ERR_PERM        -8
#define RABBITBONE_ERR_NOSPC       -9
#define RABBITBONE_ERR_NOTEMPTY    -10
#define RABBITBONE_ERR_UNSUPPORTED -11
#define RABBITBONE_ERR_BUSY        -12

#define RABBITBONE_THEME_LEGACY 0u
#define RABBITBONE_THEME_BLACK  1u
#define RABBITBONE_THEME_MAX    2u
#define RABBITBONE_THEME_OP_GET 0u
#define RABBITBONE_THEME_OP_SET 1u

#define RABBITBONE_UID_ROOT 0u
#define RABBITBONE_GID_ROOT 0u
#define RABBITBONE_UID_RABBITBONE 1000u
#define RABBITBONE_GID_USERS 1000u
#define RABBITBONE_UID_GUEST 1001u

#define RABBITBONE_CRED_OP_GET 0u
#define RABBITBONE_CRED_OP_LOGIN 1u
#define RABBITBONE_CRED_OP_SET_USER 2u
#define RABBITBONE_CRED_OP_SET_EUID 3u
#define RABBITBONE_CRED_OP_USERINFO 4u

#define RABBITBONE_SUDO_OP_STATUS 0u
#define RABBITBONE_SUDO_OP_VALIDATE 1u
#define RABBITBONE_SUDO_OP_DROP 2u
#define RABBITBONE_SUDO_OP_INVALIDATE 3u
#define RABBITBONE_SUDO_OP_SET_TIMEOUT 4u

#define RABBITBONE_SUDO_FLAG_ACTIVATE 0x00000001u
#define RABBITBONE_SUDO_FLAG_PERSIST  0x00000002u

#define RABBITBONE_SUDO_DEFAULT_TTL_TICKS 3000u
#define RABBITBONE_SUDO_MAX_TTL_TICKS 60000u
#define RABBITBONE_USER_NAME_MAX 32u

#define RABBITBONE_KCTL_OP_MEM 0u
#define RABBITBONE_KCTL_OP_HEAP 1u
#define RABBITBONE_KCTL_OP_VMM 2u
#define RABBITBONE_KCTL_OP_KTEST 3u
#define RABBITBONE_KCTL_OP_LOGS 4u
#define RABBITBONE_KCTL_OP_DISKS 5u
#define RABBITBONE_KCTL_OP_EXT4 6u
#define RABBITBONE_KCTL_OP_PCI 7u
#define RABBITBONE_KCTL_OP_BLK 8u
#define RABBITBONE_KCTL_OP_PANIC 9u
#define RABBITBONE_KCTL_OP_REBOOT 10u
#define RABBITBONE_KCTL_OP_HALT 11u
#define RABBITBONE_KCTL_OP_ACPI 12u
#define RABBITBONE_KCTL_OP_APIC 13u
#define RABBITBONE_KCTL_OP_HPET 14u
#define RABBITBONE_KCTL_OP_TIMER 15u
#define RABBITBONE_KCTL_OP_SMP 16u
#define RABBITBONE_KCTL_OP_SIGNALS 17u
#define RABBITBONE_KCTL_OP_JOBS 18u
#define RABBITBONE_KCTL_OP_BOOT 19u
#define RABBITBONE_KCTL_OP_MAX 20u
#define RABBITBONE_KCTL_OUT_MAX 4096u


#define RABBITBONE_STDIN  0u
#define RABBITBONE_STDOUT 1u
#define RABBITBONE_STDERR 2u

#define RABBITBONE_FD_CLOEXEC 0x00000001u

#define RABBITBONE_PROT_READ  0x00000001u
#define RABBITBONE_PROT_WRITE 0x00000002u
#define RABBITBONE_PROT_EXEC  0x00000004u
#define RABBITBONE_PROT_SUPPORTED (RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE | RABBITBONE_PROT_EXEC)

#define RABBITBONE_MAP_ANON    0x00000001u
#define RABBITBONE_MAP_PRIVATE 0x00000002u
#define RABBITBONE_MAP_FIXED   0x00000004u
#define RABBITBONE_MAP_SHARED  0x00000008u
#define RABBITBONE_MAP_SUPPORTED (RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_FIXED | RABBITBONE_MAP_SHARED)


#define RABBITBONE_NSIG 32u
#define RABBITBONE_SIG_DFL 0ull
#define RABBITBONE_SIG_IGN 1ull
#define RABBITBONE_SIG_ERR 0xffffffffffffffffull

#define RABBITBONE_SIGHUP   1u
#define RABBITBONE_SIGINT   2u
#define RABBITBONE_SIGQUIT  3u
#define RABBITBONE_SIGILL   4u
#define RABBITBONE_SIGABRT  6u
#define RABBITBONE_SIGKILL  9u
#define RABBITBONE_SIGUSR1  10u
#define RABBITBONE_SIGSEGV  11u
#define RABBITBONE_SIGUSR2  12u
#define RABBITBONE_SIGPIPE  13u
#define RABBITBONE_SIGALRM  14u
#define RABBITBONE_SIGTERM  15u
#define RABBITBONE_SIGCHLD  17u
#define RABBITBONE_SIGCONT  18u
#define RABBITBONE_SIGSTOP  19u
#define RABBITBONE_SIGTSTP  20u
#define RABBITBONE_SIGTTIN  21u
#define RABBITBONE_SIGTTOU  22u

#define RABBITBONE_SIG_BLOCK   0u
#define RABBITBONE_SIG_UNBLOCK 1u
#define RABBITBONE_SIG_SETMASK 2u

#define RABBITBONE_SA_RESTART 0x00000001u

#define RABBITBONE_PROCESS_FLAG_STOPPED 0x00000001u
#define RABBITBONE_PROCESS_FLAG_SESSION_LEADER 0x00000002u
#define RABBITBONE_PROCESS_FLAG_FOREGROUND 0x00000004u

#define RABBITBONE_O_RDONLY    0x00000000u
#define RABBITBONE_O_WRONLY    0x00000001u
#define RABBITBONE_O_RDWR      0x00000002u
#define RABBITBONE_O_ACCMODE   0x00000003u
#define RABBITBONE_O_CREAT     0x00000100u
#define RABBITBONE_O_EXCL      0x00000200u
#define RABBITBONE_O_TRUNC     0x00000400u
#define RABBITBONE_O_APPEND    0x00000800u
#define RABBITBONE_O_DIRECTORY 0x00001000u
#define RABBITBONE_O_CLOEXEC   0x00002000u
#define RABBITBONE_O_SUPPORTED (RABBITBONE_O_ACCMODE | RABBITBONE_O_CREAT | RABBITBONE_O_EXCL | RABBITBONE_O_TRUNC | RABBITBONE_O_APPEND | RABBITBONE_O_DIRECTORY | RABBITBONE_O_CLOEXEC)

#define RABBITBONE_SEEK_SET 0u
#define RABBITBONE_SEEK_CUR 1u
#define RABBITBONE_SEEK_END 2u
#define RABBITBONE_FDCTL_GET 0u
#define RABBITBONE_FDCTL_SET 1u

#define RABBITBONE_POLL_READ  0x00000001u
#define RABBITBONE_POLL_WRITE 0x00000002u
#define RABBITBONE_POLL_HUP   0x00000004u
#define RABBITBONE_POLL_BADF  0x00000008u

#define RABBITBONE_TTY_MODE_RAW   0x00000001u
#define RABBITBONE_TTY_MODE_ECHO  0x00000002u
#define RABBITBONE_TTY_MODE_CANON 0x00000004u

#define RABBITBONE_TTY_READ_NONBLOCK 0x00000001u

#define RABBITBONE_KEY_NONE      0u
#define RABBITBONE_KEY_CHAR      1u
#define RABBITBONE_KEY_ENTER     2u
#define RABBITBONE_KEY_BACKSPACE 3u
#define RABBITBONE_KEY_TAB       4u
#define RABBITBONE_KEY_ESC       5u
#define RABBITBONE_KEY_UP        100u
#define RABBITBONE_KEY_DOWN      101u
#define RABBITBONE_KEY_LEFT      102u
#define RABBITBONE_KEY_RIGHT     103u
#define RABBITBONE_KEY_DELETE    104u
#define RABBITBONE_KEY_HOME      105u
#define RABBITBONE_KEY_END       106u
#define RABBITBONE_KEY_PAGEUP    107u
#define RABBITBONE_KEY_PAGEDOWN  108u

#define RABBITBONE_KEY_MOD_SHIFT 0x00000001u
#define RABBITBONE_KEY_MOD_CTRL  0x00000002u
#define RABBITBONE_KEY_MOD_ALT   0x00000004u

#define RABBITBONE_VFS_FLAG_READONLY 0x00000001u
#define RABBITBONE_VFS_FLAG_PERSISTENT 0x00000002u
#define RABBITBONE_VFS_FLAG_JOURNALED 0x00000004u
#define RABBITBONE_VFS_FLAG_REPAIRABLE 0x00000008u


#ifndef RABBITBONE_ABI_STATIC_ASSERT
#define RABBITBONE_ABI_STATIC_ASSERT(name, expr) typedef char rabbitbone_abi_static_assert_##name[(expr) ? 1 : -1]
#endif


typedef struct rabbitbone_credinfo {
    unsigned int uid;
    unsigned int euid;
    unsigned int gid;
    unsigned int egid;
    unsigned int is_admin;
    unsigned int sudo_cached;
    unsigned int sudo_persistent;
    unsigned int umask;
    unsigned long long sudo_until;
    unsigned long long sudo_ttl;
    char user[RABBITBONE_USER_NAME_MAX];
} rabbitbone_credinfo_t;

typedef struct rabbitbone_userinfo {
    unsigned int uid;
    unsigned int gid;
    unsigned int is_admin;
    char user[RABBITBONE_USER_NAME_MAX];
} rabbitbone_userinfo_t;

typedef struct rabbitbone_ttyinfo {
    unsigned int rows;
    unsigned int cols;
    unsigned int cursor_row;
    unsigned int cursor_col;
    unsigned int mode;
    unsigned int pending_keys;
    unsigned int reserved0;
    unsigned int reserved1;
} rabbitbone_ttyinfo_t;

typedef struct rabbitbone_key_event {
    unsigned int code;
    unsigned int mods;
    unsigned int ch;
    unsigned int scancode;
} rabbitbone_key_event_t;

typedef struct rabbitbone_procinfo {
    unsigned int pid;
    unsigned int ppid;
    unsigned int pgrp;
    unsigned int sid;
    unsigned int state;
    unsigned int uid;
    unsigned int euid;
    unsigned int gid;
    unsigned int egid;
    unsigned int flags;
    unsigned int pending_signals;
    unsigned int blocked_signals;
    int exit_code;
    int status;
    unsigned long long started_ticks;
    unsigned long long finished_ticks;
    unsigned long long entry;
    unsigned long long user_stack_top;
    unsigned long long mapped_pages;
    unsigned long long address_space;
    unsigned long long address_space_generation;
    unsigned char faulted;
    unsigned char pad[7];
    unsigned long long fault_vector;
    unsigned long long fault_rip;
    unsigned long long fault_addr;
    char name[RABBITBONE_PROCESS_NAME_MAX];
} rabbitbone_procinfo_t;

typedef struct rabbitbone_schedinfo {
    unsigned int queue_capacity;
    unsigned int queued;
    unsigned int running;
    unsigned int completed;
    unsigned int failed;
    unsigned int next_job_id;
    unsigned int total_enqueued;
    unsigned int total_dispatched;
    unsigned int total_yields;
    unsigned int total_sleeps;
    unsigned long long last_dispatch_ticks;
    unsigned int preempt_enabled;
    unsigned int quantum_ticks;
    unsigned int current_slice_ticks;
    unsigned int reserved;
    unsigned long long total_timer_ticks;
    unsigned long long user_ticks;
    unsigned long long kernel_ticks;
    unsigned long long total_preemptions;
} rabbitbone_schedinfo_t;

typedef struct rabbitbone_fdinfo {
    unsigned int handle;
    unsigned int type;
    unsigned long long offset;
    unsigned long long size;
    unsigned int inode;
    unsigned int fs_id;
    unsigned int flags;
    unsigned int open_flags;
    char path[RABBITBONE_PATH_MAX];
} rabbitbone_fdinfo_t;

typedef struct rabbitbone_dirent {
    char name[RABBITBONE_NAME_MAX];
    unsigned int type;
    unsigned int reserved;
    unsigned long long size;
    unsigned int inode;
    unsigned int fs_id;
} rabbitbone_dirent_t;



typedef struct rabbitbone_statvfs {
    unsigned long long block_size;
    unsigned long long total_blocks;
    unsigned long long free_blocks;
    unsigned long long avail_blocks;
    unsigned long long total_inodes;
    unsigned long long free_inodes;
    unsigned long long fs_id;
    unsigned int flags;
    unsigned int max_name_len;
    char mount_path[RABBITBONE_PATH_MAX];
    char fs_name[RABBITBONE_NAME_MAX];
} rabbitbone_statvfs_t;

typedef struct rabbitbone_pipeinfo {
    unsigned int pipe_id;
    unsigned int read_handle;
    unsigned int write_handle;
    unsigned int capacity;
    unsigned long long bytes_available;
    unsigned long long total_read;
    unsigned long long total_written;
    unsigned int endpoint;
    unsigned int read_refs;
    unsigned int write_refs;
    unsigned int reserved;
} rabbitbone_pipeinfo_t;

typedef struct rabbitbone_sigaction {
    unsigned long long handler;
    unsigned long long mask;
    unsigned int flags;
    unsigned int reserved;
    unsigned long long restorer;
} rabbitbone_sigaction_t;

typedef struct rabbitbone_jobinfo {
    unsigned int pid;
    unsigned int ppid;
    unsigned int pgrp;
    unsigned int sid;
    unsigned int state;
    unsigned int flags;
    unsigned int pending_signals;
    unsigned int blocked_signals;
    char name[RABBITBONE_PROCESS_NAME_MAX];
} rabbitbone_jobinfo_t;

typedef struct rabbitbone_preemptinfo {
    unsigned int enabled;
    unsigned int quantum_ticks;
    unsigned int current_pid;
    unsigned int current_slice_ticks;
    unsigned long long total_timer_ticks;
    unsigned long long user_ticks;
    unsigned long long kernel_ticks;
    unsigned long long total_preemptions;
    unsigned long long last_preempt_ticks;
    unsigned long long last_preempt_rip;
} rabbitbone_preemptinfo_t;

RABBITBONE_ABI_STATIC_ASSERT(procinfo_pid_offset, __builtin_offsetof(rabbitbone_procinfo_t, pid) == 0);
RABBITBONE_ABI_STATIC_ASSERT(procinfo_name_size, sizeof(((rabbitbone_procinfo_t *)0)->name) == RABBITBONE_PROCESS_NAME_MAX);
RABBITBONE_ABI_STATIC_ASSERT(sigaction_size, sizeof(rabbitbone_sigaction_t) == 32);
RABBITBONE_ABI_STATIC_ASSERT(schedinfo_quantum_after_preempt_enabled, __builtin_offsetof(rabbitbone_schedinfo_t, quantum_ticks) == __builtin_offsetof(rabbitbone_schedinfo_t, preempt_enabled) + sizeof(unsigned int));
RABBITBONE_ABI_STATIC_ASSERT(fdinfo_flags_after_fsid, __builtin_offsetof(rabbitbone_fdinfo_t, flags) == __builtin_offsetof(rabbitbone_fdinfo_t, fs_id) + sizeof(unsigned int));
RABBITBONE_ABI_STATIC_ASSERT(fdinfo_open_flags_after_flags, __builtin_offsetof(rabbitbone_fdinfo_t, open_flags) == __builtin_offsetof(rabbitbone_fdinfo_t, flags) + sizeof(unsigned int));
RABBITBONE_ABI_STATIC_ASSERT(preemptinfo_rip_offset, __builtin_offsetof(rabbitbone_preemptinfo_t, last_preempt_rip) > __builtin_offsetof(rabbitbone_preemptinfo_t, total_preemptions));
RABBITBONE_ABI_STATIC_ASSERT(pipeinfo_capacity_offset, __builtin_offsetof(rabbitbone_pipeinfo_t, capacity) == 12);
RABBITBONE_ABI_STATIC_ASSERT(ttyinfo_mode_offset, __builtin_offsetof(rabbitbone_ttyinfo_t, mode) == 16);
RABBITBONE_ABI_STATIC_ASSERT(key_event_size, sizeof(rabbitbone_key_event_t) == 16);
RABBITBONE_ABI_STATIC_ASSERT(statvfs_block_size_offset, __builtin_offsetof(rabbitbone_statvfs_t, block_size) == 0);
RABBITBONE_ABI_STATIC_ASSERT(statvfs_mount_after_max_name, __builtin_offsetof(rabbitbone_statvfs_t, mount_path) == __builtin_offsetof(rabbitbone_statvfs_t, max_name_len) + sizeof(unsigned int));

#endif
