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
    next_user_pid = 1;
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

static void process_init_root_cred(aurora_credinfo_t *cred) {
    if (!cred) return;
    memset(cred, 0, sizeof(*cred));
    cred->uid = AURORA_UID_ROOT;
    cred->euid = AURORA_UID_ROOT;
    cred->gid = AURORA_GID_ROOT;
    cred->egid = AURORA_GID_ROOT;
    cred->is_admin = 1u;
    cred->sudo_ttl = AURORA_SUDO_DEFAULT_TTL_TICKS;
    cred->umask = 022u;
    strncpy(cred->user, "root", sizeof(cred->user) - 1u);
}

static void process_inherit_cred(active_process_t *p) {
    if (!p) return;
    if (current_proc.active) p->cred = current_proc.cred;
    else process_init_root_cred(&p->cred);
}

static bool process_cred_is_root(const aurora_credinfo_t *cred) {
    return cred && cred->euid == AURORA_UID_ROOT;
}

static bool process_cred_can_access(const aurora_credinfo_t *cred, const vfs_stat_t *st, u32 need) {
    if (!cred || !st) return false;
    if (process_cred_is_root(cred)) return true;
    u32 mode = st->mode & 0777u;
    u32 bits = mode & 0007u;
    if (cred->euid == st->uid) bits = (mode >> 6) & 0007u;
    else if (cred->egid == st->gid) bits = (mode >> 3) & 0007u;
    return (bits & need) == need;
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

static bool pid_in_use(u32 pid) {
    if (!pid || (current_proc.active && current_proc.result.pid == pid)) return pid == 0 || current_proc.result.pid == pid;
    if (async_slots) {
        for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
            if (async_slots[i].active && async_slots[i].result.pid == pid) return true;
        }
    }
    return false;
}

static u32 alloc_user_pid(void) {
    for (usize tries = 0; tries < PROCESS_ASYNC_CAP + 2u; ++tries) {
        u32 pid = (u32)next_user_pid++;
        if (pid == 0) continue;
        if (!pid_in_use(pid)) return pid;
    }
    return 0;
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

static void release_process_fd_snapshot(active_process_t *p) {
    if (!p) return;
    syscall_release_user_handle_snapshot(p->fd_snapshot, sizeof(p->fd_snapshot));
    memset(p->fd_snapshot, 0, sizeof(p->fd_snapshot));
}

static void release_process_slot(active_process_t *p) {
    if (!p || !p->active) return;
    release_process_fd_snapshot(p);
    release_mappings(p);
    if (p->space.pml4_physical) vmm_space_destroy(&p->space);
    memset(p, 0, sizeof(*p));
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
    info->uid = p->cred.uid;
    info->euid = p->cred.euid;
    info->gid = p->cred.gid;
    info->egid = p->cred.egid;
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
    out->uid = current_proc.cred.uid;
    out->euid = current_proc.cred.euid;
    out->gid = current_proc.cred.gid;
    out->egid = current_proc.cred.egid;
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

bool process_current_credentials(aurora_credinfo_t *out) {
    if (!out || !current_proc.active) return false;
    *out = current_proc.cred;
    u64 now = pit_ticks();
    out->sudo_cached = (out->sudo_until > now) ? 1u : 0u;
    return true;
}

bool process_set_current_credentials(const aurora_credinfo_t *cred) {
    if (!cred || !current_proc.active) return false;
    current_proc.cred = *cred;
    current_proc.cred.user[sizeof(current_proc.cred.user) - 1u] = 0;
    (void)sync_current_to_slot();
    return true;
}

bool process_set_current_user(u32 uid, u32 gid, const char *name, bool admin) {
    if (!current_proc.active || !name || !name[0]) return false;
    memset(&current_proc.cred, 0, sizeof(current_proc.cred));
    current_proc.cred.uid = uid;
    current_proc.cred.euid = uid;
    current_proc.cred.gid = gid;
    current_proc.cred.egid = gid;
    current_proc.cred.is_admin = admin ? 1u : 0u;
    current_proc.cred.sudo_ttl = AURORA_SUDO_DEFAULT_TTL_TICKS;
    current_proc.cred.umask = 022u;
    strncpy(current_proc.cred.user, name, sizeof(current_proc.cred.user) - 1u);
    (void)sync_current_to_slot();
    return true;
}

bool process_set_current_euid(u32 euid) {
    if (!current_proc.active) return false;
    current_proc.cred.euid = euid;
    current_proc.cred.egid = (euid == AURORA_UID_ROOT) ? AURORA_GID_ROOT : current_proc.cred.gid;
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
        if (write && !(flags & VMM_WRITE)) {
            if (!(flags & VMM_COW) || !process_resolve_cow_page(&current_proc, addr)) return false;
            if (!ensure_process_user_page_writable(&current_proc, addr, &phys, &flags)) return false;
        }
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
        if (!ensure_process_user_page_writable(&current_proc, addr, &phys, &flags)) return false;
        aurora_rust_user_copy_step_t step_info;
        if (!aurora_rust_user_copy_step((u64)addr, size - done, &step_info) || !step_info.ok || step_info.chunk == 0) return false;
        usize n = step_info.chunk;
        memcpy((void *)phys, s + done, n);
        done += n;
    }
    return true;
}

