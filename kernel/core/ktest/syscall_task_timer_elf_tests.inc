static void test_syscall_task_timer_elf(void) {
    suite_begin("syscall/task/timer/elf/log");
    syscall_result_t r = syscall_dispatch(AURORA_SYS_VERSION, 0, 0, 0, 0, 0, 0);
    check(r.error == 0 && r.value == (i64)AURORA_SYSCALL_ABI_VERSION, "Rust syscall ABI version matches central kernel version");
    check(aurora_rust_syscall_selftest(), "Rust syscall decoder/validator selftest");
    r = syscall_dispatch(AURORA_SYS_CLOSE, AURORA_PROCESS_HANDLE_CAP, 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall rejects handle equal to ABI cap before C backend");
    char cwd_buf[VFS_PATH_MAX];
    memset(cwd_buf, 0, sizeof(cwd_buf));
    r = syscall_dispatch(AURORA_SYS_GETCWD, (u64)(uptr)cwd_buf, sizeof(cwd_buf), 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_buf, "/") == 0, "syscall getcwd returns initial root cwd");
    r = syscall_dispatch(AURORA_SYS_CHDIR, (u64)(uptr)"/disk0", 0, 0, 0, 0, 0);
    check(r.error == 0, "syscall chdir accepts persistent EXT4 directory");
    memset(cwd_buf, 0, sizeof(cwd_buf));
    r = syscall_dispatch(AURORA_SYS_GETCWD, (u64)(uptr)cwd_buf, sizeof(cwd_buf), 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_buf, "/disk0") == 0, "syscall getcwd reflects changed cwd");
    aurora_statvfs_t cwd_sv;
    memset(&cwd_sv, 0, sizeof(cwd_sv));
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)".", (u64)(uptr)&cwd_sv, 0, 0, 0, 0);
    check(r.error == 0 && strcmp(cwd_sv.fs_name, "ext4") == 0, "relative syscall path resolves against cwd");
    r = syscall_dispatch(AURORA_SYS_CHDIR, (u64)(uptr)"/", 0, 0, 0, 0, 0);
    check(r.error == 0, "syscall chdir restores root cwd");
    r = syscall_dispatch(AURORA_SYS_OPEN, (u64)(uptr)"/bad\\path", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall rejects disallowed path bytes before C backend");
    r = syscall_dispatch(AURORA_SYS_CREATE, (u64)(uptr)"/tmp/too-large", (u64)(uptr)"x", 65537u, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall caps create payload size");
    r = syscall_dispatch(AURORA_SYS_STAT, (u64)(uptr)"/etc/motd", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall requires stat output pointer");
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)"/disk0", 0, 0, 0, 0, 0);
    check(r.value == -1 && r.error == VFS_ERR_INVAL, "Rust syscall requires statvfs output pointer");
    aurora_statvfs_t sys_sv;
    memset(&sys_sv, 0, sizeof(sys_sv));
    r = syscall_dispatch(AURORA_SYS_STATVFS, (u64)(uptr)"/disk0", (u64)(uptr)&sys_sv, 0, 0, 0, 0);
    check(r.error == 0 && sys_sv.block_size >= 1024u && sys_sv.total_blocks > 0u && sys_sv.free_blocks <= sys_sv.total_blocks && strcmp(sys_sv.fs_name, "ext4") == 0, "Rust-dispatched statvfs returns EXT4 capacity");
    r = syscall_dispatch(AURORA_SYS_SYNC, 0, 0, 0, 0, 0, 0);
    check(r.error == 0, "Rust-dispatched sync flushes mounted filesystems");

    check(syscall_selftest(), "Rust-dispatched syscall filesystem/process-control selftest");
    check(strcmp(syscall_name(AURORA_SYS_WRITE), "write") == 0 && strcmp(syscall_name(AURORA_SYS_GETPID), "getpid") == 0 && strcmp(syscall_name(AURORA_SYS_PROCINFO), "procinfo") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWN), "spawn") == 0 && strcmp(syscall_name(AURORA_SYS_WAIT), "wait") == 0 && strcmp(syscall_name(AURORA_SYS_YIELD), "yield") == 0 && strcmp(syscall_name(AURORA_SYS_SLEEP), "sleep") == 0 && strcmp(syscall_name(AURORA_SYS_SCHEDINFO), "schedinfo") == 0 && strcmp(syscall_name(AURORA_SYS_DUP), "dup") == 0 && strcmp(syscall_name(AURORA_SYS_TELL), "tell") == 0 && strcmp(syscall_name(AURORA_SYS_FSTAT), "fstat") == 0 && strcmp(syscall_name(AURORA_SYS_FDINFO), "fdinfo") == 0 && strcmp(syscall_name(AURORA_SYS_READDIR), "readdir") == 0 && strcmp(syscall_name(AURORA_SYS_SPAWNV), "spawnv") == 0 && strcmp(syscall_name(AURORA_SYS_PREEMPTINFO), "preemptinfo") == 0 && strcmp(syscall_name(AURORA_SYS_TRUNCATE), "truncate") == 0 && strcmp(syscall_name(AURORA_SYS_RENAME), "rename") == 0 && strcmp(syscall_name(AURORA_SYS_SYNC), "sync") == 0 && strcmp(syscall_name(AURORA_SYS_FSYNC), "fsync") == 0 && strcmp(syscall_name(AURORA_SYS_STATVFS), "statvfs") == 0 && strcmp(syscall_name(AURORA_SYS_INSTALL_COMMIT), "install_commit") == 0 && strcmp(syscall_name(AURORA_SYS_CHDIR), "chdir") == 0 && strcmp(syscall_name(AURORA_SYS_GETCWD), "getcwd") == 0 && strcmp(syscall_name(AURORA_SYS_BRK), "brk") == 0 && strcmp(syscall_name(AURORA_SYS_SBRK), "sbrk") == 0 && strcmp(syscall_name(AURORA_SYS_MMAP), "mmap") == 0 && strcmp(syscall_name(AURORA_SYS_MUNMAP), "munmap") == 0 && strcmp(syscall_name(AURORA_SYS_MPROTECT), "mprotect") == 0 && strcmp(syscall_name(999), "unknown") == 0, "syscall name table");
    r = syscall_dispatch(AURORA_SYS_TICKS, 0, 0, 0, 0, 0, 0);
    check(r.error == 0 && r.value >= 0, "syscall ticks");

    check(task_selftest(), "cooperative kernel task scheduler selftest");
    check(scheduler_selftest(), "process scheduler queue/wait/accounting selftest");
    task_init();
    task_stats_t ts;
    task_get_stats(&ts);
    check(ts.used_slots == 0 && ts.next_pid == 1u, "task table reset after selftest");

    check(timer_selftest(), "PIT timer advances while interrupts are enabled");
    check(strcmp(log_level_name(LOG_INFO), "INFO") == 0 && strcmp(log_level_name(LOG_FATAL), "FATAL") == 0, "log level names");
    KLOG(LOG_INFO, "ktest", "log write path exercised");

    elf64_ehdr_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.e_ident[0] = 0x7f;
    hdr.e_ident[1] = 'E';
    hdr.e_ident[2] = 'L';
    hdr.e_ident[3] = 'F';
    hdr.e_ident[4] = 2;
    hdr.e_ident[5] = 1;
    hdr.e_ident[6] = 1;
    hdr.e_type = ELF64_ET_EXEC;
    hdr.e_machine = ELF64_EM_X86_64;
    hdr.e_version = 1;
    hdr.e_entry = 0x400000;
    hdr.e_phoff = sizeof(elf64_ehdr_t);
    hdr.e_ehsize = sizeof(elf64_ehdr_t);
    hdr.e_phentsize = sizeof(elf64_phdr_t);
    hdr.e_phnum = 1;
    check(elf64_validate_header(&hdr, sizeof(hdr) + sizeof(elf64_phdr_t)) == ELF_OK, "ELF64 validates minimal amd64 executable header with program header");
    hdr.e_phnum = 0;
    check(elf64_validate_header(&hdr, sizeof(hdr)) == ELF_ERR_FORMAT, "ELF64 rejects executable header without program headers");
    hdr.e_phnum = 1;
    hdr.e_machine = 3;
    check(elf64_validate_header(&hdr, sizeof(hdr) + sizeof(elf64_phdr_t)) == ELF_ERR_UNSUPPORTED, "ELF64 rejects non-amd64 machine");
    suite_end();
}


