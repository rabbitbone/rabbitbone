#ifndef AURORA_ABI_SHARED_H
#define AURORA_ABI_SHARED_H

#define AURORA_SYS_VERSION 0u
#define AURORA_SYS_WRITE_CONSOLE 1u
#define AURORA_SYS_OPEN 2u
#define AURORA_SYS_CLOSE 3u
#define AURORA_SYS_READ 4u
#define AURORA_SYS_STAT 5u
#define AURORA_SYS_LIST 6u
#define AURORA_SYS_EXIT 7u
#define AURORA_SYS_LOG 8u
#define AURORA_SYS_WRITE 9u
#define AURORA_SYS_SEEK 10u
#define AURORA_SYS_CREATE 11u
#define AURORA_SYS_MKDIR 12u
#define AURORA_SYS_UNLINK 13u
#define AURORA_SYS_TICKS 14u
#define AURORA_SYS_GETPID 15u
#define AURORA_SYS_PROCINFO 16u
#define AURORA_SYS_SPAWN 17u
#define AURORA_SYS_WAIT 18u
#define AURORA_SYS_YIELD 19u
#define AURORA_SYS_SLEEP 20u
#define AURORA_SYS_SCHEDINFO 21u
#define AURORA_SYS_DUP 22u
#define AURORA_SYS_TELL 23u
#define AURORA_SYS_FSTAT 24u
#define AURORA_SYS_FDINFO 25u
#define AURORA_SYS_READDIR 26u
#define AURORA_SYS_SPAWNV 27u
#define AURORA_SYS_PREEMPTINFO 28u
#define AURORA_SYS_FORK 29u
#define AURORA_SYS_EXEC 30u
#define AURORA_SYS_EXECV 31u
#define AURORA_SYS_FDCTL 32u
#define AURORA_SYS_EXECVE 33u
#define AURORA_SYS_PIPE 34u
#define AURORA_SYS_PIPEINFO 35u
#define AURORA_SYS_DUP2 36u
#define AURORA_SYS_POLL 37u
#define AURORA_SYS_TTY_GETINFO 38u
#define AURORA_SYS_TTY_SETMODE 39u
#define AURORA_SYS_TTY_READKEY 40u
#define AURORA_SYS_TRUNCATE 41u
#define AURORA_SYS_RENAME 42u
#define AURORA_SYS_SYNC 43u
#define AURORA_SYS_FSYNC 44u
#define AURORA_SYS_STATVFS 45u
#define AURORA_SYS_MAX 46u

#define AURORA_NAME_MAX 64u
#define AURORA_PATH_MAX 256u
#define AURORA_PROCESS_NAME_MAX 48u
#define AURORA_ENV_MAX 8u
#define AURORA_PIPE_BUF 4096u
#define AURORA_PROCESS_HANDLE_CAP 34u

#define AURORA_STDIN  0u
#define AURORA_STDOUT 1u
#define AURORA_STDERR 2u

#define AURORA_FD_CLOEXEC 0x00000001u
#define AURORA_FDCTL_GET 0u
#define AURORA_FDCTL_SET 1u

#define AURORA_POLL_READ  0x00000001u
#define AURORA_POLL_WRITE 0x00000002u
#define AURORA_POLL_HUP   0x00000004u
#define AURORA_POLL_BADF  0x00000008u

#define AURORA_TTY_MODE_RAW   0x00000001u
#define AURORA_TTY_MODE_ECHO  0x00000002u
#define AURORA_TTY_MODE_CANON 0x00000004u

#define AURORA_TTY_READ_NONBLOCK 0x00000001u

#define AURORA_KEY_NONE      0u
#define AURORA_KEY_CHAR      1u
#define AURORA_KEY_ENTER     2u
#define AURORA_KEY_BACKSPACE 3u
#define AURORA_KEY_TAB       4u
#define AURORA_KEY_ESC       5u
#define AURORA_KEY_UP        100u
#define AURORA_KEY_DOWN      101u
#define AURORA_KEY_LEFT      102u
#define AURORA_KEY_RIGHT     103u
#define AURORA_KEY_DELETE    104u
#define AURORA_KEY_HOME      105u
#define AURORA_KEY_END       106u
#define AURORA_KEY_PAGEUP    107u
#define AURORA_KEY_PAGEDOWN  108u

#define AURORA_KEY_MOD_SHIFT 0x00000001u
#define AURORA_KEY_MOD_CTRL  0x00000002u
#define AURORA_KEY_MOD_ALT   0x00000004u

#define AURORA_VFS_FLAG_READONLY 0x00000001u
#define AURORA_VFS_FLAG_PERSISTENT 0x00000002u
#define AURORA_VFS_FLAG_JOURNALED 0x00000004u
#define AURORA_VFS_FLAG_REPAIRABLE 0x00000008u


#ifndef AURORA_ABI_STATIC_ASSERT
#define AURORA_ABI_STATIC_ASSERT(name, expr) typedef char aurora_abi_static_assert_##name[(expr) ? 1 : -1]
#endif


typedef struct aurora_ttyinfo {
    unsigned int rows;
    unsigned int cols;
    unsigned int cursor_row;
    unsigned int cursor_col;
    unsigned int mode;
    unsigned int pending_keys;
    unsigned int reserved0;
    unsigned int reserved1;
} aurora_ttyinfo_t;

typedef struct aurora_key_event {
    unsigned int code;
    unsigned int mods;
    unsigned int ch;
    unsigned int scancode;
} aurora_key_event_t;

typedef struct aurora_procinfo {
    unsigned int pid;
    unsigned int state;
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
    char name[AURORA_PROCESS_NAME_MAX];
} aurora_procinfo_t;

typedef struct aurora_schedinfo {
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
} aurora_schedinfo_t;

typedef struct aurora_fdinfo {
    unsigned int handle;
    unsigned int type;
    unsigned long long offset;
    unsigned long long size;
    unsigned int inode;
    unsigned int fs_id;
    unsigned int flags;
    unsigned int reserved;
    char path[AURORA_PATH_MAX];
} aurora_fdinfo_t;

typedef struct aurora_dirent {
    char name[AURORA_NAME_MAX];
    unsigned int type;
    unsigned int reserved;
    unsigned long long size;
    unsigned int inode;
    unsigned int fs_id;
} aurora_dirent_t;



typedef struct aurora_statvfs {
    unsigned long long block_size;
    unsigned long long total_blocks;
    unsigned long long free_blocks;
    unsigned long long avail_blocks;
    unsigned long long total_inodes;
    unsigned long long free_inodes;
    unsigned long long fs_id;
    unsigned int flags;
    unsigned int max_name_len;
    char mount_path[AURORA_PATH_MAX];
    char fs_name[AURORA_NAME_MAX];
} aurora_statvfs_t;

typedef struct aurora_pipeinfo {
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
} aurora_pipeinfo_t;

typedef struct aurora_preemptinfo {
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
} aurora_preemptinfo_t;

AURORA_ABI_STATIC_ASSERT(procinfo_pid_offset, __builtin_offsetof(aurora_procinfo_t, pid) == 0);
AURORA_ABI_STATIC_ASSERT(procinfo_name_size, sizeof(((aurora_procinfo_t *)0)->name) == AURORA_PROCESS_NAME_MAX);
AURORA_ABI_STATIC_ASSERT(schedinfo_quantum_after_preempt_enabled, __builtin_offsetof(aurora_schedinfo_t, quantum_ticks) == __builtin_offsetof(aurora_schedinfo_t, preempt_enabled) + sizeof(unsigned int));
AURORA_ABI_STATIC_ASSERT(fdinfo_flags_after_fsid, __builtin_offsetof(aurora_fdinfo_t, flags) == __builtin_offsetof(aurora_fdinfo_t, fs_id) + sizeof(unsigned int));
AURORA_ABI_STATIC_ASSERT(preemptinfo_rip_offset, __builtin_offsetof(aurora_preemptinfo_t, last_preempt_rip) > __builtin_offsetof(aurora_preemptinfo_t, total_preemptions));
AURORA_ABI_STATIC_ASSERT(pipeinfo_capacity_offset, __builtin_offsetof(aurora_pipeinfo_t, capacity) == 12);
AURORA_ABI_STATIC_ASSERT(ttyinfo_mode_offset, __builtin_offsetof(aurora_ttyinfo_t, mode) == 16);
AURORA_ABI_STATIC_ASSERT(key_event_size, sizeof(aurora_key_event_t) == 16);
AURORA_ABI_STATIC_ASSERT(statvfs_block_size_offset, __builtin_offsetof(aurora_statvfs_t, block_size) == 0);
AURORA_ABI_STATIC_ASSERT(statvfs_mount_after_max_name, __builtin_offsetof(aurora_statvfs_t, mount_path) == __builtin_offsetof(aurora_statvfs_t, max_name_len) + sizeof(unsigned int));

#endif
