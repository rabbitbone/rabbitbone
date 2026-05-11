#ifndef AURORA_PROCESS_H
#define AURORA_PROCESS_H
#include <aurora/types.h>
#include <aurora/arch/cpu.h>
#include <aurora/abi.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define PROCESS_ARG_MAX 8u
#define PROCESS_ENV_MAX AURORA_ENV_MAX
#define PROCESS_NAME_MAX AURORA_PROCESS_NAME_MAX
#define PROCESS_TABLE_CAP 64u

typedef enum process_status {
    PROC_OK = 0,
    PROC_ERR_INVAL = -1,
    PROC_ERR_IO = -2,
    PROC_ERR_FORMAT = -3,
    PROC_ERR_NOMEM = -4,
    PROC_ERR_RANGE = -5,
    PROC_ERR_FAULT = -6,
    PROC_ERR_BUSY = -7,
} process_status_t;

typedef enum process_lifecycle {
    PROCESS_STATE_EMPTY = 0,
    PROCESS_STATE_RUNNING = 1,
    PROCESS_STATE_EXITED = 2,
    PROCESS_STATE_FAULTED = 3,
    PROCESS_STATE_LOAD_ERROR = 4,
    PROCESS_STATE_READY = 5,
    PROCESS_STATE_SLEEPING = 6,
    PROCESS_STATE_WAITING = 7,
} process_lifecycle_t;

typedef struct process_result {
    i32 exit_code;
    u32 pid;
    u64 entry;
    u64 user_stack_top;
    u64 mapped_pages;
    u64 address_space;
    u64 address_space_generation;
    u64 started_ticks;
    u64 finished_ticks;
    bool faulted;
    u64 fault_vector;
    u64 fault_rip;
    u64 fault_addr;
    char name[PROCESS_NAME_MAX];
} process_result_t;

typedef aurora_procinfo_t process_info_t;

void process_init(void);
process_status_t process_exec(const char *path, int argc, const char *const *argv, process_result_t *out);
process_status_t process_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp, process_result_t *out);
process_status_t process_spawn(const char *path, int argc, const char *const *argv, u32 *pid_out, process_result_t *out);
bool process_wait(u32 pid, process_info_t *out);
process_status_t process_spawn_async(const char *path, int argc, const char *const *argv, u32 *pid_out);

process_status_t process_spawn_async_snapshot(const char *path, int argc, const char *const *argv, void *snapshot, usize snapshot_size, u32 *pid_out);
bool process_run_until_idle(u32 root_pid, process_result_t *root_out);
bool process_async_scheduler_active(void);
bool process_after_syscall(cpu_regs_t *regs);
AURORA_NORETURN void process_exit_current_from_syscall(cpu_regs_t *regs, i32 code);
void process_preempt_from_interrupt(cpu_regs_t *regs);
void process_fault_current_from_interrupt(cpu_regs_t *regs, u64 vector, u64 cr2);
void process_request_reschedule(void);
void process_request_exit(i32 code);
void process_request_sleep(u64 ticks);
bool process_request_wait(u32 pid, uptr out_ptr);
bool process_request_fork(void);
process_status_t process_request_exec(const char *path, int argc, const char *const *argv);
process_status_t process_request_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp);
const char *process_status_name(process_status_t st);
bool process_selftest(void);
bool process_user_active(void);
void process_dump_last(void);
void process_dump_table(void);
u32 process_current_pid(void);
bool process_current_info(process_info_t *out);
bool process_lookup(u32 pid, process_info_t *out);
usize process_table_count(void);
bool process_table_selftest(void);

bool process_copy_from_user(void *dst, uptr user, usize size);
bool process_copy_to_user(uptr user, const void *src, usize size);
bool process_copy_string_from_user(uptr user, char *out, usize out_size);
bool process_validate_user_range(uptr user, usize size, bool write);

AURORA_NORETURN void process_exit_from_interrupt(i32 code);
AURORA_NORETURN void process_fault_from_interrupt(u64 vector, u64 rip, u64 cr2);

#if defined(__cplusplus)
}
#endif
#endif
