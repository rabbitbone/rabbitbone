
static bool process_elf_phdr_type_supported(u32 type) {
    switch (type) {
        case ELF64_PT_NULL:
        case ELF64_PT_LOAD:
        case ELF64_PT_NOTE:
        case ELF64_PT_GNU_STACK:
            return true;
        case ELF64_PT_DYNAMIC:
        case ELF64_PT_INTERP:
        default:
            return false;
    }
}

static process_status_t process_segment_page_span(const elf64_phdr_t *ph, uptr *start_out, uptr *end_out, usize *pages_out) {
    if (!ph || !start_out || !end_out || !pages_out) return PROC_ERR_INVAL;
    u64 seg_end64 = 0;
    if (__builtin_add_overflow(ph->p_vaddr, ph->p_memsz, &seg_end64)) return PROC_ERR_RANGE;
    uptr start = RABBITBONE_ALIGN_DOWN((uptr)ph->p_vaddr, PAGE_SIZE);
    u64 aligned_end = 0;
    if (!rabbitbone_align_up_u64_checked(seg_end64, PAGE_SIZE, &aligned_end)) return PROC_ERR_RANGE;
    uptr end = (uptr)aligned_end;
    if ((u64)end != aligned_end || end <= start || !user_addr_range_valid(start, (usize)(end - start))) return PROC_ERR_RANGE;
    usize pages = (usize)((end - start) / PAGE_SIZE);
    if (pages == 0 || pages > USER_MAX_MAPPINGS) return PROC_ERR_RANGE;
    *start_out = start;
    *end_out = end;
    *pages_out = pages;
    return PROC_OK;
}

static bool user_stack_reserve(uptr stack_base, uptr *sp, usize size, uptr *addr_out) {
    if (!sp) return false;
    uptr cur = *sp;
    if (cur < stack_base) return false;
    if (size > (usize)(cur - stack_base)) return false;
    cur -= (uptr)size;
    *sp = cur;
    if (addr_out) *addr_out = cur;
    return true;
}

static bool user_stack_align_down(uptr stack_base, uptr *sp, uptr align) {
    if (!sp || align == 0 || (align & (align - 1u)) != 0) return false;
    uptr cur = *sp & ~(align - 1u);
    if (cur < stack_base) return false;
    *sp = cur;
    return true;
}

static process_status_t load_elf_image(active_process_t *p, const char *path, u64 *entry_out) {
    if (!p || !process_cred_can_traverse_parent(&p->cred, path)) return PROC_ERR_IO;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != VFS_OK || st.type != VFS_NODE_FILE) return PROC_ERR_IO;
    if (!process_cred_can_access(&p->cred, &st, 01u)) return PROC_ERR_IO;
    elf64_ehdr_t eh;
    usize got = 0;
    if (vfs_read(path, 0, &eh, sizeof(eh), &got) != VFS_OK || got != sizeof(eh)) return PROC_ERR_IO;
    elf_status_t ev = elf64_validate_header(&eh, st.size);
    if (ev != ELF_OK || eh.e_phnum == 0) return PROC_ERR_FORMAT;
    if (eh.e_type != ELF64_ET_EXEC) return PROC_ERR_FORMAT;
    if (eh.e_entry < USER_IMAGE_BASE || eh.e_entry >= USER_SPACE_LIMIT) return PROC_ERR_RANGE;
    if (eh.e_phnum > 32u) return PROC_ERR_RANGE;

    bool entry_in_exec_segment = false;
    usize image_first_mapping = p->mapping_count;
    u64 loaded_starts[32];
    u64 loaded_ends[32];
    memset(loaded_starts, 0, sizeof(loaded_starts));
    memset(loaded_ends, 0, sizeof(loaded_ends));
    u16 loaded_count = 0;
    for (u16 i = 0; i < eh.e_phnum; ++i) {
        elf64_phdr_t ph;
        got = 0;
        u64 ph_index_off = 0;
        u64 phoff = 0;
        if (__builtin_mul_overflow((u64)i, (u64)eh.e_phentsize, &ph_index_off) ||
            __builtin_add_overflow(eh.e_phoff, ph_index_off, &phoff)) return PROC_ERR_RANGE;
        if (vfs_read(path, phoff, &ph, sizeof(ph), &got) != VFS_OK || got != sizeof(ph)) return PROC_ERR_IO;
        if (!process_elf_phdr_type_supported(ph.p_type)) return PROC_ERR_FORMAT;
        if (ph.p_type != ELF64_PT_LOAD) continue;
        elf_status_t sv = elf64_validate_load_segment(&ph, st.size, USER_IMAGE_BASE, USER_SPACE_LIMIT);
        if (sv == ELF_ERR_UNSUPPORTED) return PROC_ERR_FORMAT;
        if (sv == ELF_ERR_RANGE) return PROC_ERR_RANGE;
        if (sv != ELF_OK) return PROC_ERR_FORMAT;
        if (ph.p_memsz == 0) continue;
        u64 seg_end = 0;
        if (__builtin_add_overflow(ph.p_vaddr, ph.p_memsz, &seg_end)) return PROC_ERR_RANGE;
        for (u16 j = 0; j < loaded_count; ++j) {
            if (elf64_ranges_overlap(ph.p_vaddr, seg_end, loaded_starts[j], loaded_ends[j])) return PROC_ERR_FORMAT;
        }
        loaded_starts[loaded_count] = ph.p_vaddr;
        loaded_ends[loaded_count] = seg_end;
        ++loaded_count;
        if ((ph.p_flags & ELF64_PF_X) && eh.e_entry >= ph.p_vaddr && eh.e_entry < seg_end) entry_in_exec_segment = true;

        uptr start = 0;
        uptr end = 0;
        usize pages = 0;
        process_status_t span_status = process_segment_page_span(&ph, &start, &end, &pages);
        if (span_status != PROC_OK) return span_status;
        if (pages > USER_MAX_MAPPINGS - p->mapping_count) return PROC_ERR_NOMEM;
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
    for (usize mi = image_first_mapping; mi < p->mapping_count;) {
        uptr run_start = p->mappings[mi].virt;
        uptr run_end = 0;
        if (__builtin_add_overflow(run_start, (uptr)PAGE_SIZE, &run_end)) return PROC_ERR_RANGE;
        u32 run_prot = process_prot_from_flags(p->mappings[mi].final_flags);
        ++mi;
        while (mi < p->mapping_count && p->mappings[mi].virt == run_end && process_prot_from_flags(p->mappings[mi].final_flags) == run_prot) {
            if (__builtin_add_overflow(run_end, (uptr)PAGE_SIZE, &run_end)) return PROC_ERR_RANGE;
            ++mi;
        }
        ps = process_add_vma(p, run_start, run_end, run_prot, 0, USER_VMA_IMAGE);
        if (ps != PROC_OK) return ps;
    }
    ps = process_init_heap_for(p, image_first_mapping);
    if (ps != PROC_OK) return ps;
    *entry_out = eh.e_entry;
    return PROC_OK;
}

static uptr process_stack_top_for(const active_process_t *p) {
    u64 mix = pit_ticks();
    if (p) {
        mix ^= ((u64)p->result.pid << 17);
        mix ^= (p->result.address_space_generation << 7);
    }
    usize jitter = USER_STACK_ASLR_PAGES ? (usize)(mix % USER_STACK_ASLR_PAGES) : 0;
    return USER_STACK_TOP - (uptr)jitter * PAGE_SIZE;
}

static process_status_t build_user_stack(active_process_t *p, int argc, const char *const *argv, int envc, const char *const *envp, u64 *rsp_out, u64 *argv_out, u64 *envp_out) {
    if (argc < 0 || argc > (int)PROCESS_ARG_MAX || envc < 0 || envc > (int)PROCESS_ENV_MAX) return PROC_ERR_INVAL;
    if ((argc > 0 && !argv) || (envc > 0 && !envp) || !rsp_out || !argv_out || !envp_out) return PROC_ERR_INVAL;
    uptr stack_top = process_stack_top_for(p);
    if (p) p->result.user_stack_top = stack_top;
    uptr stack_size = USER_STACK_PAGES * PAGE_SIZE;
    if (stack_top < stack_size + USER_STACK_GUARD_PAGES * PAGE_SIZE) return PROC_ERR_RANGE;
    uptr stack_base = stack_top - stack_size;
    uptr guard_base = stack_base - USER_STACK_GUARD_PAGES * PAGE_SIZE;
    if (!user_addr_range_valid(guard_base, USER_STACK_GUARD_PAGES * PAGE_SIZE) || find_mapping(p, guard_base)) return PROC_ERR_RANGE;
    for (usize i = 0; i < USER_STACK_PAGES; ++i) {
        process_status_t st = map_user_page(p, stack_base + i * PAGE_SIZE, VMM_WRITE | VMM_NX);
        if (st != PROC_OK) return st;
    }
    process_status_t stack_vma_status = process_add_vma(p, stack_base, stack_top, RABBITBONE_PROT_READ | RABBITBONE_PROT_WRITE, RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE, USER_VMA_STACK);
    if (stack_vma_status != PROC_OK) return stack_vma_status;
    uptr sp = stack_top;
    u64 arg_ptrs[PROCESS_ARG_MAX + 1u];
    u64 env_ptrs[PROCESS_ENV_MAX + 1u];
    memset(arg_ptrs, 0, sizeof(arg_ptrs));
    memset(env_ptrs, 0, sizeof(env_ptrs));
    for (int i = envc - 1; i >= 0; --i) {
        const char *s = envp[i] ? envp[i] : "";
        usize len = strnlen(s, USER_ARG_STRING_MAX);
        if (len >= USER_ARG_STRING_MAX) return PROC_ERR_RANGE;
        uptr dst = 0;
        if (!user_stack_reserve(stack_base, &sp, len + 1u, &dst)) return PROC_ERR_RANGE;
        process_status_t cs = copy_to_user_space(p, dst, s, len + 1u);
        if (cs != PROC_OK) return cs;
        env_ptrs[i] = dst;
    }
    for (int i = argc - 1; i >= 0; --i) {
        const char *s = argv[i] ? argv[i] : "";
        usize len = strnlen(s, USER_ARG_STRING_MAX);
        if (len >= USER_ARG_STRING_MAX) return PROC_ERR_RANGE;
        uptr dst = 0;
        if (!user_stack_reserve(stack_base, &sp, len + 1u, &dst)) return PROC_ERR_RANGE;
        process_status_t cs = copy_to_user_space(p, dst, s, len + 1u);
        if (cs != PROC_OK) return cs;
        arg_ptrs[i] = dst;
    }
    if (!user_stack_align_down(stack_base, &sp, 16u)) return PROC_ERR_RANGE;
    usize env_bytes = 0;
    if (__builtin_mul_overflow((usize)(envc + 1), sizeof(u64), &env_bytes)) return PROC_ERR_RANGE;
    uptr env_user = 0;
    if (!user_stack_reserve(stack_base, &sp, env_bytes, &env_user)) return PROC_ERR_RANGE;
    process_status_t ps = copy_to_user_space(p, env_user, env_ptrs, env_bytes);
    if (ps != PROC_OK) return ps;
    usize arg_bytes = 0;
    if (__builtin_mul_overflow((usize)(argc + 1), sizeof(u64), &arg_bytes)) return PROC_ERR_RANGE;
    uptr argv_user = 0;
    if (!user_stack_reserve(stack_base, &sp, arg_bytes, &argv_user)) return PROC_ERR_RANGE;
    ps = copy_to_user_space(p, argv_user, arg_ptrs, arg_bytes);
    if (ps != PROC_OK) return ps;
    if (!user_stack_align_down(stack_base, &sp, 16u)) return PROC_ERR_RANGE;
    *argv_out = argv_user;
    *envp_out = env_user;
    *rsp_out = sp;
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
    p->home_cpu = smp_current_cpu_id();
    if (p->home_cpu >= SMP_MAX_CPUS) p->home_cpu = 0;
    p->last_cpu = p->home_cpu;
    p->affinity_mask = smp_started_cpu_mask();
    if (p->affinity_mask == 0) p->affinity_mask = 1u;
    p->parent_pid = parent_pid;
    if (current_proc.active) {
        p->process_group = current_proc.process_group ? current_proc.process_group : current_proc.result.pid;
        p->session_id = current_proc.session_id ? current_proc.session_id : current_proc.result.pid;
    }
    process_inherit_cred(p);
    if (current_proc.active && current_proc.cwd[0]) strlcpy(p->cwd, current_proc.cwd, sizeof(p->cwd));
    else strlcpy(p->cwd, "/", sizeof(p->cwd));
    p->result.pid = alloc_user_pid();
    if (!p->result.pid) { vmm_space_destroy(&p->space); memset(p, 0, sizeof(*p)); return PROC_ERR_BUSY; }
    if (!p->process_group) p->process_group = p->result.pid;
    if (!p->session_id) p->session_id = p->result.pid;
    if (!tty_session_id) tty_session_id = p->session_id;
    if (!tty_foreground_pgrp) tty_foreground_pgrp = p->process_group;
    p->signal_flags = (p->result.pid == p->session_id) ? RABBITBONE_PROCESS_FLAG_SESSION_LEADER : 0u;
    process_signal_init(p);
    p->result.started_ticks = pit_ticks();
    p->result.address_space = p->space.pml4_physical;
    p->result.address_space_generation = alloc_address_space_generation();
    syscall_prepare_user_handle_snapshot(p->fd_snapshot, sizeof(p->fd_snapshot));

    process_exec_plan_t plan;
    process_status_t st = resolve_exec_plan(p, path, argc, argv, &plan);
    if (st == PROC_OK) {
        strlcpy(p->result.name, plan.image_path, sizeof(p->result.name));
    } else {
        strlcpy(p->result.name, path, sizeof(p->result.name));
    }

    u64 entry = 0;
    if (st == PROC_OK) st = load_elf_image(p, plan.image_path, &entry);
    if (st == PROC_OK) {
        u64 user_rsp = 0;
        u64 user_argv = 0;
        u64 user_envp = 0;
        st = build_user_stack(p, plan.argc, plan.argv, envc, envp, &user_rsp, &user_argv, &user_envp);
        if (st == PROC_OK) {
            p->result.entry = entry;
            init_user_regs(&p->regs, entry, user_rsp, (u64)plan.argc, user_argv, user_envp);
            KLOG(LOG_INFO, "process", "prepared pid=%u path=%s pml4=%p entry=%p rsp=%p argc=%d pages=%llu",
                 p->result.pid, plan.image_path, (void *)p->space.pml4_physical, (void *)(uptr)entry, (void *)(uptr)user_rsp, plan.argc,
                 (unsigned long long)p->result.mapped_pages);
            return PROC_OK;
        }
    }

    p->state = PROCESS_STATE_LOAD_ERROR;
    p->result.finished_ticks = pit_ticks();
    last_result = p->result;
    record_process_result(&p->result, st, p->result.started_ticks, p->result.finished_ticks);
    release_mappings(p);
    process_drop_vmas(p);
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
    dst->home_cpu = current_proc.home_cpu;
    dst->last_cpu = smp_current_cpu_id();
    dst->affinity_mask = current_proc.affinity_mask ? current_proc.affinity_mask : 1u;
    dst->migration_count = current_proc.migration_count;
    dst->parent_pid = current_proc.parent_pid;
    dst->process_group = current_proc.process_group;
    dst->session_id = current_proc.session_id;
    dst->signal_flags = current_proc.signal_flags & RABBITBONE_PROCESS_FLAG_SESSION_LEADER;
    dst->signal_blocked = current_proc.signal_blocked;
    memcpy(dst->signal_actions, current_proc.signal_actions, sizeof(dst->signal_actions));
    process_signal_exec_reset(dst);
    dst->cred = current_proc.cred;
    strlcpy(dst->cwd, current_proc.cwd[0] ? current_proc.cwd : "/", sizeof(dst->cwd));
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

    process_exec_plan_t plan;
    process_status_t st = resolve_exec_plan(dst, path, argc, argv, &plan);
    if (st == PROC_OK) strlcpy(dst->result.name, plan.image_path, sizeof(dst->result.name));
    else strlcpy(dst->result.name, path, sizeof(dst->result.name));

    u64 entry = 0;
    if (st == PROC_OK) st = load_elf_image(dst, plan.image_path, &entry);
    if (st == PROC_OK) {
        u64 user_rsp = 0;
        u64 user_argv = 0;
        u64 user_envp = 0;
        st = build_user_stack(dst, plan.argc, plan.argv, envc, envp, &user_rsp, &user_argv, &user_envp);
        if (st == PROC_OK) {
            dst->result.entry = entry;
            init_user_regs(&dst->regs, entry, user_rsp, (u64)plan.argc, user_argv, user_envp);
            syscall_save_user_handles(dst->fd_snapshot, sizeof(dst->fd_snapshot));
            KLOG(LOG_INFO, "process", "exec prepared pid=%u path=%s pml4=%p entry=%p rsp=%p argc=%d pages=%llu",
                 dst->result.pid, plan.image_path, (void *)dst->space.pml4_physical, (void *)(uptr)entry, (void *)(uptr)user_rsp, plan.argc,
                 (unsigned long long)dst->result.mapped_pages);
            return PROC_OK;
        }
    }
    release_mappings(dst);
    process_drop_vmas(dst);
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

    syscall_close_user_handles_with_flags(RABBITBONE_FD_CLOEXEC);
    current_proc = *exec_replacement;
    memset(exec_replacement, 0, sizeof(*exec_replacement));
    exec_requested = false;
    current_proc.state = PROCESS_STATE_RUNNING;
    syscall_save_user_handles(current_proc.fd_snapshot, sizeof(current_proc.fd_snapshot));
    if (regs) *regs = current_proc.regs;
    (void)sync_current_to_slot();
    vmm_switch_space(&current_proc.space);
    release_mappings(exec_old_proc);
    process_drop_vmas(exec_old_proc);
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
        process_drop_vmas(exec_replacement);
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

