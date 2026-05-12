static syscall_result_t sys_read_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || !validate_user_write_range(user_buf, len)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_PIPE_WRITE || handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) return err(VFS_ERR_PERM);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_IN) {
        u32 mode = tty_get_mode();
        while (total < len) {
            char c = 0;
            if (!tty_read_char(&c)) break;
            if ((mode & AURORA_TTY_MODE_ECHO) && c >= 32) console_putc(c);
            if ((mode & AURORA_TTY_MODE_ECHO) && c == '\n') console_putc('\n');
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, &c, 1u)) return err(VFS_ERR_INVAL);
            ++total;
            if ((mode & AURORA_TTY_MODE_CANON) && c == '\n') break;
        }
        if (__builtin_add_overflow(handles[h].offset, (u64)total, &handles[h].offset)) return err(VFS_ERR_INVAL);
        return ok((i64)total);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_READ) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        while (total < len) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            usize got = pipe_read_bytes(p, chunk, n);
            if (!got) break;
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, chunk, got)) return err(VFS_ERR_INVAL);
            if (__builtin_add_overflow(handles[h].offset, (u64)got, &handles[h].offset)) return err(VFS_ERR_INVAL);
            total += got;
            if (got < n) break;
        }
        return ok((i64)total);
    }
    refresh_handle_stat(&handles[h]);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (!open_flags_can_read(f->open_flags)) return err(VFS_ERR_PERM);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        usize got = 0;
        u64 ff = spin_lock_irqsave(&f->lock);
        vfs_status_t vs = vfs_read_ref(&f->ref, f->offset, chunk, n, &got);
        if (vs != VFS_OK) { spin_unlock_irqrestore(&f->lock, ff); return err(vs); }
        u64 ptr = 0;
        if (got && (!add_user_ptr(user_buf, total, &ptr) || !copy_out_buf(ptr, chunk, got))) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_INVAL); }
        u64 new_offset = 0;
        if (__builtin_add_overflow(f->offset, (u64)got, &new_offset)) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_INVAL); }
        f->offset = new_offset;
        sync_handle_from_file(&handles[h], f);
        spin_unlock_irqrestore(&f->lock, ff);
        total += got;
        if (got < n) break;
    }
    return ok((i64)total);
}

static syscall_result_t sys_write_impl(sys_handle_t *handles, usize h, u64 user_buf, usize len) {
    if (!valid_handle(handles, h) || !validate_user_read_range(user_buf, len)) return err(VFS_ERR_INVAL);
    if (handles[h].kind == SYS_HANDLE_PIPE_READ || handles[h].kind == SYS_HANDLE_CONSOLE_IN) return err(VFS_ERR_PERM);
    u8 chunk[SYSCALL_IO_CHUNK];
    usize total = 0;
    if (handles[h].kind == SYS_HANDLE_CONSOLE_OUT || handles[h].kind == SYS_HANDLE_CONSOLE_ERR) {
        while (total < len) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
            for (usize i = 0; i < n; ++i) console_putc((char)chunk[i]);
            total += n;
        }
        if (__builtin_add_overflow(handles[h].offset, (u64)total, &handles[h].offset)) return err(VFS_ERR_INVAL);
        return ok((i64)total);
    }
    if (handles[h].kind == SYS_HANDLE_PIPE_WRITE) {
        sys_pipe_t *p = pipe_by_id(handles[h].pipe_id);
        if (!p) return err(VFS_ERR_NOENT);
        while (total < len) {
            usize n = len - total;
            if (n > sizeof(chunk)) n = sizeof(chunk);
            u64 ptr = 0;
            if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
            usize wrote = 0;
            pipe_write_status_t pst = pipe_write_bytes(p, chunk, n, &wrote);
            if (pst == PIPE_WRITE_CLOSED) return total ? ok((i64)total) : err(VFS_ERR_IO);
            if (pst == PIPE_WRITE_INVAL) return err(VFS_ERR_INVAL);
            if (!wrote) break;
            if (__builtin_add_overflow(handles[h].offset, (u64)wrote, &handles[h].offset)) return err(VFS_ERR_INVAL);
            total += wrote;
            if (wrote < n || pst == PIPE_WRITE_FULL) break;
        }
        return ok((i64)total);
    }
    refresh_handle_stat(&handles[h]);
    sys_file_t *f = handle_file(&handles[h]);
    if (!f) return err(VFS_ERR_NOENT);
    if (!open_flags_can_write(f->open_flags)) return err(VFS_ERR_PERM);
    if (handles[h].type == VFS_NODE_DIR) return err(VFS_ERR_ISDIR);
    while (total < len) {
        usize n = len - total;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        u64 ptr = 0;
        if (!add_user_ptr(user_buf, total, &ptr) || !copy_in_buf(chunk, ptr, n)) return err(VFS_ERR_INVAL);
        usize wrote = 0;
        u64 ff = spin_lock_irqsave(&f->lock);
        if (f->open_flags & AURORA_O_APPEND) {
            vfs_stat_t ast;
            if (vfs_stat_ref(&f->ref, &ast) == VFS_OK) f->size = ast.size;
            f->offset = f->size;
        }
        if (f->offset > SYSCALL_MAX_FILE_SIZE || (u64)n > SYSCALL_MAX_FILE_SIZE - f->offset) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_INVAL); }
        vfs_status_t vs = vfs_write_ref(&f->ref, f->offset, chunk, n, &wrote);
        if (vs != VFS_OK) { spin_unlock_irqrestore(&f->lock, ff); return err(vs); }
        u64 new_offset = 0;
        if (__builtin_add_overflow(f->offset, (u64)wrote, &new_offset) || new_offset > SYSCALL_MAX_FILE_SIZE) { spin_unlock_irqrestore(&f->lock, ff); return err(VFS_ERR_INVAL); }
        f->offset = new_offset;
        if (f->offset > f->size) f->size = f->offset;
        sync_handle_from_file(&handles[h], f);
        spin_unlock_irqrestore(&f->lock, ff);
        total += wrote;
        if (wrote < n) break;
    }
    return ok((i64)total);
}

syscall_result_t aurora_sys_version(void) {
    return ok(SYSCALL_VERSION_VALUE);
}

syscall_result_t aurora_sys_write_console(u64 ptr, u64 len64) {
    usize n = (usize)len64;
    if ((!ptr && n) || len64 > 65536u) return err(VFS_ERR_INVAL);
    char chunk[256];
    usize done = 0;
    while (done < n) {
        usize step = n - done;
        if (step > sizeof(chunk)) step = sizeof(chunk);
        u64 cptr = 0;
        if (!add_user_ptr(ptr, done, &cptr) || !copy_in_buf(chunk, cptr, step)) return err(VFS_ERR_INVAL);
        for (usize i = 0; i < step; ++i) console_putc(chunk[i]);
        done += step;
    }
    return ok((i64)n);
}

