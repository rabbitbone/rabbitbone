#[repr(C)]
#[derive(Clone, Copy, Eq, PartialEq)]
pub struct SyscallResult {
    pub value: i64,
    pub error: i64,
}

impl SyscallResult {
    pub const fn ok(value: i64) -> Self { Self { value, error: 0 } }
    pub const fn err(error: i64) -> Self { Self { value: -1, error } }
}

#[repr(C)]
#[derive(Clone, Copy, Eq, PartialEq)]
pub struct SysArgs {
    pub a0: u64,
    pub a1: u64,
    pub a2: u64,
    pub a3: u64,
    pub a4: u64,
    pub a5: u64,
}

#[derive(Clone, Copy, Eq, PartialEq)]
#[repr(u64)]
pub enum SyscallNo {
    Version = 0,
    WriteConsole = 1,
    Open = 2,
    Close = 3,
    Read = 4,
    Stat = 5,
    List = 6,
    Exit = 7,
    Log = 8,
    Write = 9,
    Seek = 10,
    Create = 11,
    Mkdir = 12,
    Unlink = 13,
    Ticks = 14,
    GetPid = 15,
    ProcInfo = 16,
    Spawn = 17,
    Wait = 18,
    Yield = 19,
    Sleep = 20,
    SchedInfo = 21,
    Dup = 22,
    Tell = 23,
    Fstat = 24,
    FdInfo = 25,
    ReadDir = 26,
    SpawnV = 27,
    PreemptInfo = 28,
    Fork = 29,
    Exec = 30,
    ExecV = 31,
    FdCtl = 32,
    ExecVe = 33,
    Pipe = 34,
    PipeInfo = 35,
    Dup2 = 36,
    Poll = 37,
    TtyGetInfo = 38,
    TtySetMode = 39,
    TtyReadKey = 40,
    Truncate = 41,
    Rename = 42,
    Sync = 43,
    Fsync = 44,
    StatVfs = 45,
}

#[derive(Clone, Copy, Eq, PartialEq)]
pub enum DecodeError {
    Unsupported,
}

const VFS_ERR_INVAL: i64 = -3;
const VFS_ERR_UNSUPPORTED: i64 = -11;
const MAX_CONSOLE_WRITE: u64 = 65_536;
const MAX_IO_BYTES: u64 = 1_048_576;
const MAX_CREATE_BYTES: u64 = 65_536;
const MAX_HANDLES: u64 = crate::abi::AURORA_PROCESS_HANDLE_CAP;
const MAX_SLEEP_TICKS: u64 = 10_000;
const MAX_PROCESS_ARGS: u64 = 8;
const MAX_PROCESS_ENVS: u64 = crate::abi::AURORA_ENV_MAX;
const FD_CLOEXEC: u64 = crate::abi::AURORA_FD_CLOEXEC;
const FDCTL_GET: u64 = crate::abi::AURORA_FDCTL_GET;
const FDCTL_SET: u64 = crate::abi::AURORA_FDCTL_SET;
const POLL_READ: u64 = crate::abi::AURORA_POLL_READ;
const POLL_WRITE: u64 = crate::abi::AURORA_POLL_WRITE;
const POLL_HUP: u64 = crate::abi::AURORA_POLL_HUP;
const TTY_MODE_RAW: u64 = crate::abi::AURORA_TTY_MODE_RAW;
const TTY_MODE_ECHO: u64 = crate::abi::AURORA_TTY_MODE_ECHO;
const TTY_MODE_CANON: u64 = crate::abi::AURORA_TTY_MODE_CANON;
const TTY_READ_NONBLOCK: u64 = crate::abi::AURORA_TTY_READ_NONBLOCK;

impl SyscallNo {
    pub const fn decode(raw: u64) -> Result<Self, DecodeError> {
        match raw {
            0 => Ok(Self::Version),
            1 => Ok(Self::WriteConsole),
            2 => Ok(Self::Open),
            3 => Ok(Self::Close),
            4 => Ok(Self::Read),
            5 => Ok(Self::Stat),
            6 => Ok(Self::List),
            7 => Ok(Self::Exit),
            8 => Ok(Self::Log),
            9 => Ok(Self::Write),
            10 => Ok(Self::Seek),
            11 => Ok(Self::Create),
            12 => Ok(Self::Mkdir),
            13 => Ok(Self::Unlink),
            14 => Ok(Self::Ticks),
            15 => Ok(Self::GetPid),
            16 => Ok(Self::ProcInfo),
            17 => Ok(Self::Spawn),
            18 => Ok(Self::Wait),
            19 => Ok(Self::Yield),
            20 => Ok(Self::Sleep),
            21 => Ok(Self::SchedInfo),
            22 => Ok(Self::Dup),
            23 => Ok(Self::Tell),
            24 => Ok(Self::Fstat),
            25 => Ok(Self::FdInfo),
            26 => Ok(Self::ReadDir),
            27 => Ok(Self::SpawnV),
            28 => Ok(Self::PreemptInfo),
            29 => Ok(Self::Fork),
            30 => Ok(Self::Exec),
            31 => Ok(Self::ExecV),
            32 => Ok(Self::FdCtl),
            33 => Ok(Self::ExecVe),
            34 => Ok(Self::Pipe),
            35 => Ok(Self::PipeInfo),
            36 => Ok(Self::Dup2),
            37 => Ok(Self::Poll),
            38 => Ok(Self::TtyGetInfo),
            39 => Ok(Self::TtySetMode),
            40 => Ok(Self::TtyReadKey),
            41 => Ok(Self::Truncate),
            42 => Ok(Self::Rename),
            43 => Ok(Self::Sync),
            44 => Ok(Self::Fsync),
            45 => Ok(Self::StatVfs),
            _ => Err(DecodeError::Unsupported),
        }
    }

    pub const fn name(self) -> &'static [u8] {
        match self {
            Self::Version => b"version\0",
            Self::WriteConsole => b"write_console\0",
            Self::Open => b"open\0",
            Self::Close => b"close\0",
            Self::Read => b"read\0",
            Self::Stat => b"stat\0",
            Self::List => b"list\0",
            Self::Exit => b"exit\0",
            Self::Log => b"log\0",
            Self::Write => b"write\0",
            Self::Seek => b"seek\0",
            Self::Create => b"create\0",
            Self::Mkdir => b"mkdir\0",
            Self::Unlink => b"unlink\0",
            Self::Ticks => b"ticks\0",
            Self::GetPid => b"getpid\0",
            Self::ProcInfo => b"procinfo\0",
            Self::Spawn => b"spawn\0",
            Self::Wait => b"wait\0",
            Self::Yield => b"yield\0",
            Self::Sleep => b"sleep\0",
            Self::SchedInfo => b"schedinfo\0",
            Self::Dup => b"dup\0",
            Self::Tell => b"tell\0",
            Self::Fstat => b"fstat\0",
            Self::FdInfo => b"fdinfo\0",
            Self::ReadDir => b"readdir\0",
            Self::SpawnV => b"spawnv\0",
            Self::PreemptInfo => b"preemptinfo\0",
            Self::Fork => b"fork\0",
            Self::Exec => b"exec\0",
            Self::ExecV => b"execv\0",
            Self::FdCtl => b"fdctl\0",
            Self::ExecVe => b"execve\0",
            Self::Pipe => b"pipe\0",
            Self::PipeInfo => b"pipeinfo\0",
            Self::Dup2 => b"dup2\0",
            Self::Poll => b"poll\0",
            Self::TtyGetInfo => b"tty_getinfo\0",
            Self::TtySetMode => b"tty_setmode\0",
            Self::TtyReadKey => b"tty_readkey\0",
            Self::Truncate => b"truncate\0",
            Self::Rename => b"rename\0",
            Self::Sync => b"sync\0",
            Self::Fsync => b"fsync\0",
            Self::StatVfs => b"statvfs\0",
        }
    }
}

extern "C" {
    fn aurora_sys_version() -> SyscallResult;
    fn aurora_sys_write_console(ptr: u64, len: u64) -> SyscallResult;
    fn aurora_sys_open(path: u64) -> SyscallResult;
    fn aurora_sys_close(handle: u64) -> SyscallResult;
    fn aurora_sys_read(handle: u64, buf: u64, len: u64) -> SyscallResult;
    fn aurora_sys_stat(path: u64, stat_out: u64) -> SyscallResult;
    fn aurora_sys_list(path: u64, callback: u64, ctx: u64) -> SyscallResult;
    fn aurora_sys_exit(code: u64) -> SyscallResult;
    fn aurora_sys_log(msg: u64) -> SyscallResult;
    fn aurora_sys_write(handle: u64, buf: u64, len: u64) -> SyscallResult;
    fn aurora_sys_seek(handle: u64, offset: u64, whence: u64) -> SyscallResult;
    fn aurora_sys_create(path: u64, data: u64, len: u64) -> SyscallResult;
    fn aurora_sys_mkdir(path: u64) -> SyscallResult;
    fn aurora_sys_unlink(path: u64) -> SyscallResult;
    fn aurora_sys_ticks() -> SyscallResult;
    fn aurora_sys_getpid() -> SyscallResult;
    fn aurora_sys_procinfo(pid: u64, out: u64) -> SyscallResult;
    fn aurora_sys_spawn(path: u64) -> SyscallResult;
    fn aurora_sys_wait(pid: u64, out: u64) -> SyscallResult;
    fn aurora_sys_yield() -> SyscallResult;
    fn aurora_sys_sleep(ticks: u64) -> SyscallResult;
    fn aurora_sys_schedinfo(out: u64) -> SyscallResult;
    fn aurora_sys_dup(handle: u64) -> SyscallResult;
    fn aurora_sys_tell(handle: u64) -> SyscallResult;
    fn aurora_sys_fstat(handle: u64, out: u64) -> SyscallResult;
    fn aurora_sys_fdinfo(handle: u64, out: u64) -> SyscallResult;
    fn aurora_sys_readdir(handle: u64, index: u64, out: u64) -> SyscallResult;
    fn aurora_sys_spawnv(path: u64, argc: u64, argv: u64) -> SyscallResult;
    fn aurora_sys_preemptinfo(out: u64) -> SyscallResult;
    fn aurora_sys_fork() -> SyscallResult;
    fn aurora_sys_exec(path: u64) -> SyscallResult;
    fn aurora_sys_execv(path: u64, argc: u64, argv: u64) -> SyscallResult;
    fn aurora_sys_fdctl(handle: u64, op: u64, flags: u64) -> SyscallResult;
    fn aurora_sys_execve(path: u64, argc: u64, argv: u64, envc: u64, envp: u64) -> SyscallResult;
    fn aurora_sys_pipe(out: u64) -> SyscallResult;
    fn aurora_sys_pipeinfo(handle: u64, out: u64) -> SyscallResult;
    fn aurora_sys_dup2(src: u64, target: u64, flags: u64) -> SyscallResult;
    fn aurora_sys_poll(handle: u64, events: u64) -> SyscallResult;
    fn aurora_sys_tty_getinfo(out: u64) -> SyscallResult;
    fn aurora_sys_tty_setmode(mode: u64) -> SyscallResult;
    fn aurora_sys_tty_readkey(out: u64, flags: u64) -> SyscallResult;
    fn aurora_sys_truncate(path: u64, size: u64) -> SyscallResult;
    fn aurora_sys_rename(old_path: u64, new_path: u64) -> SyscallResult;
    fn aurora_sys_sync() -> SyscallResult;
    fn aurora_sys_fsync(handle: u64) -> SyscallResult;
    fn aurora_sys_statvfs(path: u64, out: u64) -> SyscallResult;
}

fn valid_handle(h: u64) -> bool { h < MAX_HANDLES }

fn validate_args(no: SyscallNo, a: SysArgs) -> Result<(), i64> {
    match no {
        SyscallNo::Version | SyscallNo::Ticks | SyscallNo::GetPid | SyscallNo::Yield | SyscallNo::Fork | SyscallNo::Sync => Ok(()),
        SyscallNo::WriteConsole => {
            if a.a1 > MAX_CONSOLE_WRITE || (a.a0 == 0 && a.a1 != 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Open | SyscallNo::Mkdir | SyscallNo::Unlink | SyscallNo::Log | SyscallNo::Spawn | SyscallNo::Exec => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::SpawnV | SyscallNo::ExecV => {
            if a.a0 == 0 || a.a1 == 0 || a.a1 > MAX_PROCESS_ARGS || a.a2 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::ExecVe => {
            if a.a0 == 0 || a.a1 == 0 || a.a1 > MAX_PROCESS_ARGS || a.a2 == 0 || a.a3 > MAX_PROCESS_ENVS || (a.a3 != 0 && a.a4 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Close | SyscallNo::Dup | SyscallNo::Tell | SyscallNo::Fsync => {
            if valid_handle(a.a0) { Ok(()) } else { Err(VFS_ERR_INVAL) }
        }
        SyscallNo::Read | SyscallNo::Write => {
            if !valid_handle(a.a0) || a.a2 > MAX_IO_BYTES || (a.a2 != 0 && a.a1 == 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Seek => {
            if !valid_handle(a.a0) || a.a2 != 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Create => {
            if a.a0 == 0 || a.a2 > MAX_CREATE_BYTES || (a.a1 == 0 && a.a2 != 0) { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Stat => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::List => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Exit => Ok(()),
        SyscallNo::ProcInfo | SyscallNo::Wait => {
            if a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
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
        SyscallNo::Truncate => {
            if a.a0 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::Rename => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
        SyscallNo::StatVfs => {
            if a.a0 == 0 || a.a1 == 0 { Err(VFS_ERR_INVAL) } else { Ok(()) }
        }
    }
}

#[no_mangle]
pub extern "C" fn aurora_rust_syscall_dispatch(no: u64, args: SysArgs) -> SyscallResult {
    let decoded = match SyscallNo::decode(no) {
        Ok(v) => v,
        Err(_) => return SyscallResult::err(VFS_ERR_UNSUPPORTED),
    };
    if let Err(e) = validate_args(decoded, args) {
        return SyscallResult::err(e);
    }
    unsafe {
        match decoded {
            SyscallNo::Version => aurora_sys_version(),
            SyscallNo::WriteConsole => aurora_sys_write_console(args.a0, args.a1),
            SyscallNo::Open => aurora_sys_open(args.a0),
            SyscallNo::Close => aurora_sys_close(args.a0),
            SyscallNo::Read => aurora_sys_read(args.a0, args.a1, args.a2),
            SyscallNo::Stat => aurora_sys_stat(args.a0, args.a1),
            SyscallNo::List => aurora_sys_list(args.a0, args.a1, args.a2),
            SyscallNo::Exit => aurora_sys_exit(args.a0),
            SyscallNo::Log => aurora_sys_log(args.a0),
            SyscallNo::Write => aurora_sys_write(args.a0, args.a1, args.a2),
            SyscallNo::Seek => aurora_sys_seek(args.a0, args.a1, args.a2),
            SyscallNo::Create => aurora_sys_create(args.a0, args.a1, args.a2),
            SyscallNo::Mkdir => aurora_sys_mkdir(args.a0),
            SyscallNo::Unlink => aurora_sys_unlink(args.a0),
            SyscallNo::Ticks => aurora_sys_ticks(),
            SyscallNo::GetPid => aurora_sys_getpid(),
            SyscallNo::ProcInfo => aurora_sys_procinfo(args.a0, args.a1),
            SyscallNo::Spawn => aurora_sys_spawn(args.a0),
            SyscallNo::Wait => aurora_sys_wait(args.a0, args.a1),
            SyscallNo::Yield => aurora_sys_yield(),
            SyscallNo::Sleep => aurora_sys_sleep(args.a0),
            SyscallNo::SchedInfo => aurora_sys_schedinfo(args.a0),
            SyscallNo::Dup => aurora_sys_dup(args.a0),
            SyscallNo::Tell => aurora_sys_tell(args.a0),
            SyscallNo::Fstat => aurora_sys_fstat(args.a0, args.a1),
            SyscallNo::FdInfo => aurora_sys_fdinfo(args.a0, args.a1),
            SyscallNo::ReadDir => aurora_sys_readdir(args.a0, args.a1, args.a2),
            SyscallNo::SpawnV => aurora_sys_spawnv(args.a0, args.a1, args.a2),
            SyscallNo::PreemptInfo => aurora_sys_preemptinfo(args.a0),
            SyscallNo::Fork => aurora_sys_fork(),
            SyscallNo::Exec => aurora_sys_exec(args.a0),
            SyscallNo::ExecV => aurora_sys_execv(args.a0, args.a1, args.a2),
            SyscallNo::FdCtl => aurora_sys_fdctl(args.a0, args.a1, args.a2),
            SyscallNo::ExecVe => aurora_sys_execve(args.a0, args.a1, args.a2, args.a3, args.a4),
            SyscallNo::Pipe => aurora_sys_pipe(args.a0),
            SyscallNo::PipeInfo => aurora_sys_pipeinfo(args.a0, args.a1),
            SyscallNo::Dup2 => aurora_sys_dup2(args.a0, args.a1, args.a2),
            SyscallNo::Poll => aurora_sys_poll(args.a0, args.a1),
            SyscallNo::TtyGetInfo => aurora_sys_tty_getinfo(args.a0),
            SyscallNo::TtySetMode => aurora_sys_tty_setmode(args.a0),
            SyscallNo::TtyReadKey => aurora_sys_tty_readkey(args.a0, args.a1),
            SyscallNo::Truncate => aurora_sys_truncate(args.a0, args.a1),
            SyscallNo::Rename => aurora_sys_rename(args.a0, args.a1),
            SyscallNo::Sync => aurora_sys_sync(),
            SyscallNo::Fsync => aurora_sys_fsync(args.a0),
            SyscallNo::StatVfs => aurora_sys_statvfs(args.a0, args.a1),
        }
    }
}

#[no_mangle]
pub extern "C" fn aurora_rust_syscall_name(no: u64) -> *const u8 {
    match SyscallNo::decode(no) {
        Ok(v) => v.name().as_ptr(),
        Err(_) => b"unknown\0".as_ptr(),
    }
}

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
    if SyscallNo::decode(crate::abi::AURORA_SYS_MAX) != Err(DecodeError::Unsupported) { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 0, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: MAX_CONSOLE_WRITE + 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::WriteConsole, SysArgs { a0: 1, a1: 1, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 0, a1: 0x10000, a2: 1, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Read, SysArgs { a0: 1, a1: 0x10000, a2: MAX_IO_BYTES + 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Seek, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Open, SysArgs { a0: 0, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::Create, SysArgs { a0: 1, a1: 0, a2: 1, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
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
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0x10000, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_err() { return false; }
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0, a1: 0x20000, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    if validate_args(SyscallNo::StatVfs, SysArgs { a0: 0x10000, a1: 0, a2: 0, a3: 0, a4: 0, a5: 0 }).is_ok() { return false; }
    true
}
