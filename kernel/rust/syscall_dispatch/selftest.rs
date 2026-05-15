#[no_mangle]
pub extern "C" fn rabbitbone_rust_syscall_selftest() -> bool {
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_VERSION) != Ok(SyscallNo::Version) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_WRITE_CONSOLE) != Ok(SyscallNo::WriteConsole) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_OPEN) != Ok(SyscallNo::Open) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CLOSE) != Ok(SyscallNo::Close) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_READ) != Ok(SyscallNo::Read) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_STAT) != Ok(SyscallNo::Stat) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_LIST) != Ok(SyscallNo::List) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_EXIT) != Ok(SyscallNo::Exit) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_LOG) != Ok(SyscallNo::Log) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_WRITE) != Ok(SyscallNo::Write) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SEEK) != Ok(SyscallNo::Seek) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CREATE) != Ok(SyscallNo::Create) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_MKDIR) != Ok(SyscallNo::Mkdir) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_UNLINK) != Ok(SyscallNo::Unlink) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TICKS) != Ok(SyscallNo::Ticks) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_GETPID) != Ok(SyscallNo::GetPid) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_PROCINFO) != Ok(SyscallNo::ProcInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SPAWN) != Ok(SyscallNo::Spawn) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_WAIT) != Ok(SyscallNo::Wait) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_YIELD) != Ok(SyscallNo::Yield) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SLEEP) != Ok(SyscallNo::Sleep) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SCHEDINFO) != Ok(SyscallNo::SchedInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_DUP) != Ok(SyscallNo::Dup) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TELL) != Ok(SyscallNo::Tell) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FSTAT) != Ok(SyscallNo::Fstat) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FDINFO) != Ok(SyscallNo::FdInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_READDIR) != Ok(SyscallNo::ReadDir) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SPAWNV) != Ok(SyscallNo::SpawnV) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_PREEMPTINFO) != Ok(SyscallNo::PreemptInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FORK) != Ok(SyscallNo::Fork) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_EXEC) != Ok(SyscallNo::Exec) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_EXECV) != Ok(SyscallNo::ExecV) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FDCTL) != Ok(SyscallNo::FdCtl) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_EXECVE) != Ok(SyscallNo::ExecVe) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_PIPE) != Ok(SyscallNo::Pipe) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_PIPEINFO) != Ok(SyscallNo::PipeInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_DUP2) != Ok(SyscallNo::Dup2) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_POLL) != Ok(SyscallNo::Poll) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_GETINFO) != Ok(SyscallNo::TtyGetInfo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_SETMODE) != Ok(SyscallNo::TtySetMode) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_READKEY) != Ok(SyscallNo::TtyReadKey) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TRUNCATE) != Ok(SyscallNo::Truncate) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_RENAME) != Ok(SyscallNo::Rename) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SYNC) != Ok(SyscallNo::Sync) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FSYNC) != Ok(SyscallNo::Fsync) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_STATVFS) != Ok(SyscallNo::StatVfs) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_INSTALL_COMMIT) != Ok(SyscallNo::InstallCommit) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_PREALLOCATE) != Ok(SyscallNo::Preallocate) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FTRUNCATE) != Ok(SyscallNo::Ftruncate) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FPREALLOCATE) != Ok(SyscallNo::Fpreallocate) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CHDIR) != Ok(SyscallNo::Chdir) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_GETCWD) != Ok(SyscallNo::Getcwd) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_FDATASYNC) != Ok(SyscallNo::Fdatasync) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SYMLINK) != Ok(SyscallNo::Symlink) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_READLINK) != Ok(SyscallNo::Readlink) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_LINK) != Ok(SyscallNo::Link) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_LSTAT) != Ok(SyscallNo::Lstat) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_THEME) != Ok(SyscallNo::Theme) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CRED) != Ok(SyscallNo::Cred) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SUDO) != Ok(SyscallNo::Sudo) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CHMOD) != Ok(SyscallNo::Chmod) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_CHOWN) != Ok(SyscallNo::Chown) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_KCTL) != Ok(SyscallNo::Kctl) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_SCROLL) != Ok(SyscallNo::TtyScroll) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_SETCURSOR) != Ok(SyscallNo::TtySetCursor) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_CLEAR_LINE) != Ok(SyscallNo::TtyClearLine) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_CLEAR) != Ok(SyscallNo::TtyClear) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TTY_CURSOR_VISIBLE) != Ok(SyscallNo::TtyCursorVisible) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_BRK) != Ok(SyscallNo::Brk) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SBRK) != Ok(SyscallNo::Sbrk) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_MMAP) != Ok(SyscallNo::Mmap) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_MUNMAP) != Ok(SyscallNo::Munmap) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_MPROTECT) != Ok(SyscallNo::Mprotect) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SIGNAL) != Ok(SyscallNo::Signal) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SIGACTION) != Ok(SyscallNo::Sigaction) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SIGPROCMASK) != Ok(SyscallNo::Sigprocmask) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SIGPENDING) != Ok(SyscallNo::Sigpending) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_KILL) != Ok(SyscallNo::Kill) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_RAISE) != Ok(SyscallNo::Raise) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_GETPGRP) != Ok(SyscallNo::Getpgrp) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SETPGID) != Ok(SyscallNo::Setpgid) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_GETPGID) != Ok(SyscallNo::Getpgid) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SETSID) != Ok(SyscallNo::Setsid) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_GETSID) != Ok(SyscallNo::Getsid) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TCGETPGRP) != Ok(SyscallNo::Tcgetpgrp) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_TCSETPGRP) != Ok(SyscallNo::Tcsetpgrp) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_SIGRETURN) != Ok(SyscallNo::Sigreturn) { return false; }
    if SyscallNo::decode(crate::abi::RABBITBONE_SYS_MAX) != Err(DecodeError::Unsupported) { return false; }
    if validate_args(SyscallNo::Raise, SysArgs { a0: crate::abi::RABBITBONE_SIGUSR1 as u64, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Raise, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 1, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: (-1i64) as u64, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: (-2i64) as u64, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0xffff_ffffu64, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0xffff_fffeu64, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0x8000_0000u64, a1: crate::abi::RABBITBONE_SIGKILL as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0x0000_0001_0000_0000u64, a1: crate::abi::RABBITBONE_SIGTERM as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 1, a1: crate::abi::RABBITBONE_NSIG as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0xffff_ffff_8000_0000, a1: crate::abi::RABBITBONE_SIGKILL as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kill, SysArgs { a0: 0xffff_ffff_ffff_ffff, a1: crate::abi::RABBITBONE_SIGKILL as u64, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sigprocmask, SysArgs { a0: crate::abi::RABBITBONE_SIG_SETMASK as u64, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sigpending, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Tcsetpgrp, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 0, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: MAX_CONSOLE_WRITE + 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 0, a1: 0x10000, a2: 1, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 1, a1: 0x10000, a2: MAX_IO_BYTES + 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Seek, SysArgs { a0: 1, a1: 0, a2: SEEK_CUR, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Seek, SysArgs { a0: 1, a1: 0, a2: 99, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Open, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Open, SysArgs { a0: 1, a1: OPEN_EXCL, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Open, SysArgs { a0: 1, a1: OPEN_TRUNC | OPEN_RDONLY, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Open, SysArgs { a0: 1, a1: OPEN_CREAT | OPEN_EXCL, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Create, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Preallocate, SysArgs { a0: 0, a1: 4096, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Preallocate, SysArgs { a0: 1, a1: 4096, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Ftruncate, SysArgs { a0: MAX_HANDLES, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Ftruncate, SysArgs { a0: 1, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Fpreallocate, SysArgs { a0: 1, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Chdir, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Chdir, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Getcwd, SysArgs { a0: 0, a1: MAX_PATH_BYTES, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Getcwd, SysArgs { a0: 0x10000, a1: MAX_PATH_BYTES + 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Getcwd, SysArgs { a0: 0x10000, a1: MAX_PATH_BYTES, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }

    if validate_args(SyscallNo::ProcInfo, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ProcInfo, SysArgs { a0: 0, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Spawn, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Exec, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Exec, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::SpawnV, SysArgs { a0: 1, a1: 0, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::SpawnV, SysArgs { a0: 1, a1: MAX_PROCESS_ARGS + 1, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::SpawnV, SysArgs { a0: 1, a1: 2, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::SpawnV, SysArgs { a0: 1, a1: 2, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::ExecV, SysArgs { a0: 1, a1: 0, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ExecV, SysArgs { a0: 1, a1: MAX_PROCESS_ARGS + 1, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ExecV, SysArgs { a0: 1, a1: 2, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ExecV, SysArgs { a0: 1, a1: 2, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::ExecVe, SysArgs { a0: 1, a1: 2, a2: 0x10000, a3: MAX_PROCESS_ENVS + 1, a4: 0x20000, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ExecVe, SysArgs { a0: 1, a1: 2, a2: 0x10000, a3: 1, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ExecVe, SysArgs { a0: 1, a1: 2, a2: 0x10000, a3: 1, a4: 0x20000, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Wait, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Wait, SysArgs { a0: 1, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sleep, SysArgs { a0: MAX_SLEEP_TICKS + 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Sleep, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::SchedInfo, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::SchedInfo, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::PreemptInfo, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::PreemptInfo, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Dup, SysArgs { a0: MAX_HANDLES - 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Close, SysArgs { a0: MAX_HANDLES - 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::FdInfo, SysArgs { a0: MAX_HANDLES - 1, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Dup2, SysArgs { a0: 1, a1: MAX_HANDLES - 1, a2: FD_CLOEXEC, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Dup, SysArgs { a0: MAX_HANDLES, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Dup, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::FdCtl, SysArgs { a0: 1, a1: FDCTL_SET, a2: FD_CLOEXEC, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::FdCtl, SysArgs { a0: 1, a1: 7, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Pipe, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Pipe, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::PipeInfo, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::PipeInfo, SysArgs { a0: 1, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Dup2, SysArgs { a0: 1, a1: 2, a2: FD_CLOEXEC, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Dup2, SysArgs { a0: 1, a1: MAX_HANDLES, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Dup2, SysArgs { a0: 1, a1: 2, a2: FD_CLOEXEC << 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Poll, SysArgs { a0: 1, a1: POLL_READ | POLL_WRITE, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Poll, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Fstat, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::FdInfo, SysArgs { a0: 1, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::ReadDir, SysArgs { a0: 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::ReadDir, SysArgs { a0: 1, a1: 0, a2: 0x10000, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::TtyGetInfo, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::TtyGetInfo, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::TtySetMode, SysArgs { a0: TTY_MODE_RAW | TTY_MODE_CANON, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::TtyReadKey, SysArgs { a0: 0x10000, a1: TTY_READ_NONBLOCK, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::TtySetCursor, SysArgs { a0: 127, a1: 47, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::TtySetCursor, SysArgs { a0: (u32::MAX as u64) + 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Truncate, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Truncate, SysArgs { a0: 0x10000, a1: 7, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Rename, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Rename, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }

    if validate_args(SyscallNo::Sync, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Fsync, SysArgs { a0: MAX_HANDLES - 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Fsync, SysArgs { a0: MAX_HANDLES, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Fdatasync, SysArgs { a0: MAX_HANDLES - 1, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Fdatasync, SysArgs { a0: MAX_HANDLES, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Symlink, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Symlink, SysArgs { a0: 0, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Readlink, SysArgs { a0: 0x10000, a1: 0x20000, a2: MAX_PATH_BYTES, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Readlink, SysArgs { a0: 0x10000, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Link, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Lstat, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::InstallCommit, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::InstallCommit, SysArgs { a0: 0, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::InstallCommit, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }

    if validate_args(SyscallNo::Cred, SysArgs { a0: 0, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Cred, SysArgs { a0: 4, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Cred, SysArgs { a0: 5, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Sudo, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sudo, SysArgs { a0: 4, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sudo, SysArgs { a0: 5, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: 0, a1: 0x10000, a2: KCTL_OUT_MAX, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: KCTL_OP_MAX, a1: 0x10000, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: 0, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: 0, a1: 0x10000, a2: KCTL_OUT_MAX + 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Brk, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sbrk, SysArgs { a0: 0xffff_ffff_ffff_ffff, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE, a4: u64::MAX, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_ANON | MAP_SHARED, a4: u64::MAX, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE | MAP_SHARED, a4: u64::MAX, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0x12345, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE | MAP_FIXED, a4: u64::MAX, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_WRITE | PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE, a4: u64::MAX, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: 3, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_SHARED, a4: 3, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: MAX_HANDLES, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: 3, a5: 1 }).is_ok() { return false; }
    if validate_args(SyscallNo::Munmap, SysArgs { a0: 0x10000, a1: 4096, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mprotect, SysArgs { a0: 0x10000, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mprotect, SysArgs { a0: 0x10000, a1: 4096, a2: PROT_WRITE | PROT_EXEC, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Munmap, SysArgs { a0: 0x10001, a1: 4096, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }

    if validate_args(SyscallNo::Chmod, SysArgs { a0: 0x10000, a1: 0o1777, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Chmod, SysArgs { a0: 0x10000, a1: 0o2000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Chmod, SysArgs { a0: 0x10000, a1: 0o7777, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }

    if validate_args(SyscallNo::Cred, SysArgs { a0: CRED_OP_GET, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Cred, SysArgs { a0: CRED_OP_LOGIN, a1: 0x10000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Cred, SysArgs { a0: CRED_OP_LOGIN, a1: 0x10000, a2: 0x20000, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Cred, SysArgs { a0: CRED_OP_SET_EUID, a1: 0, a2: 0, a3: (u32::MAX as u64) + 1, a4: 0, a5: 0 }).is_ok() { return false; }

    if validate_args(SyscallNo::Sudo, SysArgs { a0: SUDO_OP_VALIDATE, a1: 0, a2: SUDO_FLAG_ACTIVATE | SUDO_FLAG_PERSIST, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Sudo, SysArgs { a0: SUDO_OP_VALIDATE, a1: 0, a2: SUDO_FLAG_PERSIST << 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Sudo, SysArgs { a0: SUDO_OP_SET_TIMEOUT, a1: 0, a2: SUDO_MAX_TTL_TICKS + 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }

    if validate_args(SyscallNo::Kctl, SysArgs { a0: KCTL_OP_PANIC, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: KCTL_OP_REBOOT, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Kctl, SysArgs { a0: KCTL_OP_HALT, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }

    true
}
