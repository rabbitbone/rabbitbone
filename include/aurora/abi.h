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
#define AURORA_SYS_MAX 32u

#define AURORA_NAME_MAX 64u
#define AURORA_PATH_MAX 256u
#define AURORA_PROCESS_NAME_MAX 48u

#ifndef AURORA_ABI_STATIC_ASSERT
#define AURORA_ABI_STATIC_ASSERT(name, expr) typedef char aurora_abi_static_assert_##name[(expr) ? 1 : -1]
#endif

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
AURORA_ABI_STATIC_ASSERT(preemptinfo_rip_offset, __builtin_offsetof(aurora_preemptinfo_t, last_preempt_rip) > __builtin_offsetof(aurora_preemptinfo_t, total_preemptions));

#endif
