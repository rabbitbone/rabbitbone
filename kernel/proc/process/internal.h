#pragma once

#include <aurora/process.h>
#include <aurora/elf64.h>
#include <aurora/vfs.h>
#include <aurora/vmm.h>
#include <aurora/memory.h>
#include <aurora/console.h>
#include <aurora/kmem.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/arch/io.h>
#include <aurora/syscall.h>
#include <aurora/scheduler.h>
#include <aurora/task.h>
#include <aurora/timer.h>
#include <aurora/drivers.h>
#include <aurora/rust.h>
#include <aurora/panic.h>

#define USER_IMAGE_BASE      ELF64_AURORA_USER_IMAGE_BASE
#define USER_SPACE_LIMIT     ELF64_AURORA_USER_SPACE_LIMIT
#define USER_STACK_TOP       0x0000010100000000ull
#define USER_STACK_PAGES     8u
#define USER_STACK_GUARD_PAGES 1u
#define USER_STACK_ASLR_PAGES 16u
#define USER_MAX_MAPPINGS    256u
#define USER_ARG_STRING_MAX  128u
#define USER_BACKING_PHYS_LIMIT MEMORY_KERNEL_DIRECT_LIMIT
#define PROCESS_ASYNC_CAP 32u
#define PROCESS_INT80_LEN 2u

typedef struct user_mapping {
    uptr virt;
    uptr phys;
    u64 final_flags;
} user_mapping_t;

typedef struct active_process {
    bool active;
    bool returned;
    process_lifecycle_t state;
    u32 parent_pid;
    u32 wait_pid;
    uptr wait_out_ptr;
    u64 wake_tick;
    cpu_regs_t regs;
    process_result_t result;
    vmm_space_t space;
    user_mapping_t mappings[USER_MAX_MAPPINGS];
    usize mapping_count;
    u8 fd_snapshot[SYSCALL_USER_HANDLE_SNAPSHOT_BYTES];
    char cwd[VFS_PATH_MAX];
    aurora_credinfo_t cred;
} active_process_t;

extern void arch_user_enter(u64 entry, u64 user_rsp, u64 argc, u64 argv, u64 aux);
extern void arch_user_resume(const cpu_regs_t *regs);
extern void arch_user_return_from_interrupt(void) AURORA_NORETURN;

extern u64 arch_user_saved_rsp;
extern u64 arch_user_saved_rbp;
extern u64 arch_user_saved_rbx;
extern u64 arch_user_saved_r12;
extern u64 arch_user_saved_r13;
extern u64 arch_user_saved_r14;
extern u64 arch_user_saved_r15;

typedef struct arch_user_return_context {
    u64 rsp;
    u64 rbp;
    u64 rbx;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;
} arch_user_return_context_t;

static void arch_user_context_save(arch_user_return_context_t *ctx) {
    if (!ctx) return;
    ctx->rsp = arch_user_saved_rsp;
    ctx->rbp = arch_user_saved_rbp;
    ctx->rbx = arch_user_saved_rbx;
    ctx->r12 = arch_user_saved_r12;
    ctx->r13 = arch_user_saved_r13;
    ctx->r14 = arch_user_saved_r14;
    ctx->r15 = arch_user_saved_r15;
}

static void arch_user_context_restore(const arch_user_return_context_t *ctx) {
    if (!ctx) return;
    arch_user_saved_rsp = ctx->rsp;
    arch_user_saved_rbp = ctx->rbp;
    arch_user_saved_rbx = ctx->rbx;
    arch_user_saved_r12 = ctx->r12;
    arch_user_saved_r13 = ctx->r13;
    arch_user_saved_r14 = ctx->r14;
    arch_user_saved_r15 = ctx->r15;
}

static active_process_t *current_proc_ptr;
#define current_proc (*current_proc_ptr)
static active_process_t *async_slots;
static u32 async_next_rr;
static i32 current_async_slot = -1;
static bool async_scheduler_active;
static bool reschedule_requested;
static bool exit_requested;
static bool fork_requested;
static bool exec_requested;
static active_process_t *exec_replacement;
static active_process_t *exec_old_proc;
static i32 requested_exit_code;
static u64 next_user_pid = 1;
static u64 next_address_space_generation = 1;
static process_result_t last_result;
static process_info_t *process_table;
static usize process_table_len;
static usize process_table_next;
static bool process_initialized;

static void release_mappings(active_process_t *p);

