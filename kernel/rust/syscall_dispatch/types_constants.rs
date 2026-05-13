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
    InstallCommit = 46,
    Preallocate = 47,
    Ftruncate = 48,
    Fpreallocate = 49,
    Chdir = 50,
    Getcwd = 51,
    Fdatasync = 52,
    Symlink = 53,
    Readlink = 54,
    Link = 55,
    Lstat = 56,
    Theme = 57,
    Cred = 58,
    Sudo = 59,
    Chmod = 60,
    Chown = 61,
    Kctl = 62,
    TtyScroll = 63,
    TtySetCursor = 64,
    TtyClearLine = 65,
    TtyClear = 66,
    TtyCursorVisible = 67,
    Brk = 68,
    Sbrk = 69,
    Mmap = 70,
    Munmap = 71,
    Mprotect = 72,
    Signal = 73,
    Sigaction = 74,
    Sigprocmask = 75,
    Sigpending = 76,
    Kill = 77,
    Raise = 78,
    Getpgrp = 79,
    Setpgid = 80,
    Getpgid = 81,
    Setsid = 82,
    Getsid = 83,
    Tcgetpgrp = 84,
    Tcsetpgrp = 85,
    Sigreturn = 86,
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
const MAX_PID: u64 = 0xffff_ffff;
const MAX_FILE_SIZE: u64 = 1u64 << 40;
const MAX_PATH_BYTES: u64 = 256;
const MMAP_MAX_BYTES: u64 = 64 * 4096;
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
const OPEN_ACCMODE: u64 = crate::abi::AURORA_O_ACCMODE;
const OPEN_CREAT: u64 = crate::abi::AURORA_O_CREAT;
const OPEN_EXCL: u64 = crate::abi::AURORA_O_EXCL;
const OPEN_TRUNC: u64 = crate::abi::AURORA_O_TRUNC;
const OPEN_RDONLY: u64 = crate::abi::AURORA_O_RDONLY;
const OPEN_SUPPORTED: u64 = OPEN_ACCMODE | OPEN_CREAT | OPEN_EXCL | OPEN_TRUNC | crate::abi::AURORA_O_APPEND | crate::abi::AURORA_O_DIRECTORY | crate::abi::AURORA_O_CLOEXEC;
const SEEK_SET: u64 = crate::abi::AURORA_SEEK_SET;
const SEEK_CUR: u64 = crate::abi::AURORA_SEEK_CUR;
const SEEK_END: u64 = crate::abi::AURORA_SEEK_END;
const THEME_OP_GET: u64 = crate::abi::AURORA_THEME_OP_GET;
const THEME_OP_SET: u64 = crate::abi::AURORA_THEME_OP_SET;
const THEME_MAX: u64 = crate::abi::AURORA_THEME_MAX;
const PROT_WRITE: u64 = crate::abi::AURORA_PROT_WRITE;
const PROT_EXEC: u64 = crate::abi::AURORA_PROT_EXEC;
const PROT_SUPPORTED: u64 = crate::abi::AURORA_PROT_READ | PROT_WRITE | PROT_EXEC;
const MAP_ANON: u64 = crate::abi::AURORA_MAP_ANON;
const MAP_PRIVATE: u64 = crate::abi::AURORA_MAP_PRIVATE;
const MAP_FIXED: u64 = crate::abi::AURORA_MAP_FIXED;
const MAP_SHARED: u64 = crate::abi::AURORA_MAP_SHARED;
const MAP_SUPPORTED: u64 = MAP_ANON | MAP_PRIVATE | MAP_FIXED | MAP_SHARED;


const KCTL_OP_MAX: u64 = crate::abi::AURORA_KCTL_OP_MAX;
const KCTL_OUT_MAX: u64 = crate::abi::AURORA_KCTL_OUT_MAX;

const NSIG: u64 = crate::abi::AURORA_NSIG;
const SIG_SETMASK: u64 = crate::abi::AURORA_SIG_SETMASK;
