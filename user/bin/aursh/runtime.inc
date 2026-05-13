static int sh_open_redirs(const sh_stage_t *stage) {
    if (!stage) return 1;
    if (stage->in_path[0]) {
        au_i64 fd = au_open2(stage->in_path, AURORA_O_RDONLY);
        if (fd < 0) { sh_err("sh: cannot open input: "); sh_write_fd((au_i64)AURORA_STDERR, stage->in_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDIN, 0) != (au_i64)AURORA_STDIN) { sh_err("sh: dup2 input failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    if (stage->out_path[0]) {
        au_i64 fd = au_open2(stage->out_path, AURORA_O_WRONLY | AURORA_O_CREAT | AURORA_O_TRUNC);
        if (fd < 0) { sh_err("sh: cannot open output: "); sh_write_fd((au_i64)AURORA_STDERR, stage->out_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDOUT, 0) != (au_i64)AURORA_STDOUT) { sh_err("sh: dup2 output failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    if (stage->err_path[0]) {
        au_i64 fd = au_open2(stage->err_path, AURORA_O_WRONLY | AURORA_O_CREAT | AURORA_O_TRUNC);
        if (fd < 0) { sh_err("sh: cannot open stderr: "); sh_write_fd((au_i64)AURORA_STDERR, stage->err_path); sh_err("\n"); return 0; }
        if (au_dup2(fd, (au_i64)AURORA_STDERR, 0) != (au_i64)AURORA_STDERR) { sh_err("sh: dup2 stderr failed\n"); return 0; }
        if (fd > (au_i64)AURORA_STDERR) (void)au_close(fd);
    }
    return 1;
}

static void sh_print_procinfo(const au_procinfo_t *info) {
    if (!info) return;
    sh_puts("pid="); sh_print_u64(info->pid);
    sh_puts(" state="); sh_print_u64(info->state);
    sh_puts(" exit="); sh_print_i64(info->exit_code);
    sh_puts(" status="); sh_print_i64(info->status);
    sh_puts(" uid="); sh_print_u64(info->uid);
    sh_puts(" euid="); sh_print_u64(info->euid);
    sh_puts(" pages="); sh_print_u64(info->mapped_pages);
    sh_puts(" asid="); sh_print_u64(info->address_space_generation);
    sh_puts(" name="); sh_puts(info->name);
    if (info->faulted) {
        sh_puts(" faulted=1 vector="); sh_print_u64(info->fault_vector);
        sh_puts(" rip="); sh_print_hex64(info->fault_rip);
        sh_puts(" addr="); sh_print_hex64(info->fault_addr);
    }
    sh_puts("\n");
}
