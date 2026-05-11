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
#include <aurora/timer.h>
#include <aurora/drivers.h>
#include <aurora/rust.h>
#include <aurora/panic.h>

#define USER_IMAGE_BASE      0x0000010000000000ull
#define USER_SPACE_LIMIT     0x0000800000000000ull
#define USER_STACK_TOP       0x0000010100000000ull
#define USER_STACK_PAGES     8u
#define USER_MAX_MAPPINGS    256u
#define USER_ARG_STRING_MAX  128u
#define USER_BACKING_PHYS_LIMIT MEMORY_KERNEL_DIRECT_LIMIT
#define PROCESS_ASYNC_CAP 16u
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
} active_process_t;

extern void arch_user_enter(u64 entry, u64 user_rsp, u64 argc, u64 argv, u64 aux);
extern void arch_user_resume(const cpu_regs_t *regs);
extern void arch_user_return_from_interrupt(void) AURORA_NORETURN;

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
static u32 next_user_pid = 1000;
static u64 next_address_space_generation = 1;
static process_result_t last_result;
static process_info_t *process_table;
static usize process_table_len;
static usize process_table_next;
static bool process_initialized;

void process_init(void) {
    if (process_initialized) {
        KLOG(LOG_WARN, "process", "process_init ignored after initialization");
        return;
    }
    process_initialized = true;
    if (!current_proc_ptr) current_proc_ptr = (active_process_t *)kmalloc(sizeof(active_process_t));
    if (!current_proc_ptr) PANIC("process current allocation failed");
    memset(current_proc_ptr, 0, sizeof(*current_proc_ptr));
    memset(&last_result, 0, sizeof(last_result));
    if (!process_table) process_table = (process_info_t *)kmalloc(sizeof(process_info_t) * PROCESS_TABLE_CAP);
    if (process_table) memset(process_table, 0, sizeof(process_info_t) * PROCESS_TABLE_CAP);
    if (!async_slots) async_slots = (active_process_t *)kmalloc(sizeof(active_process_t) * PROCESS_ASYNC_CAP);
    if (async_slots) memset(async_slots, 0, sizeof(active_process_t) * PROCESS_ASYNC_CAP);
    async_next_rr = 0;
    current_async_slot = -1;
    async_scheduler_active = false;
    reschedule_requested = false;
    exit_requested = false;
    fork_requested = false;
    exec_requested = false;
    if (!exec_replacement) exec_replacement = (active_process_t *)kmalloc(sizeof(active_process_t));
    if (!exec_replacement) PANIC("process exec replacement allocation failed");
    memset(exec_replacement, 0, sizeof(*exec_replacement));
    if (!exec_old_proc) exec_old_proc = (active_process_t *)kmalloc(sizeof(active_process_t));
    if (!exec_old_proc) PANIC("process exec old-state allocation failed");
    memset(exec_old_proc, 0, sizeof(*exec_old_proc));
    requested_exit_code = 0;
    process_table_len = 0;
    process_table_next = 0;
    next_user_pid = 1000;
    next_address_space_generation = 1;
    KLOG(LOG_INFO, "process", "user process subsystem initialized");
}

bool process_user_active(void) {
    return current_proc.active;
}

bool process_async_scheduler_active(void) {
    return async_scheduler_active && current_proc.active;
}

const char *process_status_name(process_status_t st) {
    switch (st) {
        case PROC_OK: return "ok";
        case PROC_ERR_INVAL: return "invalid";
        case PROC_ERR_IO: return "io";
        case PROC_ERR_FORMAT: return "format";
        case PROC_ERR_NOMEM: return "no memory";
        case PROC_ERR_RANGE: return "range";
        case PROC_ERR_FAULT: return "fault";
        case PROC_ERR_BUSY: return "busy";
        default: return "unknown";
    }
}


static const char *process_state_name(u32 state) {
    switch (state) {
        case PROCESS_STATE_RUNNING: return "running";
        case PROCESS_STATE_EXITED: return "exited";
        case PROCESS_STATE_FAULTED: return "faulted";
        case PROCESS_STATE_LOAD_ERROR: return "loaderr";
        case PROCESS_STATE_READY: return "ready";
        case PROCESS_STATE_SLEEPING: return "sleeping";
        case PROCESS_STATE_WAITING: return "waiting";
        case PROCESS_STATE_EMPTY:
        default: return "empty";
    }
}

static void fill_info_from_result(process_info_t *info, const process_result_t *r, process_status_t status, u32 state, u64 started, u64 finished) {
    if (!info || !r) return;
    memset(info, 0, sizeof(*info));
    info->pid = r->pid;
    info->state = state;
    info->exit_code = r->exit_code;
    info->status = (i32)status;
    info->started_ticks = started;
    info->finished_ticks = finished;
    info->entry = r->entry;
    info->user_stack_top = r->user_stack_top;
    info->mapped_pages = r->mapped_pages;
    info->address_space = 0;
    info->address_space_generation = r->address_space_generation;
    info->faulted = r->faulted ? 1u : 0u;
    info->fault_vector = r->fault_vector;
    info->fault_rip = r->fault_rip;
    info->fault_addr = r->fault_addr;
    strncpy(info->name, r->name, sizeof(info->name) - 1u);
}

static void record_process_result(const process_result_t *r, process_status_t status, u64 started, u64 finished) {
    if (!r || r->pid == 0 || !process_table) return;
    u32 state = r->faulted ? PROCESS_STATE_FAULTED : (status == PROC_OK ? PROCESS_STATE_EXITED : PROCESS_STATE_LOAD_ERROR);
    process_info_t info;
    fill_info_from_result(&info, r, status, state, started, finished);
    process_table[process_table_next] = info;
    process_table_next = (process_table_next + 1u) % PROCESS_TABLE_CAP;
    if (process_table_len < PROCESS_TABLE_CAP) ++process_table_len;
}

static bool process_state_terminal(u32 state) {
    return state == PROCESS_STATE_EXITED || state == PROCESS_STATE_FAULTED || state == PROCESS_STATE_LOAD_ERROR;
}

static bool process_state_live(u32 state) {
    return state == PROCESS_STATE_READY || state == PROCESS_STATE_RUNNING || state == PROCESS_STATE_SLEEPING || state == PROCESS_STATE_WAITING;
}

static u64 alloc_address_space_generation(void) {
    u64 g = next_address_space_generation++;
    if (next_address_space_generation == 0) next_address_space_generation = 1;
    if (g == 0) {
        g = next_address_space_generation++;
        if (next_address_space_generation == 0) next_address_space_generation = 1;
    }
    return g ? g : 1;
}

static active_process_t *slot_by_pid(u32 pid, usize *idx_out) {
    if (!async_slots || !pid) return 0;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && async_slots[i].result.pid == pid) {
            if (idx_out) *idx_out = i;
            return &async_slots[i];
        }
    }
    return 0;
}

static bool resolve_current_async_slot(void) {
    if (!async_slots || !current_proc.active || !current_proc.result.pid) return false;
    if (current_async_slot >= 0 && (usize)current_async_slot < PROCESS_ASYNC_CAP &&
        async_slots[(usize)current_async_slot].active &&
        async_slots[(usize)current_async_slot].result.pid == current_proc.result.pid) {
        return true;
    }
    usize idx = 0;
    if (!slot_by_pid(current_proc.result.pid, &idx)) return false;
    current_async_slot = (i32)idx;
    return true;
}

static void fill_info_from_active(process_info_t *info, const active_process_t *p) {
    if (!info || !p) return;
    fill_info_from_result(info, &p->result, p->result.faulted ? PROC_ERR_FAULT : PROC_OK, (u32)p->state,
                          p->result.started_ticks, process_state_terminal((u32)p->state) ? p->result.finished_ticks : pit_ticks());
}

static void sync_current_handles_to_snapshot(void) {
    if (!current_proc.active) return;
    syscall_save_user_handles(current_proc.fd_snapshot, sizeof(current_proc.fd_snapshot));
}

static bool sync_current_to_slot(void) {
    if (!resolve_current_async_slot()) return false;
    sync_current_handles_to_snapshot();
    async_slots[(usize)current_async_slot] = current_proc;
    return true;
}

static void load_slot_to_current(usize idx) {
    current_proc = async_slots[idx];
    current_async_slot = (i32)idx;
    current_proc.state = PROCESS_STATE_RUNNING;
    syscall_load_user_handles(current_proc.fd_snapshot, sizeof(current_proc.fd_snapshot));
}

static void clear_current_async(void) {
    if (!current_proc_ptr) current_proc_ptr = (active_process_t *)kmalloc(sizeof(active_process_t));
    if (!current_proc_ptr) PANIC("process current allocation failed");
    memset(current_proc_ptr, 0, sizeof(*current_proc_ptr));
    current_async_slot = -1;
    syscall_reset_user_handles();
}

u32 process_current_pid(void) {
    return current_proc.active ? current_proc.result.pid : 0u;
}

bool process_current_info(process_info_t *out) {
    if (!out || !current_proc.active) return false;
    fill_info_from_active(out, &current_proc);
    return true;
}

bool process_current_cwd(char *out, usize out_size) {
    if (!out || out_size == 0 || !current_proc.active || !current_proc.cwd[0]) return false;
    usize n = strnlen(current_proc.cwd, out_size);
    if (n >= out_size) return false;
    memcpy(out, current_proc.cwd, n + 1u);
    return true;
}

bool process_set_current_cwd(const char *path) {
    if (!path || !path[0] || !current_proc.active) return false;
    usize n = strnlen(path, VFS_PATH_MAX);
    if (n == 0 || n >= VFS_PATH_MAX || path[0] != '/') return false;
    memset(current_proc.cwd, 0, sizeof(current_proc.cwd));
    memcpy(current_proc.cwd, path, n + 1u);
    (void)sync_current_to_slot();
    return true;
}

usize process_table_count(void) { return process_table_len; }

bool process_lookup(u32 pid, process_info_t *out) {
    if (!out || pid == 0) return false;
    if (current_proc.active && current_proc.result.pid == pid) return process_current_info(out);
    active_process_t *slot = slot_by_pid(pid, 0);
    if (slot) {
        fill_info_from_active(out, slot);
        return true;
    }
    if (!process_table) return false;
    for (usize i = 0; i < process_table_len; ++i) {
        usize idx = (process_table_next + PROCESS_TABLE_CAP - 1u - i) % PROCESS_TABLE_CAP;
        if (process_table[idx].pid == pid) {
            *out = process_table[idx];
            return true;
        }
    }
    return false;
}

void process_dump_table(void) {
    kprintf("process table: records=%llu cap=%u next_pid=%u backing=%s\n", (unsigned long long)process_table_len, PROCESS_TABLE_CAP, next_user_pid, process_table ? "heap" : "none");
    if (!process_table) return;
    if (current_proc.active) {
        kprintf("  * pid=%u state=running asid=%llu pages=%llu name=%s\n", current_proc.result.pid,
                (unsigned long long)current_proc.result.address_space_generation,
                (unsigned long long)current_proc.result.mapped_pages, current_proc.result.name);
    }
    for (usize i = 0; i < process_table_len; ++i) {
        usize idx = (process_table_next + PROCESS_TABLE_CAP - 1u - i) % PROCESS_TABLE_CAP;
        const process_info_t *p = &process_table[idx];
        kprintf("  pid=%u state=%s exit=%d status=%s asid=%llu pages=%llu ticks=%llu..%llu name=%s\n",
                p->pid, process_state_name(p->state), p->exit_code, process_status_name((process_status_t)p->status),
                (unsigned long long)p->address_space_generation, (unsigned long long)p->mapped_pages,
                (unsigned long long)p->started_ticks, (unsigned long long)p->finished_ticks, p->name);
    }
}

static bool user_addr_range_valid(uptr user, usize size) {
    if (!aurora_rust_user_range_check((u64)user, size)) return false;
    if (!size) return true;
    return user < USER_SPACE_LIMIT;
}

bool process_validate_user_range(uptr user, usize size, bool write) {
    if (!current_proc.active || !user_addr_range_valid(user, size)) return false;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return false;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&current_proc.space, addr, &phys, &flags)) return false;
        if (!(flags & VMM_USER)) return false;
        if (write && !(flags & VMM_WRITE)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        done += step_info.chunk;
    }
    return true;
}

bool process_copy_from_user(void *dst, uptr user, usize size) {
    if (!dst && size) return false;
    if (!process_validate_user_range(user, size, false)) return false;
    u8 *d = (u8 *)dst;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return false;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&current_proc.space, addr, &phys, &flags) || !(flags & VMM_USER)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        usize n = step_info.chunk;
        memcpy(d + done, (const void *)phys, n);
        done += n;
    }
    return true;
}

bool process_copy_to_user(uptr user, const void *src, usize size) {
    if (!src && size) return false;
    if (!process_validate_user_range(user, size, true)) return false;
    const u8 *s = (const u8 *)src;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return false;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&current_proc.space, addr, &phys, &flags) || !(flags & VMM_USER) || !(flags & VMM_WRITE)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return true;
}

static bool copy_to_process_user_space(active_process_t *p, uptr user, const void *src, usize size) {
    if (!p || !p->active || (!src && size) || !user_addr_range_valid(user, size)) return false;
    const u8 *s = (const u8 *)src;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return false;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&p->space, addr, &phys, &flags) || !(flags & VMM_USER) || !(flags & VMM_WRITE)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return true;
}

bool process_copy_string_from_user(uptr user, char *out, usize out_size) {
    if (!out || out_size == 0) return false;
    for (usize i = 0; i < out_size; ++i) {
        char c = 0;
        uptr ch_addr = 0;
        if (__builtin_add_overflow(user, (uptr)i, &ch_addr) || !process_copy_from_user(&c, ch_addr, 1u)) {
            out[0] = 0;
            return false;
        }
        out[i] = c;
        if (c == 0) return true;
    }
    out[out_size - 1u] = 0;
    return false;
}

static void release_mappings(active_process_t *p) {
    for (usize i = 0; i < p->mapping_count; ++i) {
        (void)vmm_space_unmap_4k(&p->space, p->mappings[i].virt);
        if (p->mappings[i].phys) memory_free_page((void *)p->mappings[i].phys);
    }
    p->mapping_count = 0;
}

static user_mapping_t *find_mapping(active_process_t *p, uptr virt) {
    if (!p) return 0;
    for (usize i = 0; i < p->mapping_count; ++i) {
        if (p->mappings[i].virt == virt) return &p->mappings[i];
    }
    return 0;
}

static bool merge_page_flags(u64 old_flags, u64 segment_flags, u64 *merged_out) {
    bool wants_write = ((old_flags | segment_flags) & VMM_WRITE) != 0;
    bool wants_exec = ((old_flags & VMM_NX) == 0) || ((segment_flags & VMM_NX) == 0);
    if (wants_write && wants_exec) return false;
    u64 merged = VMM_USER;
    if (wants_write) merged |= VMM_WRITE;
    if (!wants_exec) merged |= VMM_NX;
    if (merged_out) *merged_out = merged;
    return true;
}

static process_status_t track_mapping(active_process_t *p, uptr virt, uptr phys, u64 final_flags) {
    if (p->mapping_count >= USER_MAX_MAPPINGS) return PROC_ERR_NOMEM;
    p->mappings[p->mapping_count].virt = virt;
    p->mappings[p->mapping_count].phys = phys;
    p->mappings[p->mapping_count].final_flags = final_flags;
    ++p->mapping_count;
    p->result.mapped_pages = p->mapping_count;
    return PROC_OK;
}

static process_status_t map_user_page(active_process_t *p, uptr virt, u64 flags) {
    if (!user_addr_range_valid(virt, PAGE_SIZE) || (virt & (PAGE_SIZE - 1u))) return PROC_ERR_RANGE;
    if (find_mapping(p, virt)) return PROC_ERR_RANGE;
    void *page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
    if (!page) return PROC_ERR_NOMEM;
    memset(page, 0, PAGE_SIZE);
    (void)vmm_space_unmap_4k(&p->space, virt);
    u64 final_flags = VMM_USER | flags;
    if (!vmm_space_map_4k(&p->space, virt, (uptr)page, final_flags)) {
        memory_free_page(page);
        return PROC_ERR_NOMEM;
    }
    process_status_t st = track_mapping(p, virt, (uptr)page, final_flags);
    if (st != PROC_OK) {
        (void)vmm_space_unmap_4k(&p->space, virt);
        memory_free_page(page);
    }
    return st;
}

static process_status_t ensure_image_page(active_process_t *p, uptr virt, u64 segment_final_flags) {
    if (!user_addr_range_valid(virt, PAGE_SIZE) || (virt & (PAGE_SIZE - 1u))) return PROC_ERR_RANGE;
    user_mapping_t *m = find_mapping(p, virt);
    if (m) {
        u64 merged = 0;
        if (!merge_page_flags(m->final_flags, segment_final_flags, &merged)) return PROC_ERR_FORMAT;
        m->final_flags = merged;
        if (!vmm_space_protect_4k(&p->space, virt, m->final_flags | VMM_WRITE)) return PROC_ERR_RANGE;
        return PROC_OK;
    }
    void *page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
    if (!page) return PROC_ERR_NOMEM;
    memset(page, 0, PAGE_SIZE);
    if (!vmm_space_map_4k(&p->space, virt, (uptr)page, segment_final_flags | VMM_WRITE)) {
        memory_free_page(page);
        return PROC_ERR_NOMEM;
    }
    process_status_t st = track_mapping(p, virt, (uptr)page, segment_final_flags);
    if (st != PROC_OK) {
        (void)vmm_space_unmap_4k(&p->space, virt);
        memory_free_page(page);
    }
    return st;
}

static process_status_t protect_loaded_image(active_process_t *p, usize first_mapping, usize last_mapping) {
    if (!p || first_mapping > last_mapping || last_mapping > p->mapping_count) return PROC_ERR_INVAL;
    for (usize i = first_mapping; i < last_mapping; ++i) {
        if (!vmm_space_protect_4k(&p->space, p->mappings[i].virt, p->mappings[i].final_flags)) return PROC_ERR_RANGE;
    }
    return PROC_OK;
}

static process_status_t copy_to_user_space(active_process_t *p, uptr user, const void *src, usize size) {
    if (!user_addr_range_valid(user, size)) return PROC_ERR_RANGE;
    const u8 *s = (const u8 *)src;
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return PROC_ERR_RANGE;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&p->space, addr, &phys, &flags) || !(flags & VMM_USER) || !(flags & VMM_WRITE)) return PROC_ERR_RANGE;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return PROC_ERR_RANGE;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return PROC_OK;
}

static process_status_t zero_user_space(active_process_t *p, uptr user, usize size) {
    static const u8 zeros[64] = {0};
    while (size) {
        usize n = size > sizeof(zeros) ? sizeof(zeros) : size;
        process_status_t st = copy_to_user_space(p, user, zeros, n);
        if (st != PROC_OK) return st;
        if (__builtin_add_overflow(user, (uptr)n, &user)) return PROC_ERR_RANGE;
        size -= n;
    }
    return PROC_OK;
}

static process_status_t read_vfs_to_user(active_process_t *p, const char *path, u64 file_off, uptr user, usize size) {
    usize done = 0;
    while (done < size) {
        uptr addr = 0;
        if (__builtin_add_overflow(user, (uptr)done, &addr)) return PROC_ERR_RANGE;
        uptr phys = 0;
        u64 flags = 0;
        if (!vmm_space_translate(&p->space, addr, &phys, &flags) || !(flags & VMM_USER) || !(flags & VMM_WRITE)) return PROC_ERR_RANGE;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return PROC_ERR_RANGE;
        usize n = step_info.chunk;
        u64 disk_off = 0;
        if (__builtin_add_overflow(file_off, (u64)done, &disk_off)) return PROC_ERR_RANGE;
        usize got = 0;
        vfs_status_t vs = vfs_read(path, disk_off, (void *)phys, n, &got);
        if (vs != VFS_OK) return PROC_ERR_IO;
        if (got != n) return PROC_ERR_IO;
        done += n;
    }
    return PROC_OK;
}

static u64 elf_segment_page_flags(const elf64_phdr_t *ph) {
    u64 flags = VMM_USER;
    if (ph->p_flags & ELF64_PF_W) flags |= VMM_WRITE;
    if (!(ph->p_flags & ELF64_PF_X)) flags |= VMM_NX;
    return flags;
}

static process_status_t load_elf_image(active_process_t *p, const char *path, u64 *entry_out) {
    vfs_stat_t st;
    if (vfs_stat(path, &st) != VFS_OK || st.type != VFS_NODE_FILE) return PROC_ERR_IO;
    elf64_ehdr_t eh;
    usize got = 0;
    if (vfs_read(path, 0, &eh, sizeof(eh), &got) != VFS_OK || got != sizeof(eh)) return PROC_ERR_IO;
    elf_status_t ev = elf64_validate_header(&eh, st.size);
    if (ev != ELF_OK || eh.e_phnum == 0) return PROC_ERR_FORMAT;
    if (eh.e_entry < USER_IMAGE_BASE || eh.e_entry >= USER_SPACE_LIMIT) return PROC_ERR_RANGE;
    if (eh.e_phnum > 32u) return PROC_ERR_RANGE;

    bool entry_in_exec_segment = false;
    usize image_first_mapping = p->mapping_count;
    for (u16 i = 0; i < eh.e_phnum; ++i) {
        elf64_phdr_t ph;
        got = 0;
        u64 ph_index_off = 0;
        u64 phoff = 0;
        if (__builtin_mul_overflow((u64)i, (u64)eh.e_phentsize, &ph_index_off) ||
            __builtin_add_overflow(eh.e_phoff, ph_index_off, &phoff)) return PROC_ERR_RANGE;
        if (vfs_read(path, phoff, &ph, sizeof(ph), &got) != VFS_OK || got != sizeof(ph)) return PROC_ERR_IO;
        if (ph.p_type != ELF64_PT_LOAD) continue;
        if (ph.p_memsz < ph.p_filesz) return PROC_ERR_FORMAT;
        if (ph.p_vaddr < USER_IMAGE_BASE || ph.p_vaddr + ph.p_memsz < ph.p_vaddr || ph.p_vaddr + ph.p_memsz >= USER_SPACE_LIMIT) return PROC_ERR_RANGE;
        if (ph.p_offset + ph.p_filesz < ph.p_offset || ph.p_offset + ph.p_filesz > st.size) return PROC_ERR_FORMAT;
        if (ph.p_memsz == 0) continue;
        u64 seg_end = ph.p_vaddr + ph.p_memsz;
        if ((ph.p_flags & ELF64_PF_X) && eh.e_entry >= ph.p_vaddr && eh.e_entry < seg_end) entry_in_exec_segment = true;

        uptr start = AURORA_ALIGN_DOWN((uptr)ph.p_vaddr, PAGE_SIZE);
        uptr end = AURORA_ALIGN_UP((uptr)(ph.p_vaddr + ph.p_memsz), PAGE_SIZE);
        u64 final_flags = elf_segment_page_flags(&ph);
        for (uptr v = start; v < end; v += PAGE_SIZE) {
            process_status_t ms = ensure_image_page(p, v, final_flags);
            if (ms != PROC_OK) return ms;
        }
        if (ph.p_filesz) {
            process_status_t rs = read_vfs_to_user(p, path, ph.p_offset, (uptr)ph.p_vaddr, (usize)ph.p_filesz);
            if (rs != PROC_OK) return rs;
        }
        if (ph.p_memsz > ph.p_filesz) {
            process_status_t zs = zero_user_space(p, (uptr)(ph.p_vaddr + ph.p_filesz), (usize)(ph.p_memsz - ph.p_filesz));
            if (zs != PROC_OK) return zs;
        }
    }

    if (!entry_in_exec_segment) return PROC_ERR_FORMAT;
    process_status_t ps = protect_loaded_image(p, image_first_mapping, p->mapping_count);
    if (ps != PROC_OK) return ps;
    *entry_out = eh.e_entry;
    return PROC_OK;
}

static process_status_t build_user_stack(active_process_t *p, int argc, const char *const *argv, int envc, const char *const *envp, u64 *rsp_out, u64 *argv_out, u64 *envp_out) {
    if (argc < 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    uptr stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    for (usize i = 0; i < USER_STACK_PAGES; ++i) {
        process_status_t st = map_user_page(p, stack_base + i * PAGE_SIZE, VMM_WRITE | VMM_NX);
        if (st != PROC_OK) return st;
    }
    uptr sp = USER_STACK_TOP;
    u64 arg_ptrs[PROCESS_ARG_MAX + 1u];
    u64 env_ptrs[PROCESS_ENV_MAX + 1u];
    memset(arg_ptrs, 0, sizeof(arg_ptrs));
    memset(env_ptrs, 0, sizeof(env_ptrs));
    for (int i = envc - 1; i >= 0; --i) {
        const char *s = envp && envp[i] ? envp[i] : "";
        usize len = strnlen(s, USER_ARG_STRING_MAX);
        if (len >= USER_ARG_STRING_MAX || len == 0) return PROC_ERR_RANGE;
        sp -= len + 1u;
        process_status_t cs = copy_to_user_space(p, sp, s, len + 1u);
        if (cs != PROC_OK) return cs;
        env_ptrs[i] = sp;
    }
    for (int i = argc - 1; i >= 0; --i) {
        const char *s = argv && argv[i] ? argv[i] : "";
        usize len = strnlen(s, USER_ARG_STRING_MAX);
        if (len >= USER_ARG_STRING_MAX) return PROC_ERR_RANGE;
        sp -= len + 1u;
        process_status_t cs = copy_to_user_space(p, sp, s, len + 1u);
        if (cs != PROC_OK) return cs;
        arg_ptrs[i] = sp;
    }
    sp &= ~0xfull;
    sp -= (u64)(envc + 1) * sizeof(u64);
    process_status_t ps = copy_to_user_space(p, sp, env_ptrs, (usize)(envc + 1) * sizeof(u64));
    if (ps != PROC_OK) return ps;
    u64 env_user = sp;
    sp -= (u64)(argc + 1) * sizeof(u64);
    ps = copy_to_user_space(p, sp, arg_ptrs, (usize)(argc + 1) * sizeof(u64));
    if (ps != PROC_OK) return ps;
    *argv_out = sp;
    *envp_out = env_user;
    *rsp_out = sp & ~0xfull;
    return PROC_OK;
}


static void init_user_regs(cpu_regs_t *regs, u64 entry, u64 user_rsp, u64 argc, u64 argv_user, u64 aux) {
    memset(regs, 0, sizeof(*regs));
    regs->rip = entry;
    regs->cs = 0x23u;
    regs->rflags = 0x202u;
    regs->rsp = user_rsp;
    regs->ss = 0x1bu;
    regs->rdi = argc;
    regs->rsi = argv_user;
    regs->rdx = aux;
}

static process_status_t prepare_process_env(active_process_t *p, const char *path, int argc, const char *const *argv, int envc, const char *const *envp, u32 parent_pid) {
    if (!p || !path || !*path || argc < 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    memset(p, 0, sizeof(*p));
    if (!vmm_space_create_user(&p->space)) return PROC_ERR_NOMEM;
    p->active = true;
    p->state = PROCESS_STATE_READY;
    p->parent_pid = parent_pid;
    if (current_proc.active && current_proc.cwd[0]) strncpy(p->cwd, current_proc.cwd, sizeof(p->cwd) - 1u);
    else strncpy(p->cwd, "/", sizeof(p->cwd) - 1u);
    p->result.pid = next_user_pid++;
    p->result.started_ticks = pit_ticks();
    p->result.address_space = p->space.pml4_physical;
    p->result.address_space_generation = alloc_address_space_generation();
    strncpy(p->result.name, path, sizeof(p->result.name) - 1u);
    syscall_prepare_user_handle_snapshot(p->fd_snapshot, sizeof(p->fd_snapshot));

    u64 entry = 0;
    process_status_t st = load_elf_image(p, path, &entry);
    if (st == PROC_OK) {
        u64 user_rsp = 0;
        u64 user_argv = 0;
        u64 user_envp = 0;
        st = build_user_stack(p, argc, argv, envc, envp, &user_rsp, &user_argv, &user_envp);
        if (st == PROC_OK) {
            p->result.entry = entry;
            p->result.user_stack_top = USER_STACK_TOP;
            init_user_regs(&p->regs, entry, user_rsp, (u64)argc, user_argv, user_envp);
            KLOG(LOG_INFO, "process", "prepared pid=%u path=%s pml4=%p entry=%p rsp=%p argc=%d pages=%llu",
                 p->result.pid, path, (void *)p->space.pml4_physical, (void *)(uptr)entry, (void *)(uptr)user_rsp, argc,
                 (unsigned long long)p->result.mapped_pages);
            return PROC_OK;
        }
    }

    p->state = PROCESS_STATE_LOAD_ERROR;
    p->result.finished_ticks = pit_ticks();
    last_result = p->result;
    record_process_result(&p->result, st, p->result.started_ticks, p->result.finished_ticks);
    release_mappings(p);
    vmm_space_destroy(&p->space);
    memset(p, 0, sizeof(*p));
    return st;
}

static process_status_t prepare_process(active_process_t *p, const char *path, int argc, const char *const *argv, u32 parent_pid) {
    return prepare_process_env(p, path, argc, argv, 0, 0, parent_pid);
}

static process_status_t prepare_exec_replacement_env(active_process_t *dst, const char *path, int argc, const char *const *argv, int envc, const char *const *envp) {
    if (!dst || !current_proc.active || !path || !*path || argc <= 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    memset(dst, 0, sizeof(*dst));
    if (!vmm_space_create_user(&dst->space)) return PROC_ERR_NOMEM;
    dst->active = true;
    dst->state = PROCESS_STATE_RUNNING;
    dst->parent_pid = current_proc.parent_pid;
    strncpy(dst->cwd, current_proc.cwd[0] ? current_proc.cwd : "/", sizeof(dst->cwd) - 1u);
    dst->wait_pid = 0;
    dst->wait_out_ptr = 0;
    dst->wake_tick = 0;
    dst->result = current_proc.result;
    dst->result.exit_code = 0;
    dst->result.finished_ticks = 0;
    dst->result.faulted = false;
    dst->result.fault_vector = 0;
    dst->result.fault_rip = 0;
    dst->result.fault_addr = 0;
    dst->result.address_space = dst->space.pml4_physical;
    dst->result.address_space_generation = alloc_address_space_generation();
    dst->result.mapped_pages = 0;
    memset(dst->result.name, 0, sizeof(dst->result.name));
    strncpy(dst->result.name, path, sizeof(dst->result.name) - 1u);

    u64 entry = 0;
    process_status_t st = load_elf_image(dst, path, &entry);
    if (st == PROC_OK) {
        u64 user_rsp = 0;
        u64 user_argv = 0;
        u64 user_envp = 0;
        st = build_user_stack(dst, argc, argv, envc, envp, &user_rsp, &user_argv, &user_envp);
        if (st == PROC_OK) {
            dst->result.entry = entry;
            dst->result.user_stack_top = USER_STACK_TOP;
            init_user_regs(&dst->regs, entry, user_rsp, (u64)argc, user_argv, user_envp);
            syscall_save_user_handles(dst->fd_snapshot, sizeof(dst->fd_snapshot));
            KLOG(LOG_INFO, "process", "exec prepared pid=%u path=%s pml4=%p entry=%p rsp=%p argc=%d pages=%llu",
                 dst->result.pid, path, (void *)dst->space.pml4_physical, (void *)(uptr)entry, (void *)(uptr)user_rsp, argc,
                 (unsigned long long)dst->result.mapped_pages);
            return PROC_OK;
        }
    }
    release_mappings(dst);
    vmm_space_destroy(&dst->space);
    memset(dst, 0, sizeof(*dst));
    return st;
}

static void commit_exec_replacement(cpu_regs_t *regs) {
    if (!exec_replacement || !exec_old_proc) return;

    /*
     * active_process_t is intentionally large: it contains the address-space
     * mapping table and the fd snapshot.  Keeping the old image as a local
     * value here can overflow the ring-0 syscall/interrupt stack during
     * fork+exec pipelines.  Use the reusable heap-backed scratch process
     * object instead and never materialize this structure on the kernel stack.
     */
    *exec_old_proc = current_proc;

    syscall_close_user_handles_with_flags(AURORA_FD_CLOEXEC);
    current_proc = *exec_replacement;
    memset(exec_replacement, 0, sizeof(*exec_replacement));
    exec_requested = false;
    current_proc.state = PROCESS_STATE_RUNNING;
    syscall_save_user_handles(current_proc.fd_snapshot, sizeof(current_proc.fd_snapshot));
    if (regs) *regs = current_proc.regs;
    (void)sync_current_to_slot();
    vmm_switch_space(&current_proc.space);
    release_mappings(exec_old_proc);
    vmm_space_destroy(&exec_old_proc->space);
    memset(exec_old_proc, 0, sizeof(*exec_old_proc));
    KLOG(LOG_INFO, "process", "exec pid=%u image=%s asid=%llu", current_proc.result.pid,
         current_proc.result.name, (unsigned long long)current_proc.result.address_space_generation);
}

static void discard_exec_replacement(void) {
    if (!exec_replacement) {
        exec_requested = false;
        return;
    }
    if (exec_replacement->active || exec_replacement->space.pml4_physical || exec_replacement->mapping_count) {
        release_mappings(exec_replacement);
        vmm_space_destroy(&exec_replacement->space);
    }
    memset(exec_replacement, 0, sizeof(*exec_replacement));
    exec_requested = false;
}

static active_process_t *alloc_async_slot(void) {
    if (!async_slots) return 0;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (!async_slots[i].active) return &async_slots[i];
    }
    return 0;
}

process_status_t process_spawn_async(const char *path, int argc, const char *const *argv, u32 *pid_out) {
    active_process_t *slot = alloc_async_slot();
    if (!slot) return PROC_ERR_NOMEM;
    u32 parent = current_proc.active ? current_proc.result.pid : 0;
    process_status_t st = prepare_process(slot, path, argc, argv, parent);
    if (st != PROC_OK) {
        release_mappings(slot);
        if (slot->space.pml4_physical) vmm_space_destroy(&slot->space);
        memset(slot, 0, sizeof(*slot));
        return st;
    }
    if (current_proc.active) {
        syscall_save_user_handles(slot->fd_snapshot, sizeof(slot->fd_snapshot));
        if (!syscall_retain_user_handle_snapshot(slot->fd_snapshot, sizeof(slot->fd_snapshot))) {
            release_mappings(slot);
            vmm_space_destroy(&slot->space);
            memset(slot, 0, sizeof(*slot));
            return PROC_ERR_NOMEM;
        }
    }
    if (pid_out) *pid_out = slot->result.pid;
    return PROC_OK;
}

process_status_t process_spawn_async_snapshot(const char *path, int argc, const char *const *argv, void *snapshot, usize snapshot_size, u32 *pid_out) {
    if (!snapshot || snapshot_size < syscall_user_handle_snapshot_size()) return PROC_ERR_INVAL;
    active_process_t *slot = alloc_async_slot();
    if (!slot) return PROC_ERR_NOMEM;
    u32 parent = current_proc.active ? current_proc.result.pid : 0;
    process_status_t st = prepare_process(slot, path, argc, argv, parent);
    if (st != PROC_OK) {
        release_mappings(slot);
        if (slot->space.pml4_physical) vmm_space_destroy(&slot->space);
        memset(slot, 0, sizeof(*slot));
        return st;
    }
    syscall_release_user_handle_snapshot(slot->fd_snapshot, sizeof(slot->fd_snapshot));
    memcpy(slot->fd_snapshot, snapshot, sizeof(slot->fd_snapshot));
    memset(snapshot, 0, sizeof(slot->fd_snapshot));
    if (pid_out) *pid_out = slot->result.pid;
    return PROC_OK;
}

static bool clone_current_address_space(active_process_t *child) {
    if (!child || !current_proc.active) return false;
    if (!vmm_space_create_user(&child->space)) return false;
    child->active = true;
    child->state = PROCESS_STATE_READY;
    child->parent_pid = current_proc.result.pid;
    strncpy(child->cwd, current_proc.cwd[0] ? current_proc.cwd : "/", sizeof(child->cwd) - 1u);
    child->result = current_proc.result;
    child->result.pid = next_user_pid++;
    child->result.started_ticks = pit_ticks();
    child->result.finished_ticks = 0;
    child->result.faulted = false;
    child->result.fault_vector = 0;
    child->result.fault_rip = 0;
    child->result.fault_addr = 0;
    child->result.exit_code = 0;
    child->result.address_space = child->space.pml4_physical;
    child->result.address_space_generation = alloc_address_space_generation();
    child->mapping_count = 0;
    for (usize i = 0; i < current_proc.mapping_count; ++i) {
        void *page = memory_alloc_page_below(USER_BACKING_PHYS_LIMIT);
        if (!page) return false;
        memcpy(page, (const void *)current_proc.mappings[i].phys, PAGE_SIZE);
        if (!vmm_space_map_4k(&child->space, current_proc.mappings[i].virt, (uptr)page, current_proc.mappings[i].final_flags)) {
            memory_free_page(page);
            return false;
        }
        if (track_mapping(child, current_proc.mappings[i].virt, (uptr)page, current_proc.mappings[i].final_flags) != PROC_OK) {
            (void)vmm_space_unmap_4k(&child->space, current_proc.mappings[i].virt);
            memory_free_page(page);
            return false;
        }
    }
    syscall_save_user_handles(child->fd_snapshot, sizeof(child->fd_snapshot));
    if (!syscall_retain_user_handle_snapshot(child->fd_snapshot, sizeof(child->fd_snapshot))) return false;
    return true;
}

static bool fork_current_from_regs(cpu_regs_t *regs, u32 *pid_out) {
    if (!regs || !async_slots || current_async_slot < 0) return false;
    active_process_t *slot = alloc_async_slot();
    if (!slot) return false;
    memset(slot, 0, sizeof(*slot));
    if (!clone_current_address_space(slot)) {
        release_mappings(slot);
        vmm_space_destroy(&slot->space);
        memset(slot, 0, sizeof(*slot));
        return false;
    }
    slot->regs = *regs;
    slot->regs.rax = 0;
    slot->regs.rdx = 0;
    if (pid_out) *pid_out = slot->result.pid;
    KLOG(LOG_INFO, "process", "fork parent=%u child=%u pages=%llu", current_proc.result.pid, slot->result.pid,
         (unsigned long long)slot->result.mapped_pages);
    return true;
}

static void finish_current(process_lifecycle_t state, process_status_t status, cpu_regs_t *regs) {
    if (regs) current_proc.regs = *regs;
    current_proc.state = state;
    current_proc.active = true;
    current_proc.result.finished_ticks = pit_ticks();
    last_result = current_proc.result;
    record_process_result(&last_result, status, last_result.started_ticks, last_result.finished_ticks);
    bool synced = sync_current_to_slot();
    if (!synced && async_scheduler_active) {
        KLOG(LOG_ERROR, "process", "lost async slot for pid=%u while finishing", current_proc.result.pid);
    }
    KLOG(LOG_INFO, "process", "finish pid=%u state=%s exit=%d", current_proc.result.pid,
         process_state_name((u32)state), current_proc.result.exit_code);
}

static void wake_waiters(u32 pid) {
    if (!async_slots || !pid) return;
    active_process_t *child = slot_by_pid(pid, 0);
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_WAITING && p->wait_pid == pid) {
            process_info_t info;
            bool copied = false;
            if (child && process_state_terminal((u32)child->state) && p->wait_out_ptr) {
                fill_info_from_active(&info, child);
                copied = copy_to_process_user_space(p, p->wait_out_ptr, &info, sizeof(info));
            }
            p->regs.rax = copied ? 0u : (u64)-1ll;
            p->regs.rdx = copied ? 0u : (u64)VFS_ERR_INVAL;
            p->state = PROCESS_STATE_READY;
            p->wait_pid = 0;
            p->wait_out_ptr = 0;
        }
    }
}

static void wake_sleepers(void) {
    if (!async_slots) return;
    u64 now = pit_ticks();
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_SLEEPING && p->wake_tick <= now) {
            p->state = PROCESS_STATE_READY;
            p->wake_tick = 0;
        }
    }
}

static bool any_live_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && process_state_live((u32)async_slots[i].state)) return true;
    }
    return false;
}

static bool any_sleeping_process(void) {
    if (!async_slots) return false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active && async_slots[i].state == PROCESS_STATE_SLEEPING) return true;
    }
    return false;
}

static void fault_wait_deadlock(void) {
    if (!async_slots) return;
    u64 now = pit_ticks();
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_WAITING) {
            p->state = PROCESS_STATE_FAULTED;
            p->result.exit_code = -127;
            p->result.faulted = true;
            p->result.fault_vector = 0xfeu;
            p->result.fault_rip = p->regs.rip;
            p->result.fault_addr = p->wait_pid;
            p->result.finished_ticks = now;
            record_process_result(&p->result, PROC_ERR_FAULT, p->result.started_ticks, now);
        }
    }
}

static bool pick_ready_slot(usize *idx_out) {
    if (!async_slots || !idx_out) return false;
    wake_sleepers();
    for (usize scan = 0; scan < PROCESS_ASYNC_CAP; ++scan) {
        usize idx = (async_next_rr + scan) % PROCESS_ASYNC_CAP;
        if (async_slots[idx].active && async_slots[idx].state == PROCESS_STATE_READY) {
            *idx_out = idx;
            async_next_rr = (u32)((idx + 1u) % PROCESS_ASYNC_CAP);
            return true;
        }
    }
    return false;
}

static void dispose_terminal_slots_except(u32 keep_pid) {
    if (!async_slots) return;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->result.pid != keep_pid && process_state_terminal((u32)p->state)) {
            syscall_release_user_handle_snapshot(p->fd_snapshot, sizeof(p->fd_snapshot));
            release_mappings(p);
            vmm_space_destroy(&p->space);
            memset(p, 0, sizeof(*p));
        }
    }
}

bool process_run_until_idle(u32 root_pid, process_result_t *root_out) {
    if (!async_slots || !root_pid) return false;
    bool found_root = false;
    async_scheduler_active = true;
    for (;;) {
        active_process_t *root = slot_by_pid(root_pid, 0);
        if (root && process_state_terminal((u32)root->state)) {
            found_root = true;
            if (!any_live_process()) break;
        } else if (!root) {
            break;
        }
        usize idx = 0;
        if (!pick_ready_slot(&idx)) {
            if (!any_live_process()) break;
            if (!any_sleeping_process()) {
                fault_wait_deadlock();
                continue;
            }
            cpu_sti();
            cpu_hlt();
            continue;
        }
        load_slot_to_current(idx);
        vmm_switch_space(&current_proc.space);
        arch_user_resume(&current_proc.regs);
        cpu_sti();
        continue;
    }
    vmm_switch_kernel();
    syscall_reset_user_handles();
    active_process_t *root = slot_by_pid(root_pid, 0);
    if (root && process_state_terminal((u32)root->state)) found_root = true;
    if (root) {
        if (root_out) *root_out = root->result;
        last_result = root->result;
        syscall_release_user_handle_snapshot(root->fd_snapshot, sizeof(root->fd_snapshot));
        release_mappings(root);
        vmm_space_destroy(&root->space);
        memset(root, 0, sizeof(*root));
    }
    dispose_terminal_slots_except(root_pid);
    clear_current_async();
    async_scheduler_active = false;
    reschedule_requested = false;
    exit_requested = false;
    fork_requested = false;
    discard_exec_replacement();
    requested_exit_code = 0;
    return found_root;
}

process_status_t process_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp, process_result_t *out) {
    if (!path || !*path || argc < 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    if (async_scheduler_active || current_proc.active) return PROC_ERR_INVAL;
    if (!async_slots) return PROC_ERR_NOMEM;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        if (async_slots[i].active) return PROC_ERR_BUSY;
    }
    memset(async_slots, 0, sizeof(active_process_t) * PROCESS_ASYNC_CAP);
    async_next_rr = 0;
    u32 pid = 0;
    active_process_t *slot = alloc_async_slot();
    process_status_t st = slot ? prepare_process_env(slot, path, argc, argv, envc, envp, 0) : PROC_ERR_NOMEM;
    if (st == PROC_OK) pid = slot->result.pid;
    if (st != PROC_OK) {
        if (out) memset(out, 0, sizeof(*out));
        return st;
    }
    process_result_t result;
    memset(&result, 0, sizeof(result));
    bool ran = process_run_until_idle(pid, &result);
    if (!ran && result.pid != 0) {
        process_info_t info;
        if (process_lookup(result.pid, &info) && process_state_terminal(info.state)) ran = true;
    }
    if (out) *out = result;
    if (!ran) return PROC_ERR_FAULT;
    return result.faulted ? PROC_ERR_FAULT : PROC_OK;
}

process_status_t process_exec(const char *path, int argc, const char *const *argv, process_result_t *out) {
    return process_execve(path, argc, argv, 0, 0, out);
}

process_status_t process_spawn(const char *path, int argc, const char *const *argv, u32 *pid_out, process_result_t *out) {
    process_result_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    process_status_t st = process_exec(path, argc, argv, &tmp);
    if (st == PROC_OK) {
        if (pid_out) *pid_out = tmp.pid;
        if (out) *out = tmp;
    } else {
        if (pid_out) *pid_out = 0;
        if (out) memset(out, 0, sizeof(*out));
    }
    return st;
}

bool process_wait(u32 pid, process_info_t *out) {
    if (!pid || !out) return false;
    process_info_t info;
    if (!process_lookup(pid, &info)) return false;
    if (process_state_live(info.state)) return false;
    *out = info;
    return true;
}


void process_request_reschedule(void) {
    if (!process_async_scheduler_active()) return;
    reschedule_requested = true;
}

void process_request_sleep(u64 ticks) {
    if (!process_async_scheduler_active()) return;
    reschedule_requested = true;
    current_proc.state = PROCESS_STATE_SLEEPING;
    u64 now = pit_ticks();
    current_proc.wake_tick = ticks > 0xffffffffffffffffull - now ? 0xffffffffffffffffull : now + ticks;
    if (ticks == 0) current_proc.state = PROCESS_STATE_READY;
}

void process_request_exit(i32 code) {
    if (!process_async_scheduler_active()) return;
    requested_exit_code = code;
    exit_requested = true;
}

bool process_request_wait(u32 pid, uptr out_ptr) {
    if (!process_async_scheduler_active() || !pid || pid == current_proc.result.pid || !out_ptr) return false;
    if (!process_validate_user_range(out_ptr, sizeof(process_info_t), true)) return false;
    process_info_t info;
    if (!process_lookup(pid, &info)) return false;
    active_process_t *target = slot_by_pid(pid, 0);
    if (!target || target->parent_pid != current_proc.result.pid) return false;
    if (!process_state_live(info.state)) return false;
    current_proc.state = PROCESS_STATE_WAITING;
    current_proc.wait_pid = pid;
    current_proc.wait_out_ptr = out_ptr;
    reschedule_requested = true;
    return true;
}

bool process_request_fork(void) {
    if (!process_async_scheduler_active()) return false;
    fork_requested = true;
    return true;
}

process_status_t process_request_execve(const char *path, int argc, const char *const *argv, int envc, const char *const *envp) {
    if (!process_async_scheduler_active() || !current_proc.active) return PROC_ERR_INVAL;
    if (exec_requested) discard_exec_replacement();
    if (!exec_replacement) return PROC_ERR_NOMEM;
    process_status_t st = prepare_exec_replacement_env(exec_replacement, path, argc, argv, envc, envp);
    if (st != PROC_OK) return st;
    exec_requested = true;
    return PROC_OK;
}

process_status_t process_request_exec(const char *path, int argc, const char *const *argv) {
    return process_request_execve(path, argc, argv, 0, 0);
}

static AURORA_NORETURN void leave_user_scheduler(void) {
    vmm_switch_kernel();
    arch_user_return_from_interrupt();
}

void process_preempt_from_interrupt(cpu_regs_t *regs) {
    if (!async_scheduler_active || !current_proc.active || !regs) return;
    if (!resolve_current_async_slot()) {
        current_proc.result.exit_code = -128;
        current_proc.result.faulted = true;
        current_proc.result.fault_vector = 0xffu;
        current_proc.result.fault_rip = regs->rip;
        current_proc.result.fault_addr = 0;
        finish_current(PROCESS_STATE_FAULTED, PROC_ERR_FAULT, regs);
        clear_current_async();
        leave_user_scheduler();
    }
    current_proc.regs = *regs;
    if (current_proc.state == PROCESS_STATE_RUNNING) current_proc.state = PROCESS_STATE_READY;
    sync_current_to_slot();
    clear_current_async();
    leave_user_scheduler();
}

void process_fault_current_from_interrupt(cpu_regs_t *regs, u64 vector, u64 cr2) {
    if (!async_scheduler_active || !current_proc.active || !resolve_current_async_slot()) {
        process_fault_from_interrupt(vector, regs ? regs->rip : 0, cr2);
    }
    current_proc.result.exit_code = -128 - (i32)vector;
    current_proc.result.faulted = true;
    current_proc.result.fault_vector = vector;
    current_proc.result.fault_rip = regs ? regs->rip : 0;
    current_proc.result.fault_addr = cr2;
    finish_current(PROCESS_STATE_FAULTED, PROC_ERR_FAULT, regs);
    wake_waiters(current_proc.result.pid);
    clear_current_async();
    leave_user_scheduler();
}

bool process_after_syscall(cpu_regs_t *regs) {
    if (!async_scheduler_active || !current_proc.active || !regs) return false;
    if (!resolve_current_async_slot()) return false;
    if (fork_requested) {
        u32 child_pid = 0;
        if (fork_current_from_regs(regs, &child_pid)) {
            regs->rax = child_pid;
            regs->rdx = 0;
        } else {
            regs->rax = (u64)-1ll;
            regs->rdx = (u64)-4ll;
        }
        fork_requested = false;
    }
    if (exec_requested) {
        commit_exec_replacement(regs);
        return false;
    }
    if (exit_requested) {
        current_proc.result.exit_code = requested_exit_code;
        finish_current(PROCESS_STATE_EXITED, PROC_OK, regs);
        wake_waiters(current_proc.result.pid);
        exit_requested = false;
        requested_exit_code = 0;
        clear_current_async();
        leave_user_scheduler();
    }
    if (reschedule_requested || current_proc.state == PROCESS_STATE_SLEEPING || current_proc.state == PROCESS_STATE_WAITING) {
        if (current_proc.state == PROCESS_STATE_RUNNING) current_proc.state = PROCESS_STATE_READY;
        reschedule_requested = false;
        process_preempt_from_interrupt(regs);
        return true;
    }
    return false;
}


AURORA_NORETURN void process_exit_current_from_syscall(cpu_regs_t *regs, i32 code) {
    if (current_proc.active && async_scheduler_active) {
        (void)resolve_current_async_slot();
        current_proc.result.exit_code = code;
        finish_current(PROCESS_STATE_EXITED, PROC_OK, regs);
        wake_waiters(current_proc.result.pid);
        exit_requested = false;
        requested_exit_code = 0;
        fork_requested = false;
        clear_current_async();
        leave_user_scheduler();
    }
    if (current_proc.active) {
        current_proc.result.exit_code = code;
        current_proc.returned = true;
    }
    arch_user_return_from_interrupt();
}

AURORA_NORETURN void process_exit_from_interrupt(i32 code) {
    if (current_proc.active) {
        current_proc.result.exit_code = code;
        current_proc.returned = true;
        KLOG(LOG_INFO, "process", "exit pid=%u code=%d", current_proc.result.pid, code);
    }
    arch_user_return_from_interrupt();
}

AURORA_NORETURN void process_fault_from_interrupt(u64 vector, u64 rip, u64 cr2) {
    if (current_proc.active) {
        current_proc.result.exit_code = -128 - (i32)vector;
        current_proc.result.faulted = true;
        current_proc.result.fault_vector = vector;
        current_proc.result.fault_rip = rip;
        current_proc.result.fault_addr = cr2;
        KLOG(LOG_ERROR, "process", "fault pid=%u vector=%llu rip=%p cr2=%p", current_proc.result.pid,
             (unsigned long long)vector, (void *)(uptr)rip, (void *)(uptr)cr2);
    }
    arch_user_return_from_interrupt();
}

void process_dump_last(void) {
    kprintf("last user process: pid=%u name=%s exit=%d pml4=%p asid=%llu entry=%p stack_top=%p pages=%llu ticks=%llu..%llu faulted=%u\n",
            last_result.pid, last_result.name, last_result.exit_code, (void *)(uptr)last_result.address_space,
            (unsigned long long)last_result.address_space_generation, (void *)(uptr)last_result.entry, (void *)(uptr)last_result.user_stack_top,
            (unsigned long long)last_result.mapped_pages, (unsigned long long)last_result.started_ticks, (unsigned long long)last_result.finished_ticks, last_result.faulted ? 1u : 0u);
    if (last_result.faulted) {
        kprintf("  fault vector=%llu rip=%p addr=%p\n", (unsigned long long)last_result.fault_vector,
                (void *)(uptr)last_result.fault_rip, (void *)(uptr)last_result.fault_addr);
    }
}


bool process_table_selftest(void) {
    usize before = process_table_count();
    const char *argv0[] = { "/bin/hello" };
    process_result_t r;
    process_status_t st = process_exec("/bin/hello", 1, argv0, &r);
    if (st != PROC_OK || r.pid == 0 || r.exit_code != 7 || r.faulted) return false;
    if (process_table_count() != before + 1u && process_table_count() != PROCESS_TABLE_CAP) return false;
    process_info_t info;
    if (!process_lookup(r.pid, &info)) return false;
    if (info.pid != r.pid || info.state != PROCESS_STATE_EXITED || info.exit_code != 7) return false;
    if (info.address_space_generation != r.address_space_generation || info.mapped_pages == 0) return false;
    if (info.started_ticks > info.finished_ticks) return false;
    if (process_lookup(0, &info)) return false;
    if (process_lookup(0xffffffffu, &info)) return false;
    u32 pid = 0;
    const char *spawn_argv[] = { "/bin/hello" };
    process_result_t spawned;
    process_status_t sst = process_spawn("/bin/hello", 1, spawn_argv, &pid, &spawned);
    if (sst != PROC_OK || pid == 0 || spawned.exit_code != 7) return false;
    if (!process_wait(pid, &info) || info.pid != pid || info.exit_code != 7 || info.state != PROCESS_STATE_EXITED) return false;
    if (process_wait(0xffffffffu, &info)) return false;
    return true;
}

bool process_selftest(void) {
    const char *argv0[] = { "/bin/hello" };
    process_result_t r1;
    process_status_t st = process_exec("/bin/hello", 1, argv0, &r1);
    if (st != PROC_OK || r1.faulted || r1.exit_code != 7 || r1.address_space == vmm_kernel_space()->pml4_physical) return false;

    const char *argv_reg[] = { "/bin/regtrash" };
    process_result_t rreg;
    st = process_exec("/bin/regtrash", 1, argv_reg, &rreg);
    if (st != PROC_OK || rreg.faulted || rreg.exit_code != 23) return false;
    if (rreg.address_space_generation == r1.address_space_generation) return false;

    const char *argv_bad[] = { "/bin/badptr" };
    process_result_t rbad;
    st = process_exec("/bin/badptr", 1, argv_bad, &rbad);
    if (st != PROC_OK || rbad.faulted || rbad.exit_code != 0) return false;

    const char *argv_badpath[] = { "/bin/badpath" };
    process_result_t rbadpath;
    st = process_exec("/bin/badpath", 1, argv_badpath, &rbadpath);
    if (st != PROC_OK || rbadpath.faulted || rbadpath.exit_code != 0) return false;

    const char *argv_stat[] = { "/bin/statcheck" };
    process_result_t rstat;
    st = process_exec("/bin/statcheck", 1, argv_stat, &rstat);
    if (st != PROC_OK || rstat.faulted || rstat.exit_code != 0) return false;

    const char *argv_iso[] = { "/bin/isolate" };
    process_result_t riso1;
    st = process_exec("/bin/isolate", 1, argv_iso, &riso1);
    if (st != PROC_OK || riso1.faulted || riso1.exit_code != 41) return false;
    process_result_t riso2;
    st = process_exec("/bin/isolate", 1, argv_iso, &riso2);
    if (st != PROC_OK || riso2.faulted || riso2.exit_code != 41) return false;
    if (riso1.address_space_generation == riso2.address_space_generation) return false;

    const char *argv_fd[] = { "/bin/fdleak" };
    process_result_t rfd1;
    st = process_exec("/bin/fdleak", 1, argv_fd, &rfd1);
    if (st != PROC_OK || rfd1.faulted || rfd1.exit_code != 0) return false;
    process_result_t rfd2;
    st = process_exec("/bin/fdleak", 1, argv_fd, &rfd2);
    if (st != PROC_OK || rfd2.faulted || rfd2.exit_code != 0) return false;

    const char *argv1[] = { "/bin/writetest" };
    process_result_t rw;
    st = process_exec("/bin/writetest", 1, argv1, &rw);
    if (st != PROC_OK || rw.faulted || rw.exit_code != 0) return false;
    const char *argv2[] = { "/bin/fscheck", "/disk0/hello.txt" };
    process_result_t rf;
    st = process_exec("/bin/fscheck", 2, argv2, &rf);
    if (st != PROC_OK || rf.faulted || rf.exit_code != 0) return false;
    const char *argv_exec[] = { "/bin/execcheck" };
    process_result_t rexec;
    st = process_exec("/bin/execcheck", 1, argv_exec, &rexec);
    if (st != PROC_OK || rexec.faulted || rexec.exit_code != 0 || strcmp(rexec.name, "/bin/fscheck") != 0) return false;
    const char *argv_execve[] = { "/bin/execvecheck" };
    process_result_t rexecve;
    st = process_exec("/bin/execvecheck", 1, argv_execve, &rexecve);
    if (st != PROC_OK || rexecve.faulted || rexecve.exit_code != 0 || strcmp(rexecve.name, "/bin/exectarget") != 0) return false;
    const char *argv_fdexec[] = { "/bin/execfdcheck" };
    process_result_t rfdexec;
    st = process_exec("/bin/execfdcheck", 1, argv_fdexec, &rfdexec);
    if (st != PROC_OK || rfdexec.faulted || rfdexec.exit_code != 0 || strcmp(rfdexec.name, "/bin/exectarget") != 0) return false;
    const char *argv_clo[] = { "/bin/execfdcheck", "cloexec" };
    process_result_t rclo;
    st = process_exec("/bin/execfdcheck", 2, argv_clo, &rclo);
    if (st != PROC_OK || rclo.faulted || rclo.exit_code != 0 || strcmp(rclo.name, "/bin/exectarget") != 0) return false;
    return true;
}
