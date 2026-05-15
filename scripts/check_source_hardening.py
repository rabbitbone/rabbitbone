#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def check_paths() -> None:
    c_path = text("kernel/vfs/path.c")
    rust_path = text("kernel/rust/vfs_route.rs")
    for needle in ("char comps[32]", "*count >= 32", "count >= 32"):
        require(needle not in c_path, f"C path normalizer still has fixed component cap: {needle}")
    for needle in ("[[0u8; VFS_NAME_MAX]; 32]", "saturating_sub(1)"):
        require(needle not in rust_path, f"Rust route normalizer still has unsafe legacy path behavior: {needle}")
    require("path_pop_component" in c_path, "C path normalizer must reject attempts to escape above root")
    require("path_component_byte_allowed" in c_path and "c != '\\\\'" in c_path,
            "C path normalizer must reject unsafe component bytes")
    require("pop_component(&mut out" in rust_path, "Rust path normalizer must reject attempts to escape above root")


def check_auth() -> None:
    auth = text("kernel/sys/syscall/auth_syscalls.inc")
    forbidden = [
        '"root", "root"',
        '"rabbitbone", "rabbitbone"',
        '"guest", "guest"',
        "const char *password;",
    ]
    for needle in forbidden:
        require(needle not in auth, f"auth still contains plaintext/static password field: {needle}")
    require("password_hash" in auth and "password_salt" in auth, "auth table must store salted password hashes")
    require("memzero_explicit" in auth, "auth password buffers must be wiped through memzero_explicit")


def kernel_source_files():
    for path in ROOT.joinpath("kernel").rglob("*"):
        if path.is_file() and path.suffix in {".c", ".h", ".inc"}:
            yield path


def check_string_api() -> None:
    libc_h = text("kernel/include/rabbitbone/libc.h")
    libc_c = text("kernel/lib/string.c")
    for name in ("strlcpy", "strlcat", "memzero_explicit"):
        require(name in libc_h and name in libc_c, f"kernel libc missing hardened helper {name}")
    allowed = {"kernel/lib/string.c", "kernel/include/rabbitbone/libc.h", "kernel/core/ktest/tarfs_tests.inc"}
    dangerous = ("strncpy", "strcpy", "strcat", "sprintf", "vsprintf", "gets")
    offenders = []
    for path in kernel_source_files():
        rel = path.relative_to(ROOT).as_posix()
        if rel in allowed:
            continue
        data = path.read_text(encoding="utf-8")
        for name in dangerous:
            if re.search(rf"\b{name}\s*\(", data):
                offenders.append(f"{rel}:{name}")
    require(not offenders, "dangerous string callsites remain: " + ", ".join(offenders))


def check_elf_and_pipes() -> None:
    elf = text("kernel/exec/elf64.c")
    pipes = text("kernel/sys/syscall/pipes.inc")
    require("entry_in_exec_segment" in elf, "ELF loader must verify entrypoint lands inside an executable PT_LOAD segment")
    require("count_pipe_endpoints(handles) >= SYSCALL_PIPE_PER_PROCESS_ENDPOINT_CAP" in pipes,
            "pipe handle allocator must enforce per-process pipe endpoint cap itself")
    for needle in ("pipe_write_by_id", "pipe_read_by_id", "pipe_snapshot_by_id", "pipe_poll_by_id"):
        require(needle in pipes, f"pipe subsystem missing locked wrapper {needle}")
    forbidden = []
    allowed_pipe_by_id = {"kernel/sys/syscall/pipes.inc", "kernel/sys/syscall/files_and_handles.inc"}
    for path in ROOT.joinpath("kernel/sys/syscall").rglob("*"):
        if not path.is_file() or path.suffix not in {".c", ".h", ".inc"}:
            continue
        rel = path.relative_to(ROOT).as_posix()
        data = path.read_text(encoding="utf-8")
        if rel not in allowed_pipe_by_id and re.search(r"\bpipe_by_id\s*\(", data):
            forbidden.append(f"{rel}:pipe_by_id")
        if rel != "kernel/sys/syscall/pipes.inc":
            for name in ("pipe_read_bytes", "pipe_write_bytes", "pipe_snapshot"):
                if re.search(rf"\b{name}\s*\(", data):
                    forbidden.append(f"{rel}:{name}")
    require(not forbidden, "raw pipe object access escapes locked wrappers: " + ", ".join(forbidden))



def check_open_files() -> None:
    lifecycle = text("kernel/sys/syscall/lifecycle.inc")
    files = text("kernel/sys/syscall/files_and_handles.inc")
    require("handle_file_acquire" in lifecycle and "file_acquire" in files and "file_put" in files,
            "open-file subsystem must use acquire/release wrappers around global file table objects")
    forbidden = []
    for path in ROOT.joinpath("kernel/sys/syscall").rglob("*"):
        if not path.is_file() or path.suffix not in {".c", ".h", ".inc"}:
            continue
        rel = path.relative_to(ROOT).as_posix()
        data = path.read_text(encoding="utf-8")
        if re.search(r"\bhandle_file\s*\(", data):
            forbidden.append(f"{rel}:handle_file")
        if rel not in {"kernel/sys/syscall/lifecycle.inc", "kernel/sys/syscall/files_and_handles.inc"} and re.search(r"\bfile_by_id\s*\(", data):
            forbidden.append(f"{rel}:file_by_id")
    require(not forbidden, "raw open-file object access escapes acquire/release wrappers: " + ", ".join(forbidden))


def check_tarfs_headers() -> None:
    tar = text("kernel/vfs/tarfs/header_parse.inc")
    require("tar_name_byte_allowed" in tar and "tar_name_invalid" in tar,
            "tarfs header parser must validate archive path bytes/components before normalization")
    require("s[0] == '/'" in tar and "c != '\\\\'" in tar,
            "tarfs header parser must reject absolute or backslash-containing archive names")


def check_shell_redirections() -> None:
    internal = text("user/bin/rbsh/internal.inc")
    parse = text("user/bin/rbsh/parse.inc")
    runtime = text("user/bin/rbsh/runtime.inc")
    require("sh_redir_t redirs[SH_REDIR_MAX]" in internal, "rbsh must keep ordered redirection actions")
    require("sh_add_redir_path" in parse and "sh_add_redir_dup" in parse, "rbsh parser must record redirections in parse order")
    require("for (unsigned int i = 0; i < stage->redir_count; ++i)" in runtime, "rbsh runtime must apply redirections in parse order")
    legacy_order = "if (stage->in_path[0]) {"
    require(legacy_order not in runtime, "rbsh runtime still applies grouped legacy redirections instead of ordered actions")
    require("SH_RM_MAX_DEPTH" in internal and "depth > 32u" not in text("user/bin/rbsh/builtins_fs.inc"),
            "rbsh rm -r must not keep the old arbitrary 32-level recursion cap")


def check_syscall_boundary_validation() -> None:
    rust = text("kernel/rust/syscall_dispatch/validation.rs")
    rust_consts = text("kernel/rust/syscall_dispatch/types_constants.rs")
    rust_selftest = text("kernel/rust/syscall_dispatch/selftest.rs")
    mmap = text("kernel/sys/syscall/memory_syscalls.inc")

    for needle in (
        "fn fd_in_range",
        "fn valid_chmod_mode",
        "fn kctl_terminal_op",
        "fn valid_cred_args",
        "fn valid_sudo_args",
    ):
        require(needle in rust, f"Rust syscall validator missing hardened helper: {needle}")
    require("valid_handle(" not in rust, "Rust syscall validator still uses misleading valid_handle name")

    for needle in (
        "RABBITBONE_CRED_OP_LOGIN",
        "RABBITBONE_SUDO_OP_VALIDATE",
        "RABBITBONE_SUDO_MAX_TTL_TICKS",
        "RABBITBONE_KCTL_OP_PANIC",
        "CHMOD_FORBIDDEN_MODE",
    ):
        require(needle in rust_consts, f"Rust syscall constants missing ABI-backed symbol: {needle}")

    require("!valid_chmod_mode(a.a1)" in rust, "Rust chmod validation must mirror C mode mask policy")
    require("kctl_terminal_op(a.a0)" in rust, "Rust kctl validation must allow terminal ops without fake output buffers")
    require("CRED_OP_LOGIN => a.a1 != 0 && a.a2 != 0" in rust, "Rust cred validation must require login name and password pointers")
    require("SUDO_OP_SET_TIMEOUT => a.a1 == 0 && a.a2 <= SUDO_MAX_TTL_TICKS" in rust, "Rust sudo validation must cap timeout at ABI maximum")
    require("SUDO_OP_VALIDATE => (a.a2 & !(SUDO_FLAG_ACTIVATE | SUDO_FLAG_PERSIST)) == 0" in rust, "Rust sudo validation must reject unknown flags")

    for needle in (
        "sys_mmap_prot_valid",
        "sys_mmap_flags_valid",
        "sys_mmap_addr_valid",
        "sys_user_mapping_range_valid",
    ):
        require(needle in mmap, f"C mmap wrapper missing direct validation helper: {needle}")
    require("RABBITBONE_PROT_WRITE) && (prot & RABBITBONE_PROT_EXEC" in mmap, "C mmap/mprotect validation must reject W+X mappings")
    require("sharing == RABBITBONE_MAP_PRIVATE || sharing == RABBITBONE_MAP_SHARED" in mmap, "C mmap validation must require exactly one sharing mode")
    require("!sys_user_mapping_range_valid(addr, length)" in mmap, "C munmap/mprotect wrappers must reject null or unaligned ranges before VM layer")

    for needle in (
        "KCTL_OP_PANIC",
        "SUDO_MAX_TTL_TICKS + 1",
        "CRED_OP_SET_EUID",
        "0o2000",
        "PROT_WRITE | PROT_EXEC",
        "0x10001",
    ):
        require(needle in rust_selftest, f"Rust syscall selftest missing hardened validation case: {needle}")

def main() -> int:
    check_paths()
    check_auth()
    check_string_api()
    check_elf_and_pipes()
    check_open_files()
    check_tarfs_headers()
    check_shell_redirections()
    check_syscall_boundary_validation()
    print("source-hardening: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
