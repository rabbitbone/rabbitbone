#[no_mangle]
pub extern "C" fn aurora_rust_syscall_selftest() -> bool {
    if SyscallNo::decode(0) != Ok(SyscallNo::Version) { return false; }
    if SyscallNo::decode(14) != Ok(SyscallNo::Ticks) { return false; }
    if SyscallNo::decode(15) != Ok(SyscallNo::GetPid) { return false; }
    if SyscallNo::decode(16) != Ok(SyscallNo::ProcInfo) { return false; }
    if SyscallNo::decode(17) != Ok(SyscallNo::Spawn) { return false; }
    if SyscallNo::decode(18) != Ok(SyscallNo::Wait) { return false; }
    if SyscallNo::decode(19) != Ok(SyscallNo::Yield) { return false; }
    if SyscallNo::decode(20) != Ok(SyscallNo::Sleep) { return false; }
    if SyscallNo::decode(21) != Ok(SyscallNo::SchedInfo) { return false; }
    if SyscallNo::decode(22) != Ok(SyscallNo::Dup) { return false; }
    if SyscallNo::decode(23) != Ok(SyscallNo::Tell) { return false; }
    if SyscallNo::decode(24) != Ok(SyscallNo::Fstat) { return false; }
    if SyscallNo::decode(25) != Ok(SyscallNo::FdInfo) { return false; }
    if SyscallNo::decode(26) != Ok(SyscallNo::ReadDir) { return false; }
    if SyscallNo::decode(27) != Ok(SyscallNo::SpawnV) { return false; }
    if SyscallNo::decode(28) != Ok(SyscallNo::PreemptInfo) { return false; }
    if SyscallNo::decode(29) != Ok(SyscallNo::Fork) { return false; }
    if SyscallNo::decode(30) != Ok(SyscallNo::Exec) { return false; }
    if SyscallNo::decode(31) != Ok(SyscallNo::ExecV) { return false; }
    if SyscallNo::decode(32) != Ok(SyscallNo::FdCtl) { return false; }
    if SyscallNo::decode(33) != Ok(SyscallNo::ExecVe) { return false; }
    if SyscallNo::decode(34) != Ok(SyscallNo::Pipe) { return false; }
    if SyscallNo::decode(35) != Ok(SyscallNo::PipeInfo) { return false; }
    if SyscallNo::decode(36) != Ok(SyscallNo::Dup2) { return false; }
    if SyscallNo::decode(37) != Ok(SyscallNo::Poll) { return false; }
    if SyscallNo::decode(38) != Ok(SyscallNo::TtyGetInfo) { return false; }
    if SyscallNo::decode(39) != Ok(SyscallNo::TtySetMode) { return false; }
    if SyscallNo::decode(40) != Ok(SyscallNo::TtyReadKey) { return false; }
    if SyscallNo::decode(41) != Ok(SyscallNo::Truncate) { return false; }
    if SyscallNo::decode(42) != Ok(SyscallNo::Rename) { return false; }
    if SyscallNo::decode(43) != Ok(SyscallNo::Sync) { return false; }
    if SyscallNo::decode(44) != Ok(SyscallNo::Fsync) { return false; }
    if SyscallNo::decode(45) != Ok(SyscallNo::StatVfs) { return false; }
    if SyscallNo::decode(46) != Ok(SyscallNo::InstallCommit) { return false; }
    if SyscallNo::decode(47) != Ok(SyscallNo::Preallocate) { return false; }
    if SyscallNo::decode(48) != Ok(SyscallNo::Ftruncate) { return false; }
    if SyscallNo::decode(49) != Ok(SyscallNo::Fpreallocate) { return false; }
    if SyscallNo::decode(50) != Ok(SyscallNo::Chdir) { return false; }
    if SyscallNo::decode(51) != Ok(SyscallNo::Getcwd) { return false; }
    if SyscallNo::decode(52) != Ok(SyscallNo::Fdatasync) { return false; }
    if SyscallNo::decode(53) != Ok(SyscallNo::Symlink) { return false; }
    if SyscallNo::decode(54) != Ok(SyscallNo::Readlink) { return false; }
    if SyscallNo::decode(55) != Ok(SyscallNo::Link) { return false; }
    if SyscallNo::decode(56) != Ok(SyscallNo::Lstat) { return false; }
    if SyscallNo::decode(57) != Ok(SyscallNo::Theme) { return false; }
    if SyscallNo::decode(58) != Ok(SyscallNo::Cred) { return false; }
    if SyscallNo::decode(59) != Ok(SyscallNo::Sudo) { return false; }
    if SyscallNo::decode(60) != Ok(SyscallNo::Chmod) { return false; }
    if SyscallNo::decode(61) != Ok(SyscallNo::Chown) { return false; }
    if SyscallNo::decode(62) != Ok(SyscallNo::Kctl) { return false; }
    if SyscallNo::decode(68) != Ok(SyscallNo::Brk) { return false; }
    if SyscallNo::decode(69) != Ok(SyscallNo::Sbrk) { return false; }
    if SyscallNo::decode(70) != Ok(SyscallNo::Mmap) { return false; }
    if SyscallNo::decode(71) != Ok(SyscallNo::Munmap) { return false; }
    if SyscallNo::decode(72) != Ok(SyscallNo::Mprotect) { return false; }
    if SyscallNo::decode(crate::abi::AURORA_SYS_MAX) != Err(DecodeError::Unsupported) { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 0, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: MAX_CONSOLE_WRITE + 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 0, a1: 0x10000, a2: 1, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if SyscallNo::decode(50) != Ok(SyscallNo::Chdir) { return false; }
    if SyscallNo::decode(51) != Ok(SyscallNo::Getcwd) { return false; }
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
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0x12345, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE | MAP_FIXED, a4: u64::MAX, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_WRITE | PROT_EXEC, a3: MAP_ANON | MAP_PRIVATE, a4: u64::MAX, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: 3, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: MAX_HANDLES, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Mmap, SysArgs { a0: 0, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: MAP_PRIVATE, a4: 3, a5: 1 }).is_ok() { return false; }
    if validate_args(SyscallNo::Munmap, SysArgs { a0: 0x10000, a1: 4096, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Mprotect, SysArgs { a0: 0x10000, a1: 4096, a2: PROT_SUPPORTED & !PROT_EXEC, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }

    true
}
