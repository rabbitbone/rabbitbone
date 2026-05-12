typedef struct process_kernel_test_context {
    bool active;
    active_process_t saved_current;
    active_process_t saved_exec_replacement;
    active_process_t saved_exec_old_proc;
    active_process_t *saved_async_slots;
    u32 saved_async_next_rr;
    i32 saved_current_async_slot;
    bool saved_async_scheduler_active;
    bool saved_reschedule_requested;
    bool saved_exit_requested;
    bool saved_fork_requested;
    bool saved_exec_requested;
    i32 saved_requested_exit_code;
    process_result_t saved_last_result;
    u64 saved_next_user_pid;
    u64 saved_next_address_space_generation;
    usize saved_process_table_len;
    usize saved_process_table_next;
    process_info_t *saved_process_table;
    u8 saved_user_handles[SYSCALL_USER_HANDLE_SNAPSHOT_BYTES];
    arch_user_return_context_t saved_arch_user_context;
    void *task_snapshot;
    usize task_snapshot_len;
    void *scheduler_snapshot;
    usize scheduler_snapshot_len;
} process_kernel_test_context_t;

static process_kernel_test_context_t *kernel_test_ctx;

static void process_free_partial_kernel_test_context(process_kernel_test_context_t *ctx, active_process_t *test_slots) {
    if (test_slots) kfree(test_slots);
    if (ctx) {
        if (ctx->task_snapshot) kfree(ctx->task_snapshot);
        if (ctx->scheduler_snapshot) kfree(ctx->scheduler_snapshot);
        if (ctx->saved_process_table) kfree(ctx->saved_process_table);
        kfree(ctx);
    }
}

bool process_enter_kernel_test_context(void) {
    if (kernel_test_ctx || !current_proc_ptr || !async_slots || !exec_replacement || !exec_old_proc) return false;
    if (exec_requested || fork_requested || exit_requested || reschedule_requested) return false;

    process_kernel_test_context_t *ctx = (process_kernel_test_context_t *)kmalloc(sizeof(*ctx));
    active_process_t *test_slots = (active_process_t *)kmalloc(sizeof(active_process_t) * PROCESS_ASYNC_CAP);
    if (!ctx || !test_slots) {
        process_free_partial_kernel_test_context(ctx, test_slots);
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    memset(test_slots, 0, sizeof(active_process_t) * PROCESS_ASYNC_CAP);

    ctx->task_snapshot_len = task_snapshot_size();
    ctx->scheduler_snapshot_len = scheduler_snapshot_size();
    ctx->task_snapshot = kmalloc(ctx->task_snapshot_len);
    ctx->scheduler_snapshot = kmalloc(ctx->scheduler_snapshot_len);
    ctx->saved_process_table = (process_info_t *)kmalloc(sizeof(process_info_t) * PROCESS_TABLE_CAP);
    if (!ctx->task_snapshot || !ctx->scheduler_snapshot || !ctx->saved_process_table) {
        process_free_partial_kernel_test_context(ctx, test_slots);
        return false;
    }
    if (!task_snapshot_save(ctx->task_snapshot, ctx->task_snapshot_len) ||
        !scheduler_snapshot_save(ctx->scheduler_snapshot, ctx->scheduler_snapshot_len)) {
        process_free_partial_kernel_test_context(ctx, test_slots);
        return false;
    }

    ctx->saved_current = current_proc;
    ctx->saved_exec_replacement = *exec_replacement;
    ctx->saved_exec_old_proc = *exec_old_proc;
    ctx->saved_async_slots = async_slots;
    ctx->saved_async_next_rr = async_next_rr;
    ctx->saved_current_async_slot = current_async_slot;
    ctx->saved_async_scheduler_active = async_scheduler_active;
    ctx->saved_reschedule_requested = reschedule_requested;
    ctx->saved_exit_requested = exit_requested;
    ctx->saved_fork_requested = fork_requested;
    ctx->saved_exec_requested = exec_requested;
    ctx->saved_requested_exit_code = requested_exit_code;
    ctx->saved_last_result = last_result;
    ctx->saved_next_user_pid = next_user_pid;
    ctx->saved_next_address_space_generation = next_address_space_generation;
    ctx->saved_process_table_len = process_table_len;
    ctx->saved_process_table_next = process_table_next;
    if (process_table) memcpy(ctx->saved_process_table, process_table, sizeof(process_info_t) * PROCESS_TABLE_CAP);
    else memset(ctx->saved_process_table, 0, sizeof(process_info_t) * PROCESS_TABLE_CAP);
    syscall_save_user_handles(ctx->saved_user_handles, sizeof(ctx->saved_user_handles));
    arch_user_context_save(&ctx->saved_arch_user_context);

    if (current_proc.active) (void)sync_current_to_slot();

    vmm_switch_kernel();
    memset(current_proc_ptr, 0, sizeof(*current_proc_ptr));
    syscall_reset_user_handles();
    async_slots = test_slots;
    async_next_rr = 0;
    current_async_slot = -1;
    async_scheduler_active = false;
    reschedule_requested = false;
    exit_requested = false;
    fork_requested = false;
    exec_requested = false;
    requested_exit_code = 0;
    memset(exec_replacement, 0, sizeof(*exec_replacement));
    memset(exec_old_proc, 0, sizeof(*exec_old_proc));
    process_table_len = 0;
    process_table_next = 0;
    if (process_table) memset(process_table, 0, sizeof(process_info_t) * PROCESS_TABLE_CAP);
    task_reset_for_test();
    scheduler_reset_for_test();

    ctx->active = true;
    kernel_test_ctx = ctx;
    KLOG(LOG_INFO, "ktest", "entered isolated full-kernel test context");
    return true;
}

void process_leave_kernel_test_context(void) {
    process_kernel_test_context_t *ctx = kernel_test_ctx;
    if (!ctx || !ctx->active) return;

    active_process_t *test_slots = async_slots;
    vmm_switch_kernel();
    if (test_slots) {
        for (usize i = 0; i < PROCESS_ASYNC_CAP; ++i) {
            if (test_slots[i].active) release_process_slot(&test_slots[i]);
        }
        kfree(test_slots);
    }

    (void)task_snapshot_restore(ctx->task_snapshot, ctx->task_snapshot_len);
    (void)scheduler_snapshot_restore(ctx->scheduler_snapshot, ctx->scheduler_snapshot_len);

    async_slots = ctx->saved_async_slots;
    async_next_rr = ctx->saved_async_next_rr;
    current_async_slot = ctx->saved_current_async_slot;
    async_scheduler_active = ctx->saved_async_scheduler_active;
    reschedule_requested = ctx->saved_reschedule_requested;
    exit_requested = ctx->saved_exit_requested;
    fork_requested = ctx->saved_fork_requested;
    exec_requested = ctx->saved_exec_requested;
    requested_exit_code = ctx->saved_requested_exit_code;
    last_result = ctx->saved_last_result;
    next_user_pid = ctx->saved_next_user_pid;
    next_address_space_generation = ctx->saved_next_address_space_generation;
    process_table_len = ctx->saved_process_table_len;
    process_table_next = ctx->saved_process_table_next;
    if (process_table && ctx->saved_process_table) memcpy(process_table, ctx->saved_process_table, sizeof(process_info_t) * PROCESS_TABLE_CAP);
    current_proc = ctx->saved_current;
    *exec_replacement = ctx->saved_exec_replacement;
    *exec_old_proc = ctx->saved_exec_old_proc;
    syscall_load_user_handles(ctx->saved_user_handles, sizeof(ctx->saved_user_handles));
    arch_user_context_restore(&ctx->saved_arch_user_context);

    if (current_proc.active && current_proc.space.pml4_physical) vmm_switch_space(&current_proc.space);
    else vmm_switch_kernel();

    kernel_test_ctx = 0;
    ctx->active = false;
    if (ctx->task_snapshot) kfree(ctx->task_snapshot);
    if (ctx->scheduler_snapshot) kfree(ctx->scheduler_snapshot);
    if (ctx->saved_process_table) kfree(ctx->saved_process_table);
    kfree(ctx);
    KLOG(LOG_INFO, "ktest", "left isolated full-kernel test context");
}
