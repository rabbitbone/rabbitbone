static int sh_wait_pid(unsigned int pid) {
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    if (au_wait(pid, &info) != 0) return 1;
    return info.exit_code;
}

static void sh_close_pipe_table(unsigned int pipes[SH_STAGES_MAX - 1u][2], unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) {
        if (pipes[i][0] > RABBITBONE_STDERR) (void)au_close((au_i64)pipes[i][0]);
        if (pipes[i][1] > RABBITBONE_STDERR) (void)au_close((au_i64)pipes[i][1]);
    }
}

static int sh_exec_external_child(sh_stage_t *stage) {
    if (!stage || stage->argc == 0) return 0;
    if (!stage->path[0] && !sh_resolve_program(stage->path, sizeof(stage->path), stage->argv[0])) {
        sh_err("sh: command not found: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->argv[0]); sh_err("\n");
        return 127;
    }
    if (!sh_path_is_elf(stage->path)) {
        sh_err("sh: not executable: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->path); sh_err("\n");
        return 126;
    }
    stage->argv[0] = stage->path;
    char env_storage[RABBITBONE_ENV_MAX][SH_ENV_ENTRY_MAX];
    const char *envp[RABBITBONE_ENV_MAX];
    au_memset(env_storage, 0, sizeof(env_storage));
    au_memset((void *)envp, 0, sizeof(envp));
    int envc = sh_build_env(env_storage, envp, stage);
    au_i64 r = au_execve(stage->path, stage->argc, (const char *const *)stage->argv, (unsigned int)envc, envp);
    sh_err("sh: exec failed: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->path); sh_err("\n");
    return r < 0 ? 127 : 126;
}

static int sh_child_run_stage(sh_stage_t *stage) {
    if (!stage) return 127;
    if (!sh_open_redirs(stage)) return 126;
    if (stage->argc == 0) return 0;
    if (sh_is_builtin(stage->argv[0])) {
        (void)sh_apply_stage_assignments(stage, 0);
        return sh_run_builtin((int)stage->argc, stage->argv, 0);
    }
    return sh_exec_external_child(stage);
}

static int sh_fork_stage(sh_stage_t *stage, unsigned int in_fd, unsigned int out_fd) {
    au_i64 child = au_fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (in_fd != RABBITBONE_STDIN) {
            if (au_dup2((au_i64)in_fd, (au_i64)RABBITBONE_STDIN, 0) != (au_i64)RABBITBONE_STDIN) au_exit(125);
        }
        if (out_fd != RABBITBONE_STDOUT) {
            if (au_dup2((au_i64)out_fd, (au_i64)RABBITBONE_STDOUT, 0) != (au_i64)RABBITBONE_STDOUT) au_exit(125);
        }
        int code = sh_child_run_stage(stage);
        au_exit(code);
    }
    return (int)child;
}

static int sh_spawn_external_wait(sh_stage_t *stage) {
    if (!stage) return 1;
    int pid = sh_fork_stage(stage, RABBITBONE_STDIN, RABBITBONE_STDOUT);
    if (pid < 0) { sh_err("sh: fork failed\n"); return 127; }
    return sh_wait_pid((unsigned int)pid);
}

static int sh_run_single_stage(sh_stage_t *stage, int background) {
    if (!stage) return 1;
    if (stage->argc == 0) {
        sh_fd_save_t saves[3];
        int rc = 0;
        if (sh_stage_has_redirs(stage)) {
            if (!sh_save_shell_redirs(stage, saves)) return 1;
            if (!sh_open_redirs(stage)) rc = 1;
            sh_restore_shell_redirs(saves);
            if (rc) return rc;
        }
        return sh_apply_stage_assignments(stage, 1) ? 0 : 1;
    }

    if (!background && sh_is_builtin(stage->argv[0])) {
        sh_fd_save_t saves[3];
        int have_saves = 0;
        if (sh_stage_has_redirs(stage)) {
            if (!sh_save_shell_redirs(stage, saves)) return 1;
            have_saves = 1;
            if (!sh_open_redirs(stage)) { sh_restore_shell_redirs(saves); return 1; }
        }
        (void)sh_apply_stage_assignments(stage, 0);
        int rc = sh_run_builtin((int)stage->argc, stage->argv, 1);
        if (have_saves) sh_restore_shell_redirs(saves);
        return rc;
    }

    int pid = sh_fork_stage(stage, RABBITBONE_STDIN, RABBITBONE_STDOUT);
    if (pid < 0) { sh_err("sh: fork failed\n"); return 127; }
    if (background) {
        sh_last_async_pid = (unsigned int)pid;
        sh_puts("["); sh_print_i64((au_i64)pid); sh_puts("] ");
        sh_puts(stage->argv[0]); sh_puts("\n");
        return 0;
    }
    return sh_wait_pid((unsigned int)pid);
}

static SH_NOINLINE int sh_run_pipeline_text(char *segment, int background) {
    char *parts[SH_STAGES_MAX];
    unsigned int stages_n = 0;
    sh_stage_t *stages = sh_pipeline_stages;
    au_memset(sh_pipeline_stages, 0, sizeof(sh_pipeline_stages));
    if (!segment) return 0;
    if (!sh_split_pipeline(segment, parts, &stages_n)) { sh_err("sh: invalid pipeline\n"); return 2; }
    if (stages_n == 0) return 0;
    for (unsigned int i = 0; i < stages_n; ++i) {
        if (!sh_parse_stage(parts[i], &stages[i])) { sh_err("sh: parse error\n"); return 2; }
        if (i > 0 && stages[i].in_path[0]) { sh_err("sh: < conflicts with pipe\n"); return 2; }
        if (i + 1u < stages_n && stages[i].out_path[0]) { sh_err("sh: > conflicts with pipe\n"); return 2; }
    }

    if (stages_n == 1) return sh_run_single_stage(&stages[0], background);

    unsigned int pipes[SH_STAGES_MAX - 1u][2];
    au_memset(pipes, 0, sizeof(pipes));
    for (unsigned int i = 0; i + 1u < stages_n; ++i) {
        if (au_pipe(pipes[i]) != 0) {
            sh_close_pipe_table(pipes, i);
            sh_err("sh: pipe failed\n");
            return 1;
        }
    }

    unsigned int pids[SH_STAGES_MAX];
    au_memset(pids, 0, sizeof(pids));
    unsigned int spawned = 0;
    for (; spawned < stages_n; ++spawned) {
        au_i64 child = au_fork();
        if (child < 0) {
            sh_err("sh: fork failed\n");
            break;
        }
        if (child == 0) {
            if (spawned > 0) {
                if (au_dup2((au_i64)pipes[spawned - 1u][0], (au_i64)RABBITBONE_STDIN, 0) != (au_i64)RABBITBONE_STDIN) au_exit(125);
            }
            if (spawned + 1u < stages_n) {
                if (au_dup2((au_i64)pipes[spawned][1], (au_i64)RABBITBONE_STDOUT, 0) != (au_i64)RABBITBONE_STDOUT) au_exit(125);
            }
            sh_close_pipe_table(pipes, stages_n - 1u);
            int code = sh_child_run_stage(&stages[spawned]);
            au_exit(code);
        }
        pids[spawned] = (unsigned int)child;
    }
    sh_close_pipe_table(pipes, stages_n - 1u);

    if (background) {
        if (spawned) {
            sh_last_async_pid = pids[spawned - 1u];
            sh_puts("["); sh_print_u64(sh_last_async_pid); sh_puts("] pipeline\n");
        }
        return spawned == stages_n ? 0 : 1;
    }

    int rc = 1;
    for (unsigned int i = 0; i < spawned; ++i) rc = sh_wait_pid(pids[i]);
    return rc;
}

static SH_NOINLINE int sh_run_line(char *line) {
    au_usize pos = 0;
    unsigned int prev_op = SH_OP_SEQ;
    int last = sh_last_status;
    if (!line) return 0;
    for (;;) {
        char *segment = 0;
        unsigned int op_after = SH_OP_SEQ;
        int background = 0;
        int r = sh_next_segment(line, &pos, &segment, &op_after, &background);
        if (r < 0) { sh_err("sh: unmatched quote\n"); sh_last_status = 2; return 2; }
        if (r == 0) break;
        int should_run = 1;
        if (prev_op == SH_OP_AND && last != 0) should_run = 0;
        if (prev_op == SH_OP_OR && last == 0) should_run = 0;
        if (should_run) last = sh_run_pipeline_text(segment, background);
        prev_op = op_after;
    }
    sh_last_status = last;
    return last;
}

static SH_NOINLINE int sh_command_from_argv(int argc, char **argv) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    if (sh_is_builtin(argv[0])) return sh_run_builtin(argc, argv, 0);
    sh_stage_t *stage = &sh_command_stage;
    au_memset(stage, 0, sizeof(*stage));
    if ((unsigned int)argc > SH_ARG_MAX) { sh_err("sh: too many arguments\n"); return 1; }
    for (int i = 0; i < argc; ++i) {
        if (!sh_copy(stage->token_storage[i], sizeof(stage->token_storage[0]), argv[i])) return 1;
        stage->argv[i] = stage->token_storage[i];
    }
    stage->argc = (unsigned int)argc;
    if (!sh_resolve_program(stage->path, sizeof(stage->path), argv[0])) { sh_err("sh: command not found: "); sh_write_fd((au_i64)RABBITBONE_STDERR, argv[0]); sh_err("\n"); return 127; }
    if (!sh_path_is_elf(stage->path)) { sh_err("sh: not executable: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->path); sh_err("\n"); return 126; }
    return sh_spawn_external_wait(stage);
}
