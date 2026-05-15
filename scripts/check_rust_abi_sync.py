#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI_H = ROOT / "include" / "rabbitbone" / "abi.h"
RUST_TYPES = ROOT / "kernel" / "rust" / "syscall_dispatch" / "types_constants.rs"
RUST_NUMBERS = ROOT / "kernel" / "rust" / "syscall_dispatch" / "number_table.rs"
RUST_SELFTEST = ROOT / "kernel" / "rust" / "syscall_dispatch" / "selftest.rs"

ABI_SYS_RE = re.compile(r"^#define[ \t]+(RABBITBONE_SYS_[A-Z0-9_]+)[ \t]+([0-9]+)u[ \t]*$", re.MULTILINE)
ENUM_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*([0-9]+)\s*,\s*$", re.MULTILINE)
DECODE_RE = re.compile(r"^\s*((?:crate::abi::)?RABBITBONE_SYS_[A-Z0-9_]+|[0-9]+)\s*=>\s*Ok\(Self::([A-Za-z][A-Za-z0-9]*)\),\s*$", re.MULTILINE)
SELFTEST_RE = re.compile(r"SyscallNo::decode\((?:crate::abi::)?(RABBITBONE_SYS_[A-Z0-9_]+|[0-9]+)\)\s*!=\s*Ok\(SyscallNo::([A-Za-z][A-Za-z0-9]*)\)")

VARIANT_OVERRIDES = {
    "WRITE_CONSOLE": "WriteConsole",
    "GETPID": "GetPid",
    "PROCINFO": "ProcInfo",
    "SCHEDINFO": "SchedInfo",
    "FDINFO": "FdInfo",
    "READDIR": "ReadDir",
    "SPAWNV": "SpawnV",
    "PREEMPTINFO": "PreemptInfo",
    "EXECV": "ExecV",
    "EXECVE": "ExecVe",
    "FDCTL": "FdCtl",
    "PIPEINFO": "PipeInfo",
    "DUP2": "Dup2",
    "TTY_GETINFO": "TtyGetInfo",
    "TTY_SETMODE": "TtySetMode",
    "TTY_READKEY": "TtyReadKey",
    "STATVFS": "StatVfs",
    "INSTALL_COMMIT": "InstallCommit",
    "FTRUNCATE": "Ftruncate",
    "FPREALLOCATE": "Fpreallocate",
    "GETCWD": "Getcwd",
    "FDATASYNC": "Fdatasync",
    "READLINK": "Readlink",
    "KCTL": "Kctl",
    "TTY_SCROLL": "TtyScroll",
    "TTY_SETCURSOR": "TtySetCursor",
    "TTY_CLEAR_LINE": "TtyClearLine",
    "TTY_CLEAR": "TtyClear",
    "TTY_CURSOR_VISIBLE": "TtyCursorVisible",
    "BRK": "Brk",
    "SBRK": "Sbrk",
    "MMAP": "Mmap",
    "MUNMAP": "Munmap",
    "MPROTECT": "Mprotect",
    "SIGACTION": "Sigaction",
    "SIGPROCMASK": "Sigprocmask",
    "SIGPENDING": "Sigpending",
    "GETPGRP": "Getpgrp",
    "SETPGID": "Setpgid",
    "GETPGID": "Getpgid",
    "SETSID": "Setsid",
    "GETSID": "Getsid",
    "TCGETPGRP": "Tcgetpgrp",
    "TCSETPGRP": "Tcsetpgrp",
    "SIGRETURN": "Sigreturn",
}


def expected_variant(sys_name: str) -> str:
    suffix = sys_name.removeprefix("RABBITBONE_SYS_")
    if suffix in VARIANT_OVERRIDES:
        return VARIANT_OVERRIDES[suffix]
    return "".join(part.capitalize() for part in suffix.lower().split("_"))


def parse_abi_syscalls() -> list[tuple[str, int, str]]:
    text = ABI_H.read_text(encoding="utf-8")
    out = [(name, int(value), expected_variant(name)) for name, value in ABI_SYS_RE.findall(text)]
    if not out or out[-1][0] != "RABBITBONE_SYS_MAX":
        raise SystemExit("RABBITBONE_SYS_MAX must be the final syscall define in include/rabbitbone/abi.h")
    max_value = out[-1][1]
    syscalls = out[:-1]
    if len(syscalls) != max_value:
        raise SystemExit(f"RABBITBONE_SYS_MAX is {max_value}, but {len(syscalls)} syscall defines were found")
    for index, (name, value, _variant) in enumerate(syscalls):
        if value != index:
            raise SystemExit(f"{name} is {value}, expected dense syscall id {index}")
    return syscalls


def parse_enum() -> dict[str, int]:
    text = RUST_TYPES.read_text(encoding="utf-8")
    matches = ENUM_RE.findall(text)
    if not matches:
        raise SystemExit("could not parse SyscallNo enum discriminants")
    return {variant: int(value) for variant, value in matches}


def parse_decode() -> dict[int, str]:
    text = RUST_NUMBERS.read_text(encoding="utf-8")
    abi_values = {name: value for name, value, _variant in parse_abi_syscalls()}
    matches = DECODE_RE.findall(text)
    if not matches:
        raise SystemExit("could not parse SyscallNo::decode table")
    out: dict[int, str] = {}
    for raw, variant in matches:
        token = raw.removeprefix("crate::abi::")
        value = int(token) if token.isdigit() else abi_values[token]
        out[value] = variant
    return out


def parse_selftest() -> dict[int, str]:
    text = RUST_SELFTEST.read_text(encoding="utf-8")
    abi_values = {name: value for name, value, _variant in parse_abi_syscalls()}
    out: dict[int, str] = {}
    for raw, variant in SELFTEST_RE.findall(text):
        value = int(raw) if raw.isdigit() else abi_values[raw]
        out[value] = variant
    return out


def main() -> int:
    errors: list[str] = []
    syscalls = parse_abi_syscalls()
    rust_numbers_text = RUST_NUMBERS.read_text(encoding="utf-8")
    if re.search(r"^\s*[0-9]+\s*=>", rust_numbers_text, re.MULTILINE):
        errors.append("decode table must use crate::abi::RABBITBONE_SYS_* constants, not raw numeric syscall ids")
    rust_selftest_text = RUST_SELFTEST.read_text(encoding="utf-8")
    if re.search(r"SyscallNo::decode\([0-9]+\)", rust_selftest_text):
        errors.append("Rust syscall selftest must use crate::abi::RABBITBONE_SYS_* constants, not raw numeric syscall ids")
    enum = parse_enum()
    decode = parse_decode()
    selftest = parse_selftest()

    for name, value, variant in syscalls:
        enum_value = enum.get(variant)
        if enum_value != value:
            errors.append(f"SyscallNo::{variant} is {enum_value}, expected {value} from {name}")
        decoded = decode.get(value)
        if decoded != variant:
            errors.append(f"decode({value}) returns {decoded}, expected {variant} from {name}")

    extra_enum = sorted(set(enum) - {variant for _name, _value, variant in syscalls})
    if extra_enum:
        errors.append("SyscallNo enum has variants not present in ABI: " + ", ".join(extra_enum))

    extra_decode = sorted(set(decode) - {value for _name, value, _variant in syscalls})
    if extra_decode:
        errors.append("decode table has ids not present in ABI: " + ", ".join(str(v) for v in extra_decode))

    missing_selftest = [f"{name}/{variant}" for name, value, variant in syscalls if selftest.get(value) != variant]
    if missing_selftest:
        errors.append("Rust syscall selftest is missing ABI decode checks for: " + ", ".join(missing_selftest))

    if errors:
        for error in errors:
            print(f"rust-abi-sync: {error}", file=sys.stderr)
        return 1
    print(f"rust-abi-sync: ok ({len(syscalls)} syscalls)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
