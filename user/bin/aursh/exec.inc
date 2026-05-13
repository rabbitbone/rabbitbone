static int sh_command_from_argv(int argc, char **argv) {
    if (argc <= 0 || !argv || !argv[0]) return 0;
    if (sh_is_builtin(argv[0])) return sh_run_builtin(argc, argv, 0);
    sh_stage_t stage;
    au_memset(&stage, 0, sizeof(stage));
    if ((unsigned int)argc > SH_ARG_MAX) return 1;
    for (int i = 0; i < argc; ++i) stage.argv[i] = argv[i];
    stage.argc = (unsigned int)argc;
    if (!sh_resolve_program(stage.path, sizeof(stage.path), argv[0])) { sh_err("sh: command not found: "); sh_write_fd((au_i64)AURORA_STDERR, argv[0]); sh_err("\n"); return 127; }
    if (!sh_path_is_elf(stage.path)) { sh_err("sh: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, stage.path); sh_err("\n"); return 126; }
    return sh_spawn_external_wait(&stage);
}

static int sh_spawn_external_wait(sh_stage_t *stage) {
    if (!stage) return 1;
    if (!sh_path_is_elf(stage->path)) { sh_err("sh: not an executable ELF: "); sh_write_fd((au_i64)AURORA_STDERR, stage->path); sh_err("\n"); return 126; }
    stage->argv[0] = stage->path;
    au_procinfo_t info;
    au_memset(&info, 0, sizeof(info));
    au_i64 r = au_spawnv_wait(stage->path, stage->argc, (const char *const *)stage->argv, &info);
    if (r < 0) {
        sh_err("sh: command not found: ");
        sh_write_fd((au_i64)AURORA_STDERR, sh_basename(stage->path));
        sh_err("\n");
        return 127;
    }
    return info.exit_code;
}

static void sh_close_pipe_table(unsigned int pipes[SH_STAGES_MAX - 1u][2], unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) {
        if (pipes[i][0] > AURORA_STDERR) (void)au_close((au_i64)pipes[i][0]);
        if (pipes[i][1] > AURORA_STDERR) (void)au_close((au_i64)pipes[i][1]);
    }
}

static int sh_child_run_stage(sh_stage_t *stage) {
    if (!stage) return 127;
    if (!sh_open_redirs(stage)) return 126;
    if (sh_is_builtin(stage->argv[0])) return sh_run_builtin((int)stage->argc, stage->argv, 0);
    stage->argv[0] = stage->path;
    au_i64 r = au_execv(stage->path, stage->argc, (const char *const *)stage->argv);
    sh_err("sh: exec failed: ");
    sh_write_fd((au_i64)AURORA_STDERR, stage->path);
    sh_err("\n");
    return r < 0 ? 127 : 126;
}

static int sh_run_pipeline(char *line) {
    char *parts[SH_STAGES_MAX];
    unsigned int stages_n = 0;
    sh_stage_t stages[SH_STAGES_MAX];
    if (!sh_split_pipeline(line, parts, &stages_n)) { sh_err("sh: invalid pipeline\n"); return 2; }
    if (stages_n == 0) return 0;
    for (unsigned int i = 0; i < stages_n; ++i) {
        if (!sh_parse_stage(parts[i], &stages[i])) { sh_err("sh: parse error\n"); return 2; }
        if (i > 0 && stages[i].in_path[0]) { sh_err("sh: < conflicts with pipe\n"); return 2; }
        if (i + 1u < stages_n && stages[i].out_path[0]) { sh_err("sh: > conflicts with pipe\n"); return 2; }
    }

    if (stages_n == 1 && !stages[0].in_path[0] && !stages[0].out_path[0] && !stages[0].err_path[0]) {
        if (sh_is_builtin(stages[0].argv[0])) return sh_run_builtin((int)stages[0].argc, stages[0].argv, 1);
        return sh_spawn_external_wait(&stages[0]);
    }

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
            int code;
            if (spawned > 0) {
                if (au_dup2((au_i64)pipes[spawned - 1u][0], (au_i64)AURORA_STDIN, 0) != (au_i64)AURORA_STDIN) au_exit(125);
            }
            if (spawned + 1u < stages_n) {
                if (au_dup2((au_i64)pipes[spawned][1], (au_i64)AURORA_STDOUT, 0) != (au_i64)AURORA_STDOUT) au_exit(125);
            }
            sh_close_pipe_table(pipes, stages_n - 1u);
            code = sh_child_run_stage(&stages[spawned]);
            au_exit(code);
        }
        pids[spawned] = (unsigned int)child;
    }
    sh_close_pipe_table(pipes, stages_n - 1u);

    int rc = 1;
    for (unsigned int i = 0; i < spawned; ++i) {
        au_procinfo_t info;
        au_memset(&info, 0, sizeof(info));
        if (au_wait(pids[i], &info) == 0) rc = info.exit_code;
        else rc = 1;
    }
    return rc;
}
