#pragma once

#include <rabbitbone/process.h>
#include <rabbitbone/elf64.h>
#include <rabbitbone/vfs.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/console.h>
#include <rabbitbone/kmem.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/arch/gdt.h>
#include <rabbitbone/syscall.h>
#include <rabbitbone/scheduler.h>
#include <rabbitbone/task.h>
#include <rabbitbone/timer.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/rust.h>
#include <rabbitbone/panic.h>
#include <rabbitbone/path.h>
#include <rabbitbone/tty.h>

#define USER_IMAGE_BASE      ELF64_RABBITBONE_USER_IMAGE_BASE
#define USER_SPACE_LIMIT     ELF64_RABBITBONE_USER_SPACE_LIMIT
#define USER_STACK_TOP       0x0000010100000000ull
#define USER_STACK_PAGES     8u
#define USER_STACK_GUARD_PAGES 1u
#define USER_STACK_ASLR_PAGES 16u
#define USER_HEAP_MAX_PAGES  128u
#define USER_HEAP_GAP_PAGES  1u
#define USER_MAX_MAPPINGS    256u
#define USER_MAX_VMAS        32u
#define USER_MMAP_BASE       0x0000010080000000ull
#define USER_MMAP_MAX_PAGES  64u
#define USER_ARG_STRING_MAX  128u
#define USER_BACKING_PHYS_LIMIT MEMORY_KERNEL_DIRECT_LIMIT
#define PROCESS_ASYNC_CAP 32u
#define PROCESS_INT80_LEN 2u
#define PROCESS_SHEBANG_LINE_MAX 127u
#define PROCESS_SHEBANG_MAX_DEPTH 2u
#define USER_MMAP_FILE_BACKING_NONE 0xffffffffu
#define USER_SHARED_ANON_INVALID 0xffffffffu
#define USER_SHARED_ANON_OBJECTS 32u
#define PROCESS_SIGNAL_FRAME_MAGIC 0x5349474652414d45ull

typedef struct user_shared_anon_object {
    bool used;
    u32 refcount;
    u32 page_count;
    u32 reserved;
    u64 generation;
    uptr pages[USER_MMAP_MAX_PAGES];
} user_shared_anon_object_t;

typedef enum user_vma_kind {
    USER_VMA_IMAGE = 1,
    USER_VMA_STACK = 2,
    USER_VMA_HEAP = 3,
    USER_VMA_MMAP = 4,
} user_vma_kind_t;

typedef struct user_vma {
    uptr start;
    uptr end;
    u32 prot;
    u32 flags;
    u32 kind;
    u32 reserved;
    u64 file_offset;
    u64 file_size;
    u32 file_id;
    u32 file_reserved;
    vfs_node_ref_t file_ref;
} user_vma_t;

typedef struct user_mapping {
    uptr virt;
    uptr phys;
    u64 final_flags;
} user_mapping_t;

typedef struct process_exec_plan {
    char image_path[VFS_PATH_MAX];
    char argv_storage[PROCESS_ARG_MAX][USER_ARG_STRING_MAX];
    const char *argv[PROCESS_ARG_MAX];
    int argc;
    bool shebang;
} process_exec_plan_t;

typedef struct process_signal_action {
    uptr handler;
    u64 mask;
    u32 flags;
    u32 reserved;
    uptr restorer;
} process_signal_action_t;

typedef struct process_signal_frame {
    u64 restorer;
    u64 magic;
    u64 old_mask;
    u32 signal;
    u32 reserved;
    cpu_regs_t saved_regs;
} process_signal_frame_t;

typedef struct active_process {
    bool active;
    bool returned;
    process_lifecycle_t state;
    u32 parent_pid;
    u32 process_group;
    u32 session_id;
    u32 signal_flags;
    u32 wait_pid;
    uptr wait_out_ptr;
    u64 wake_tick;
    cpu_regs_t regs;
    process_result_t result;
    vmm_space_t space;
    user_mapping_t mappings[USER_MAX_MAPPINGS];
    usize mapping_count;
    user_vma_t vmas[USER_MAX_VMAS];
    usize vma_count;
    uptr mmap_cursor;
    uptr heap_base;
    uptr heap_break;
    uptr heap_limit;
    u8 fd_snapshot[SYSCALL_USER_HANDLE_SNAPSHOT_BYTES];
    char cwd[VFS_PATH_MAX];
    rabbitbone_credinfo_t cred;
    u64 signal_pending;
    u64 signal_blocked;
    bool signal_in_handler;
    process_signal_action_t signal_actions[RABBITBONE_NSIG];
} active_process_t;

extern void arch_user_enter(u64 entry, u64 user_rsp, u64 argc, u64 argv, u64 aux);
extern void arch_user_resume(const cpu_regs_t *regs);
extern void arch_user_return_from_interrupt(void) RABBITBONE_NORETURN;

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
static user_shared_anon_object_t *shared_anon_objects;
static u64 next_shared_anon_generation = 1;
static u32 tty_foreground_pgrp;
static u32 tty_session_id;

static void release_mappings(active_process_t *p);
static user_mapping_t *find_mapping(active_process_t *p, uptr virt);
static process_status_t track_mapping(active_process_t *p, uptr virt, uptr phys, u64 final_flags);
static void release_mapping_at(active_process_t *p, usize idx);
static bool process_resolve_cow_page(active_process_t *p, uptr fault_addr);
static bool process_resolve_vma_fault(active_process_t *p, uptr fault_addr, bool write, bool instruction_fetch);
static bool ensure_process_user_page_writable(active_process_t *p, uptr addr, uptr *phys_out, u64 *flags_out);
static process_status_t process_set_brk_for(active_process_t *p, uptr new_break, uptr *current_out);
static bool process_range_has_vma(const active_process_t *p, uptr start, uptr end, u32 kind);
static process_status_t process_add_vma(active_process_t *p, uptr start, uptr end, u32 prot, u32 flags, u32 kind);
static process_status_t process_add_vma_backed(active_process_t *p, uptr start, uptr end, u32 prot, u32 flags, u32 kind, u64 file_offset, u64 file_size, u32 file_id, const vfs_node_ref_t *file_ref);
static process_status_t process_split_vmas_for_range(active_process_t *p, uptr start, uptr end);
static const user_vma_t *process_find_vma_for_page_const(const active_process_t *p, uptr page_virt);
static bool process_vma_is_shared_anon(const user_vma_t *v);
static bool process_mapping_is_shared_anon(const active_process_t *p, uptr page_virt);
static void process_signal_init(active_process_t *p);
static int process_signal_deliver_pending(active_process_t *p, cpu_regs_t *regs);
static process_status_t shared_anon_alloc(u32 page_count, u32 *id_out);
static bool shared_anon_retain(u32 id);
static void shared_anon_release(u32 id);

