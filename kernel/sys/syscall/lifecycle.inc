static void handle_release_resource(sys_handle_t *h);
static bool handle_retain_resource(const sys_handle_t *h);
static void close_handle(sys_handle_t *handles, usize h);
static sys_file_t *file_by_id(u32 id);
static sys_file_t *handle_file(sys_handle_t *h);
static sys_pipe_t *alloc_pipe(void);
static void release_pipe(sys_pipe_t *p);
static bool copy_path_arg(u64 user_or_kernel_ptr, char *out);

void syscall_reset_user_handles(void) {
    memset(user_handles, 0, SYSCALL_HANDLE_TABLE_BYTES);
    init_stdio_handles(user_handles);
}

void syscall_prepare_user_handle_snapshot(void *dst, usize dst_size) {
    if (!dst || dst_size < SYSCALL_HANDLE_TABLE_BYTES) return;
    sys_handle_t tmp[SYSCALL_MAX_HANDLES];
    memset(tmp, 0, sizeof(tmp));
    init_stdio_handles(tmp);
    memcpy(dst, tmp, sizeof(tmp));
}

usize syscall_user_handle_snapshot_size(void) {
    return SYSCALL_HANDLE_TABLE_BYTES;
}

void syscall_save_user_handles(void *dst, usize dst_size) {
    if (!dst || dst_size < SYSCALL_HANDLE_TABLE_BYTES) return;
    memcpy(dst, user_handles, SYSCALL_HANDLE_TABLE_BYTES);
}

bool syscall_retain_user_handle_snapshot(const void *src, usize src_size) {
    if (!src || src_size < SYSCALL_HANDLE_TABLE_BYTES) return false;
    const sys_handle_t *handles = (const sys_handle_t *)src;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used && !handle_retain_resource(&handles[i])) {
            for (usize j = 0; j < i; ++j) {
                if (handles[j].used) {
                    sys_handle_t tmp = handles[j];
                    handle_release_resource(&tmp);
                }
            }
            return false;
        }
    }
    return true;
}

void syscall_release_user_handle_snapshot(void *src, usize src_size) {
    if (!src || src_size < SYSCALL_HANDLE_TABLE_BYTES) return;
    sys_handle_t *handles = (sys_handle_t *)src;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (handles[i].used) close_handle(handles, i);
    }
}

void syscall_close_user_handles_with_flags(u32 flags) {
    if (!flags) return;
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (user_handles[i].used && (user_handles[i].flags & flags)) close_handle(user_handles, i);
    }
}


void syscall_forget_pid(u32 pid) {
    if (!pid || !initialized) return;
    u64 flags = spin_lock_irqsave(&user_log_lock);
    for (usize i = 0; i < SYSCALL_USER_LOG_SLOTS; ++i) {
        if (user_log_states[i].used && user_log_states[i].pid == pid) {
            memset(&user_log_states[i], 0, sizeof(user_log_states[i]));
            break;
        }
    }
    spin_unlock_irqrestore(&user_log_lock, flags);
}

void syscall_close_all_user_handles(void) {
    for (usize i = 0; i < SYSCALL_MAX_HANDLES; ++i) {
        if (user_handles[i].used) close_handle(user_handles, i);
    }
}

bool syscall_load_user_handles(const void *src, usize src_size) {
    if (!src || src_size < SYSCALL_HANDLE_TABLE_BYTES) return false;
    memcpy(user_handles, src, SYSCALL_HANDLE_TABLE_BYTES);
    return true;
}

void syscall_init(void) {
    if (!kernel_handles) {
        kernel_handles = (sys_handle_t *)kmalloc(SYSCALL_HANDLE_TABLE_BYTES);
        if (!kernel_handles) PANIC("syscall kernel handle table allocation failed");
    }
    if (!user_handles) {
        user_handles = (sys_handle_t *)kmalloc(SYSCALL_HANDLE_TABLE_BYTES);
        if (!user_handles) PANIC("syscall user handle table allocation failed");
    }
    if (!files) {
        files = (sys_file_t *)kmalloc(sizeof(sys_file_t) * SYSCALL_FILE_CAP);
        if (!files) PANIC("syscall open-file table allocation failed");
    }
    if (!pipes) {
        pipes = (sys_pipe_t *)kmalloc(SYSCALL_PIPE_TABLE_BYTES);
        if (!pipes) PANIC("syscall pipe table allocation failed");
    }
    memset(kernel_handles, 0, SYSCALL_HANDLE_TABLE_BYTES);
    memset(user_handles, 0, SYSCALL_HANDLE_TABLE_BYTES);
    init_stdio_handles(kernel_handles);
    init_stdio_handles(user_handles);
    memset(files, 0, sizeof(sys_file_t) * SYSCALL_FILE_CAP);
    memset(pipes, 0, SYSCALL_PIPE_TABLE_BYTES);
    memset(user_log_states, 0, sizeof(user_log_states));
    spinlock_init(&file_table_lock);
    spinlock_init(&pipe_table_lock);
    spinlock_init(&user_log_lock);
    next_file_id = 1u;
    next_pipe_id = 1u;
    memset(kernel_cwd, 0, sizeof(kernel_cwd));
    kernel_cwd[0] = '/';
    initialized = true;
    KLOG(LOG_INFO, "syscall", "Rust syscall dispatcher initialized handles=%u", SYSCALL_MAX_HANDLES);
}

const char *syscall_name(u64 no) {
    return (const char *)rabbitbone_rust_syscall_name(no);
}

static sys_file_t *file_by_id(u32 id) {
    if (!id) return 0;
    for (usize i = 0; i < SYSCALL_FILE_CAP; ++i) {
        if (files[i].used && files[i].id == id) return &files[i];
    }
    return 0;
}

static sys_file_t *handle_file(sys_handle_t *h) {
    if (!h || !h->used || h->kind != SYS_HANDLE_VFS || !h->file_id) return 0;
    return file_by_id(h->file_id);
}

