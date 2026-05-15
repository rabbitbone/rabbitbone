typedef struct sh_fd_save {
    unsigned int fd;
    au_i64 saved;
    unsigned int active;
} sh_fd_save_t;

static int sh_stage_has_redirs(const sh_stage_t *stage) {
    return stage && (stage->in_path[0] || stage->out_path[0] || stage->err_path[0] || stage->err_to_out || stage->out_to_err);
}

static unsigned int sh_open_flags_for_mode(unsigned int mode) {
    unsigned int flags = RABBITBONE_O_WRONLY | RABBITBONE_O_CREAT;
    if (mode == SH_REDIR_APPEND) flags |= RABBITBONE_O_APPEND;
    else flags |= RABBITBONE_O_TRUNC;
    return flags;
}

static int sh_dup_to(au_i64 src, au_i64 dst, const char *what) {
    if (au_dup2(src, dst, 0) != dst) {
        sh_err("sh: dup2 failed for ");
        sh_err(what ? what : "fd");
        sh_err("\n");
        return 0;
    }
    return 1;
}

static int sh_open_redirs(const sh_stage_t *stage) {
    if (!stage) return 1;
    if (stage->in_path[0]) {
        au_i64 fd = au_open2(stage->in_path, RABBITBONE_O_RDONLY);
        if (fd < 0) { sh_err("sh: cannot open input: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->in_path); sh_err("\n"); return 0; }
        int ok = sh_dup_to(fd, (au_i64)RABBITBONE_STDIN, "stdin");
        if (fd > (au_i64)RABBITBONE_STDERR) (void)au_close(fd);
        if (!ok) return 0;
    }
    if (stage->out_path[0]) {
        unsigned int mode = stage->out_mode ? stage->out_mode : SH_REDIR_TRUNC;
        au_i64 fd = au_open2(stage->out_path, sh_open_flags_for_mode(mode));
        if (fd < 0) { sh_err("sh: cannot open output: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->out_path); sh_err("\n"); return 0; }
        int ok = sh_dup_to(fd, (au_i64)RABBITBONE_STDOUT, "stdout");
        if (fd > (au_i64)RABBITBONE_STDERR) (void)au_close(fd);
        if (!ok) return 0;
    }
    if (stage->err_path[0]) {
        unsigned int mode = stage->err_mode ? stage->err_mode : SH_REDIR_TRUNC;
        au_i64 fd = au_open2(stage->err_path, sh_open_flags_for_mode(mode));
        if (fd < 0) { sh_err("sh: cannot open stderr: "); sh_write_fd((au_i64)RABBITBONE_STDERR, stage->err_path); sh_err("\n"); return 0; }
        int ok = sh_dup_to(fd, (au_i64)RABBITBONE_STDERR, "stderr");
        if (fd > (au_i64)RABBITBONE_STDERR) (void)au_close(fd);
        if (!ok) return 0;
    }
    if (stage->err_to_out) {
        if (!sh_dup_to((au_i64)RABBITBONE_STDOUT, (au_i64)RABBITBONE_STDERR, "stderr")) return 0;
    }
    if (stage->out_to_err) {
        if (!sh_dup_to((au_i64)RABBITBONE_STDERR, (au_i64)RABBITBONE_STDOUT, "stdout")) return 0;
    }
    return 1;
}

static int sh_save_fd(sh_fd_save_t *s, unsigned int fd) {
    if (!s) return 0;
    s->fd = fd;
    s->saved = au_dup((au_i64)fd);
    s->active = s->saved >= 0 ? 1u : 0u;
    return s->active != 0;
}

static int sh_save_shell_redirs(const sh_stage_t *stage, sh_fd_save_t saves[3]) {
    if (!stage || !saves) return 0;
    au_memset(saves, 0, sizeof(sh_fd_save_t) * 3u);
    if (stage->in_path[0] && !sh_save_fd(&saves[0], RABBITBONE_STDIN)) return 0;
    if ((stage->out_path[0] || stage->out_to_err) && !sh_save_fd(&saves[1], RABBITBONE_STDOUT)) return 0;
    if ((stage->err_path[0] || stage->err_to_out || stage->out_to_err) && !sh_save_fd(&saves[2], RABBITBONE_STDERR)) return 0;
    return 1;
}

static void sh_restore_shell_redirs(sh_fd_save_t saves[3]) {
    if (!saves) return;
    for (int i = 2; i >= 0; --i) {
        if (!saves[i].active) continue;
        (void)au_dup2(saves[i].saved, (au_i64)saves[i].fd, 0);
        if (saves[i].saved > (au_i64)RABBITBONE_STDERR) (void)au_close(saves[i].saved);
        saves[i].active = 0;
    }
}

static int sh_apply_stage_assignments(const sh_stage_t *stage, unsigned int exported) {
    if (!stage) return 0;
    for (unsigned int i = 0; i < stage->assign_count; ++i) {
        if (!sh_var_set(stage->assign_name[i], stage->assign_value[i], exported)) return 0;
        if (sh_streq(stage->assign_name[i], "PATH")) (void)sh_set_path_value(stage->assign_value[i]);
    }
    return 1;
}

static void sh_print_procinfo(const au_procinfo_t *info) {
    if (!info) return;
    sh_puts("pid="); sh_print_u64(info->pid);
    sh_puts(" pgrp="); sh_print_u64(info->pgrp);
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
