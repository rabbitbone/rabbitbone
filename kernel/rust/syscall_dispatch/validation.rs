fn valid_handle(h: u64) -> bool { h < MAX_HANDLES }

fn validate_args(no: SyscallNo, a: SysArgs) -> Result<(), i64> {
    match no {
        SyscallNo::Version | SyscallNo::Ticks | SyscallNo::GetPid | SyscallNo::Yield | SyscallNo::Fork | SyscallNo::Sync => Ok(()),
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
            if valid_handle(a.a0) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Ftruncate | SyscallNo::Fpreallocate => {
            if valid_handle(a.a0) && a.a1 <= MAX_FILE_SIZE { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Read | SyscallNo::Write => {
            if !valid_handle(a.a0) || a.a2 > MAX_IO_BYTES || (a.a2 != 0 && a.a1 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Seek => {
            if !valid_handle(a.a0) || (a.a2 != SEEK_SET && a.a2 != SEEK_CUR && a.a2 != SEEK_END) { Err(VFS_ERR_INVAL) } else { Ok(()) }
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
            if !valid_handle(a.a0) || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::ReadDir => {
            if !valid_handle(a.a0) || a.a2 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::FdCtl => {
            if !valid_handle(a.a0) || (a.a1 != FDCTL_GET && a.a1 != FDCTL_SET) || (a.a2 & !FD_CLOEXEC) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Pipe => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::PipeInfo => {
            if !valid_handle(a.a0) || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Dup2 => {
            if !valid_handle(a.a0) || !valid_handle(a.a1) || (a.a2 & !FD_CLOEXEC) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Poll => {
            if !valid_handle(a.a0) || a.a1 == 0 || (a.a1 & !(POLL_READ | POLL_WRITE | POLL_HUP)) != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
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
        SyscallNo::Theme => {
            if a.a0 == THEME_OP_GET || (a.a0 == THEME_OP_SET && a.a1 < THEME_MAX) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Cred => {
            if a.a0 <= 4 { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Sudo => {
            if a.a0 <= 4 { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Chmod => {
            if a.a0 == 0 || a.a1 > 0o7777 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Chown => {
            if a.a0 == 0 || a.a1 > MAX_PID || a.a2 > MAX_PID { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Kctl => {
            if a.a0 >= KCTL_OP_MAX || a.a1 == 0 || a.a2 == 0 || a.a2 > KCTL_OUT_MAX { Err(VFS_ERR_INVAL) } else { Ok(()) }
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

