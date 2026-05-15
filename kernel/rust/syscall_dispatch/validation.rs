fn fd_in_range(fd: u64) -> bool { fd < MAX_HANDLES }
fn valid_mmap_prot(prot: u64) -> bool {
    prot != 0 && (prot & !PROT_SUPPORTED) == 0 && !((prot & PROT_WRITE) != 0 && (prot & PROT_EXEC) != 0)
}
fn valid_mmap_flags(flags: u64) -> bool {
    let sharing = flags & (MAP_PRIVATE | MAP_SHARED);
    (flags & !MAP_SUPPORTED) == 0 && (sharing == MAP_PRIVATE || sharing == MAP_SHARED)
}

fn valid_chmod_mode(mode: u64) -> bool {
    mode <= CHMOD_MAX_MODE && (mode & CHMOD_FORBIDDEN_MODE) == 0
}

fn kctl_terminal_op(op: u64) -> bool {
    op == KCTL_OP_PANIC || op == KCTL_OP_REBOOT || op == KCTL_OP_HALT
}

fn valid_cred_args(a: SysArgs) -> bool {
    match a.a0 {
        CRED_OP_GET => a.a1 != 0 && a.a2 == 0 && a.a3 == 0,
        CRED_OP_LOGIN => a.a1 != 0 && a.a2 != 0 && a.a3 == 0,
        CRED_OP_SET_USER => a.a1 != 0 && a.a2 == 0 && a.a3 == 0,
        CRED_OP_SET_EUID => a.a1 == 0 && a.a2 == 0 && a.a3 <= u32::MAX as u64,
        CRED_OP_USERINFO => a.a1 != 0 && a.a2 == 0,
        _ => false,
    }
}

fn valid_sudo_args(a: SysArgs) -> bool {
    match a.a0 {
        SUDO_OP_STATUS | SUDO_OP_DROP | SUDO_OP_INVALIDATE => a.a1 == 0 && a.a2 == 0,
        SUDO_OP_SET_TIMEOUT => a.a1 == 0 && a.a2 <= SUDO_MAX_TTL_TICKS,
        SUDO_OP_VALIDATE => (a.a2 & !(SUDO_FLAG_ACTIVATE | SUDO_FLAG_PERSIST)) == 0,
        _ => false,
    }
}

fn valid_pid_or_pgrp_arg(pid: u64) -> bool {
    if pid <= MAX_PID {
        return pid != 0x8000_0000;
    }
    pid >= 0xffff_ffff_8000_0001
}

fn validate_args(no: SyscallNo, a: SysArgs) -> Result<(), i64> {
    match no {
        SyscallNo::Version | SyscallNo::Ticks | SyscallNo::GetPid | SyscallNo::Yield | SyscallNo::Fork | SyscallNo::Sync | SyscallNo::Brk | SyscallNo::Sbrk | SyscallNo::Getpgrp | SyscallNo::Setsid | SyscallNo::Tcgetpgrp => Ok(()),

        SyscallNo::Signal => {
            if a.a0 == 0 || a.a0 >= NSIG { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Sigaction => {
            if a.a0 == 0 || a.a0 >= NSIG || (a.a1 == 0 && a.a2 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Sigprocmask => {
            if a.a0 > SIG_SETMASK || (a.a1 == 0 && a.a2 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Sigpending => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Kill => {
            if !valid_pid_or_pgrp_arg(a.a0) || a.a1 == 0 || a.a1 >= NSIG { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Raise => {
            if a.a0 == 0 || a.a0 >= NSIG { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Setpgid => {
            if a.a0 > MAX_PID || a.a1 > MAX_PID { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Getpgid | SyscallNo::Getsid => {
            if a.a0 > MAX_PID { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Tcsetpgrp => {
            if a.a0 == 0 || a.a0 > MAX_PID { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Sigreturn => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Mmap => {
            let anon = (a.a3 & MAP_ANON) != 0;
            if a.a1 == 0 || a.a1 > MMAP_MAX_BYTES || !valid_mmap_prot(a.a2) || !valid_mmap_flags(a.a3) { Err(VFS_ERR_INVAL) }
            else if (a.a3 & MAP_FIXED) != 0 && (a.a0 == 0 || (a.a0 & 4095) != 0) { Err(VFS_ERR_INVAL) }
            else if a.a0 != 0 && (a.a0 & 4095) != 0 { Err(VFS_ERR_INVAL) }
            else if anon && (a.a4 != u64::MAX || a.a5 != 0) { Err(VFS_ERR_INVAL) }
            else if !anon && ((a.a3 & MAP_SHARED) != 0 || !fd_in_range(a.a4) || (a.a5 & 4095) != 0) { Err(VFS_ERR_INVAL) }
            else { Ok(()) }
        }
        SyscallNo::Munmap => {
            if a.a0 == 0 || (a.a0 & 4095) != 0 || a.a1 == 0 || a.a1 > MMAP_MAX_BYTES { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Mprotect => {
            if a.a0 == 0 || (a.a0 & 4095) != 0 || a.a1 == 0 || a.a1 > MMAP_MAX_BYTES || !valid_mmap_prot(a.a2) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::WriteConsole => {
            if a.a1 > MAX_CONSOLE_WRITE || (a.a0 == 0 && a.a1 != 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Open => {
            let flags = a.a1;
            let acc = flags & OPEN_ACCMODE;
            if a.a0 == 0 || (flags & !OPEN_SUPPORTED) != 0 || acc == OPEN_ACCMODE || ((flags & OPEN_EXCL) != 0 && (flags & OPEN_CREAT) == 0) || ((flags & OPEN_TRUNC) != 0 && acc == OPEN_RDONLY) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Mkdir | SyscallNo::Unlink | SyscallNo::Log | SyscallNo::Spawn | SyscallNo::Exec | SyscallNo::Chdir => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::SpawnV | SyscallNo::ExecV => {
            if a.a0 == 0 || a.a1 == 0 || a.a1 > MAX_PROCESS_ARGS || a.a2 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::ExecVe => {
            if a.a0 == 0 || a.a1 == 0 || a.a1 > MAX_PROCESS_ARGS || a.a2 == 0 || a.a3 > MAX_PROCESS_ENVS || (a.a3 != 0 && a.a4 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Close | SyscallNo::Dup | SyscallNo::Tell | SyscallNo::Fsync | SyscallNo::Fdatasync => {
            if fd_in_range(a.a0) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Ftruncate | SyscallNo::Fpreallocate => {
            if fd_in_range(a.a0) && a.a1 <= MAX_FILE_SIZE { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Read | SyscallNo::Write => {
            if !fd_in_range(a.a0) || a.a2 > MAX_IO_BYTES || (a.a2 != 0 && a.a1 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Seek => {
            if !fd_in_range(a.a0) || (a.a2 != SEEK_SET && a.a2 != SEEK_CUR && a.a2 != SEEK_END) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Create => {
            if a.a0 == 0 || a.a2 > MAX_CREATE_BYTES || (a.a1 == 0 && a.a2 != 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Stat | SyscallNo::Lstat => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::List => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Exit => Ok(()),
        SyscallNo::ProcInfo | SyscallNo::Wait => {
            if a.a0 > MAX_PID || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Sleep => {
            if a.a0 > MAX_SLEEP_TICKS { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::SchedInfo | SyscallNo::PreemptInfo => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Fstat | SyscallNo::FdInfo => {
            if !fd_in_range(a.a0) || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::ReadDir => {
            if !fd_in_range(a.a0) || a.a2 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::FdCtl => {
            if !fd_in_range(a.a0) || (a.a1 != FDCTL_GET && a.a1 != FDCTL_SET) || (a.a2 & !FD_CLOEXEC) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Pipe => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::PipeInfo => {
            if !fd_in_range(a.a0) || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Dup2 => {
            if !fd_in_range(a.a0) || !fd_in_range(a.a1) || (a.a2 & !FD_CLOEXEC) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Poll => {
            if !fd_in_range(a.a0) || a.a1 == 0 || (a.a1 & !(POLL_READ | POLL_WRITE | POLL_HUP)) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::TtyGetInfo => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::TtySetMode => {
            if (a.a0 & !(TTY_MODE_RAW | TTY_MODE_ECHO | TTY_MODE_CANON)) != 0 || ((a.a0 & TTY_MODE_RAW) != 0 && (a.a0 & TTY_MODE_CANON) != 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::TtyReadKey => {
            if a.a0 == 0 || (a.a1 & !TTY_READ_NONBLOCK) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::TtyScroll => {
            let signed = a.a0 as i64;
            if signed >= -1000 && signed <= 1000 { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::TtySetCursor => {
            if a.a0 <= u32::MAX as u64 && a.a1 <= u32::MAX as u64 { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::TtyClearLine | SyscallNo::TtyClear => Ok(()),
        SyscallNo::TtyCursorVisible => {
            if a.a0 <= 1 { Ok(()) } else { Err(VFS_ERR_INVAL) }
        },
        SyscallNo::Theme => {
            if a.a0 == THEME_OP_GET || (a.a0 == THEME_OP_SET && a.a1 < THEME_MAX) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Cred => {
            if valid_cred_args(a) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Sudo => {
            if valid_sudo_args(a) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Chmod => {
            if a.a0 == 0 || !valid_chmod_mode(a.a1) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Chown => {
            if a.a0 == 0 || a.a1 > MAX_PID || a.a2 > MAX_PID { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Kctl => {
            if a.a0 >= KCTL_OP_MAX { Err(VFS_ERR_INVAL) }
            else if kctl_terminal_op(a.a0) { Ok(()) }
            else if a.a1 == 0 || a.a2 == 0 || a.a2 > KCTL_OUT_MAX { Err(VFS_ERR_INVAL) }
            else { Ok(()) }
        }
        SyscallNo::Truncate | SyscallNo::Preallocate => {
            if a.a0 == 0 || a.a1 > MAX_FILE_SIZE { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Rename | SyscallNo::InstallCommit | SyscallNo::Symlink | SyscallNo::Link => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::StatVfs => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Getcwd => {
            if a.a0 == 0 || a.a1 == 0 || a.a1 > MAX_PATH_BYTES { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Readlink => {
            if a.a0 == 0 || (a.a2 != 0 && a.a1 == 0) || a.a2 > MAX_PATH_BYTES { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
    }
}

