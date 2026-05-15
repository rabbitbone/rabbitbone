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
    child->process_group = current_proc.process_group;
    child->session_id = current_proc.session_id;
    child->signal_flags = current_proc.signal_flags & ~RABBITBONE_PROCESS_FLAG_STOPPED;
    child->signal_pending = 0;
    child->signal_blocked = current_proc.signal_blocked;
    child->signal_in_handler = false;
    memcpy(child->signal_actions, current_proc.signal_actions, sizeof(child->signal_actions));
    child->cred = current_proc.cred;
    strncpy(child->cwd, current_proc.cwd[0] ? current_proc.cwd : "/", sizeof(child->cwd) - 1u);
    child->result = current_proc.result;
    child->result.pid = alloc_user_pid();
    if (!child->result.pid) return false;
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
    child->vma_count = current_proc.vma_count;
    memcpy(child->vmas, current_proc.vmas, sizeof(current_proc.vmas));
    for (usize vi = 0; vi < child->vma_count; ++vi) {
        if (!process_retain_vma_backing(&child->vmas[vi])) {
            for (usize r = 0; r < vi; ++r) process_release_vma_backing(&child->vmas[r]);
            child->vma_count = 0;
            return false;
        }
    }
    child->mmap_cursor = current_proc.mmap_cursor;
    child->heap_base = current_proc.heap_base;
    child->heap_break = current_proc.heap_break;
    child->heap_limit = current_proc.heap_limit;
    for (usize i = 0; i < current_proc.mapping_count; ++i) {
        user_mapping_t *parent_mapping = &current_proc.mappings[i];
        if (!parent_mapping->phys) return false;
        uptr virt = parent_mapping->virt;
        uptr phys = parent_mapping->phys;
        u64 child_flags = parent_mapping->final_flags;
        bool shared_anon = process_mapping_is_shared_anon(&current_proc, virt);
        if (!shared_anon && (parent_mapping->final_flags & VMM_WRITE)) {
            u64 cow_flags = (parent_mapping->final_flags & ~VMM_WRITE) | VMM_COW;
            if (!vmm_space_protect_4k(&current_proc.space, virt, cow_flags)) return false;
            parent_mapping->final_flags = cow_flags;
            child_flags = cow_flags;
        }
        if (!memory_ref_page((void *)phys)) return false;
        if (!vmm_space_map_4k(&child->space, virt, phys, child_flags)) {
            (void)memory_unref_page((void *)phys);
            return false;
        }
        if (track_mapping(child, virt, phys, child_flags) != PROC_OK) {
            (void)vmm_space_unmap_4k(&child->space, virt);
            (void)memory_unref_page((void *)phys);
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


static void reparent_children(u32 old_parent, u32 new_parent) {
    if (!async_slots || !old_parent || old_parent == new_parent) return;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *child = &async_slots[i];
        if (child->active && child->parent_pid == old_parent) child->parent_pid = new_parent;
    }
}

static void finish_current(process_lifecycle_t state, process_status_t status, cpu_regs_t *regs) {
    if (regs) current_proc.regs = *regs;
    current_proc.state = state;
    current_proc.active = true;
    current_proc.result.finished_ticks = pit_ticks();
    reparent_children(current_proc.result.pid, current_proc.result.pid == 1u ? 0u : 1u);
    if (current_proc.parent_pid) (void)process_signal_send(current_proc.parent_pid, RABBITBONE_SIGCHLD);

    /*
     * A terminal process no longer owns its descriptor table.  Closing the
     * live table here matches normal process-exit semantics and makes pipe
     * readers observe HUP as soon as the writer exits, not only after wait()
     * reaps the child slot.  Without this, a parent that polls for HUP before
     * wait() can spin forever on a pipe whose only writer belongs to an
     * already-exited child.
     */
    syscall_close_all_user_handles();
    syscall_save_user_handles(current_proc.fd_snapshot, sizeof(current_proc.fd_snapshot));
    tty_forget_pid(current_proc.result.pid);
    syscall_forget_pid(current_proc.result.pid);

    release_mappings(&current_proc);
    if (current_proc.space.pml4_physical) vmm_space_destroy(&current_proc.space);

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
    bool reap_child = false;
    for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
        active_process_t *p = &async_slots[i];
        if (p->active && p->state == PROCESS_STATE_WAITING && p->wait_pid == pid) {
            process_info_t info;
            bool copied = false;
            if (child && process_state_terminal((u32)child->state) && p->wait_out_ptr) {
                fill_info_from_active(&info, child);
                copied = copy_to_process_user_space(p, p->wait_out_ptr, &info, sizeof(info));
                if (copied) reap_child = true;
            }
            p->regs.rax = copied ? 0u : (u64)-1ll;
            p->regs.rdx = copied ? 0u : (u64)VFS_ERR_INVAL;
            p->state = PROCESS_STATE_READY;
            p->wait_pid = 0;
            p->wait_out_ptr = 0;
        }
    }
    if (reap_child && child) release_process_slot(child);
}

