static i64 process_status_to_vfs_error(process_status_t st) {
    switch (st) {
        case PROC_OK: return 0;
        case PROC_ERR_INVAL: return VFS_ERR_INVAL;
        case PROC_ERR_NOMEM: return VFS_ERR_NOMEM;
        case PROC_ERR_RANGE: return VFS_ERR_INVAL;
        case PROC_ERR_IO: return VFS_ERR_IO;
        case PROC_ERR_FORMAT: return VFS_ERR_IO;
        case PROC_ERR_FAULT: return VFS_ERR_IO;
        case PROC_ERR_BUSY: return VFS_ERR_NOSPC;
        default: return VFS_ERR_IO;
    }
}

static bool copy_argv_vector(u64 argv_ptr, u32 argc, char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX], const char *argv[PROCESS_ARG_MAX]) {
    if (argc == 0 || argc > PROCESS_ARG_MAX || !argv_ptr) return false;
    for (u32 i = 0; i < argc; ++i) {
        u64 arg_ptr = 0;
        u64 slot_ptr = 0;
        if (!add_user_ptr(argv_ptr, (usize)i * sizeof(u64), &slot_ptr) || !copy_in_buf(&arg_ptr, slot_ptr, sizeof(arg_ptr)) || !arg_ptr) return false;
        if (!copy_string_arg(arg_ptr, storage[i], SYSCALL_PATH_MAX)) return false;
        if (i == 0 && storage[i][0] == 0) return false;
        argv[i] = storage[i];
    }
    return true;
}


static bool copy_env_vector(u64 env_ptr, u32 envc, char storage[PROCESS_ENV_MAX][SYSCALL_PATH_MAX], const char *envp[PROCESS_ENV_MAX]) {
    if (envc > PROCESS_ENV_MAX) return false;
    if (envc == 0) return true;
    if (!env_ptr) return false;
    for (u32 i = 0; i < envc; ++i) {
        u64 item_ptr = 0;
        u64 slot_ptr = 0;
        if (!add_user_ptr(env_ptr, (usize)i * sizeof(u64), &slot_ptr) || !copy_in_buf(&item_ptr, slot_ptr, sizeof(item_ptr)) || !item_ptr) return false;
        if (!copy_string_arg(item_ptr, storage[i], SYSCALL_PATH_MAX)) return false;
        if (storage[i][0] == 0) return false;
        envp[i] = storage[i];
    }
    return true;
}

syscall_result_t rabbitbone_sys_spawn(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    const char *argv[] = { path };
    u32 pid = 0;
    process_status_t st;
    if (process_async_scheduler_active()) {
        st = process_spawn_async(path, 1, argv, &pid);
    } else {
        process_result_t result;
        st = process_spawn(path, 1, argv, &pid, &result);
    }
    return st == PROC_OK ? ok((i64)pid) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_spawnv(u64 path_ptr, u64 argc64, u64 argv_ptr) {
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    memset(storage, 0, sizeof(storage));
    memset(argv, 0, sizeof(argv));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, storage, argv)) return err(VFS_ERR_INVAL);
    u32 pid = 0;
    process_status_t st;
    if (process_async_scheduler_active()) {
        st = process_spawn_async(path, (int)argc64, argv, &pid);
    } else {
        process_result_t result;
        st = process_spawn(path, (int)argc64, argv, &pid, &result);
    }
    return st == PROC_OK ? ok((i64)pid) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_exec(u64 path_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    const char *argv[] = { path };
    process_status_t st = process_request_exec(path, 1, argv);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_execv(u64 path_ptr, u64 argc64, u64 argv_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    memset(storage, 0, sizeof(storage));
    memset(argv, 0, sizeof(argv));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, storage, argv)) return err(VFS_ERR_INVAL);
    process_status_t st = process_request_exec(path, (int)argc64, argv);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_execve(u64 path_ptr, u64 argc64, u64 argv_ptr, u64 envc64, u64 envp_ptr) {
    if (!process_async_scheduler_active()) return err(VFS_ERR_UNSUPPORTED);
    if (argc64 == 0 || argc64 > PROCESS_ARG_MAX || !argv_ptr || envc64 > PROCESS_ENV_MAX || (envc64 && !envp_ptr)) return err(VFS_ERR_INVAL);
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    char argv_storage[PROCESS_ARG_MAX][SYSCALL_PATH_MAX];
    const char *argv[PROCESS_ARG_MAX];
    char env_storage[PROCESS_ENV_MAX][SYSCALL_PATH_MAX];
    const char *envp[PROCESS_ENV_MAX];
    memset(argv_storage, 0, sizeof(argv_storage));
    memset(argv, 0, sizeof(argv));
    memset(env_storage, 0, sizeof(env_storage));
    memset(envp, 0, sizeof(envp));
    if (!copy_argv_vector(argv_ptr, (u32)argc64, argv_storage, argv)) return err(VFS_ERR_INVAL);
    if (!copy_env_vector(envp_ptr, (u32)envc64, env_storage, envp)) return err(VFS_ERR_INVAL);
    process_status_t st = process_request_execve(path, (int)argc64, argv, (int)envc64, envp);
    return st == PROC_OK ? ok(0) : err(process_status_to_vfs_error(st));
}

syscall_result_t rabbitbone_sys_chdir(u64 path_ptr) {
    char path[SYSCALL_PATH_MAX];
    if (!copy_path_arg(path_ptr, path)) return err(VFS_ERR_INVAL);
    vfs_stat_t st;
    vfs_status_t vs = vfs_stat(path, &st);
    if (vs != VFS_OK) return err(vs);
    if (st.type != VFS_NODE_DIR) return err(VFS_ERR_NOTDIR);
    return syscall_set_cwd(path) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_getcwd(u64 out_ptr, u64 size64) {
    if (!out_ptr || size64 == 0 || size64 > SYSCALL_PATH_MAX || (usize)size64 != size64) return err(VFS_ERR_INVAL);
    char cwd[SYSCALL_PATH_MAX];
    if (!syscall_get_cwd(cwd, sizeof(cwd))) return err(VFS_ERR_INVAL);
    usize n = strlen(cwd) + 1u;
    if (n > (usize)size64) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, cwd, n) ? ok((i64)(n - 1u)) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_wait(u64 pid64, u64 out_ptr) {
    if (!out_ptr || pid64 > 0xffffffffull) return err(VFS_ERR_INVAL);
    if (!validate_user_write_range(out_ptr, sizeof(process_info_t))) return err(VFS_ERR_INVAL);
    process_info_t info;
    u32 pid = (u32)pid64;
    if (process_user_active()) {
        if (process_async_scheduler_active() && process_request_wait(pid, (uptr)out_ptr)) return ok(0);
        return err(VFS_ERR_NOENT);
    }
    if (process_wait(pid, &info)) {
        return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
    }
    return err(VFS_ERR_NOENT);
}


syscall_result_t rabbitbone_sys_yield(void) {
    scheduler_note_yield();
    if (process_async_scheduler_active()) process_request_reschedule();
    return ok(0);
}

syscall_result_t rabbitbone_sys_sleep(u64 ticks) {
    if (ticks > 10000u) return err(VFS_ERR_INVAL);
    scheduler_note_sleep(ticks);
    if (process_async_scheduler_active()) {
        process_request_sleep(ticks);
    } else if (ticks) {
        cpu_sti();
        timer_sleep_ticks(ticks);
    }
    return ok((i64)pit_ticks());
}

syscall_result_t rabbitbone_sys_schedinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    sched_stats_t stats;
    scheduler_stats(&stats);
    return copy_out_buf(out_ptr, &stats, sizeof(stats)) ? ok(0) : err(VFS_ERR_INVAL);
}

syscall_result_t rabbitbone_sys_preemptinfo(u64 out_ptr) {
    if (!out_ptr) return err(VFS_ERR_INVAL);
    rabbitbone_preemptinfo_t info;
    if (!scheduler_preempt_info(&info)) return err(VFS_ERR_INVAL);
    return copy_out_buf(out_ptr, &info, sizeof(info)) ? ok(0) : err(VFS_ERR_INVAL);
}


