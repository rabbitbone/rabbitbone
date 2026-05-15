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
    require("route_component_byte_allowed" in rust_path and ".checked_add(" in rust_path and r"bad\\name" in rust_path,
            "Rust VFS route normalizer must reject unsafe component bytes and use checked output arithmetic")


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
    require("saw_real_component" in tar and "return !saw_real_component" in tar and "strcmp(out, \"/\") != 0" in tar,
            "tarfs must accept benign ./ components but reject archive entries that canonicalize to root")


def check_shell_redirections() -> None:
    internal = text("user/bin/rbsh/internal.inc")
    parse = text("user/bin/rbsh/parse.inc")
    runtime = text("user/bin/rbsh/runtime.inc")
    require("sh_redir_t redirs[SH_REDIR_MAX]" in internal, "rbsh must keep ordered redirection actions")
    require("sh_add_redir_path" in parse and "sh_add_redir_dup" in parse, "rbsh parser must record redirections in parse order")
    require("for (unsigned int i = 0; i < stage->redir_count; ++i)" in runtime, "rbsh runtime must apply redirections in parse order")
    require("sh_close_saved_redirs(saves);" in runtime and "if (fd > RABBITBONE_STDERR) {" in runtime,
            "rbsh must close partially duplicated builtin redirection saves on failure")
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


def check_process_lifetime_and_usercopy() -> None:
    internal = text("kernel/proc/process/internal.h")
    lifecycle = text("kernel/proc/process/lifecycle_state.inc")
    memory = text("kernel/proc/process/memory_copy.inc")
    spawn = text("kernel/proc/process/spawn_fork.inc")
    syscall_h = text("kernel/include/rabbitbone/syscall.h")
    sys_life = text("kernel/sys/syscall/lifecycle.inc")

    require("process_user_access_allowed_at" in internal and "process_user_access_allowed_at" in lifecycle,
            "process usercopy validation must check VMA permissions for already-materialized pages")
    require("process_vma_prot_allows" in memory and "process_find_vma_for_page_const(p, page_virt)" in memory,
            "process usercopy permission helper must derive access from VMA protections")
    require("process_drop_vmas" in internal and "process_drop_vmas(p);" in lifecycle,
            "process slot teardown must release VMA backing references")
    require("process_drop_vmas(&current_proc);" in spawn,
            "process exit must drop VMA backing references, not wait for reap")
    loader = text("kernel/proc/process/loader_stack.inc")
    require("process_drop_vmas(exec_old_proc);" in loader and "process_drop_vmas(exec_replacement);" in loader,
            "exec commit/discard must drop old or abandoned VMA backing references")
    require(loader.count("process_drop_vmas(") >= 4,
            "process load/exec failure paths must drop VMA backing references")
    require("bool syscall_retain_user_handle_snapshot(void *src" in syscall_h and "memset(handles, 0, SYSCALL_HANDLE_TABLE_BYTES);" in sys_life,
            "failed handle snapshot retain must sanitize the snapshot to prevent later double-release")
    require("release_process_slot(slot);" in spawn,
            "fork/spawn failure paths must use full process resource teardown")


def check_checked_alignment_edges() -> None:
    memory = text("kernel/proc/process/memory_copy.inc")
    loader = text("kernel/proc/process/loader_stack.inc")
    tar = text("kernel/vfs/tarfs/header_parse.inc")
    require("rabbitbone_align_up_u64_checked((u64)raw_end" in memory,
            "munmap/mprotect range normalization must use checked alignment")
    require("process_align_up_page_checked" in memory and "!process_align_up_page_checked(heap_base, &heap_base)" in memory and
            "!process_align_up_page_checked(p->heap_break, &old_end)" in memory and
            "!process_align_up_page_checked(v->end, &next)" in memory,
            "process heap/mmap placement must use checked page alignment instead of wrapped ALIGN_UP")
    require("RABBITBONE_ALIGN_UP(" not in memory,
            "process VM code must not use unchecked ALIGN_UP on user-controlled or metadata-derived values")
    require("__builtin_add_overflow(start, (uptr)size, &end)" in memory,
            "process mmap must check final start+size arithmetic before storing a VMA range")
    require("rabbitbone_align_up_u64_checked(seg_end64" in loader and "__builtin_add_overflow(run_end, (uptr)PAGE_SIZE, &run_end)" in loader,
            "ELF segment span and image VMA coalescing must use checked arithmetic")
    require("tar_align_512_checked" in tar and "RABBITBONE_ALIGN_UP(fsize" not in tar,
            "tarfs payload alignment must reject overflow instead of relying on wrapped ALIGN_UP")



def check_ext4_metadata_and_signals() -> None:
    ext4_internal = text("kernel/fs/ext4/internal.h")
    ext4_diridx = text("kernel/fs/ext4/directory_index.inc")
    ext4_meta = text("kernel/fs/ext4/directory_metadata_validation.inc")
    ext4_list = text("kernel/fs/ext4/rename_ops.inc")
    ext4_lookup = text("kernel/fs/ext4/lookup_symlink.inc")
    signals = text("kernel/proc/process/signals_jobs.inc")
    loader = text("kernel/proc/process/loader_stack.inc")
    sys_sig = text("kernel/sys/syscall/signals_jobs_syscalls.inc")
    rust_val = text("kernel/rust/syscall_dispatch/validation.rs")
    rust_selftest = text("kernel/rust/syscall_dispatch/selftest.rs")

    for needle in (
        "ext4_name_byte_allowed",
        "ext4_name_bytes_valid",
        "ext4_name_cstr_valid",
        "ext4_symlink_target_valid",
    ):
        require(needle in ext4_internal, f"ext4 name hardening helper missing: {needle}")
    require("c != (u8)'\\\\'" in ext4_internal and "c >= 0x20u" in ext4_internal and "c != 0x7fu" in ext4_internal,
            "ext4 name validator must reject backslash/control/DEL bytes")
    require("ext4_name_bytes_valid(path + slash, name_len, false)" in ext4_diridx,
            "ext4 split_parent_path must validate leaf name bytes")
    require("!ext4_name_bytes_valid(de->name, de->name_len, true)" in ext4_meta and "!ext4_name_bytes_valid(de->name, de->name_len, true)" in ext4_list,
            "ext4 directory metadata/listing must reject corrupt raw dirent names")
    require("!ext4_name_cstr_valid(name, true)" in ext4_lookup,
            "ext4 direct directory lookup must validate lookup names")
    require("name[len]" not in ext4_internal and "len > EXT4_NAME_LEN" in ext4_internal,
            "ext4 C-string name validation must not read one byte past an overlong unterminated name")
    require("le16(hdr->max_entries) == 0" in ext4_diridx and "de->file_type == he->file_type" in ext4_diridx,
            "ext4 HTree lookup/load must reject zero-capacity indexes and mismatched/corrupt dirents")
    require("ext4_symlink_target_byte_allowed" in ext4_internal and "c != (u8)'\\\\'" in ext4_internal,
            "ext4 symlink target validator must reject unsafe target bytes")
    require("!ext4_symlink_target_valid(target, len)" in text("kernel/fs/ext4/node_create.inc") and
            "!ext4_symlink_target_valid(target, got)" in ext4_lookup,
            "ext4 symlink create/follow paths must validate target bytes")

    require("process_signal_exec_reset" in signals and "process_signal_exec_reset(dst);" in loader,
            "exec must reset caught signal handlers instead of carrying stale user pointers into the new image")
    require("process_signal_user_code_pointer_valid" in signals and "process_user_access_allowed_at(&current_proc, ptr, false, true)" in signals,
            "signal handlers/restorers must be validated against executable user VMAs, not just raw address bounds")
    require("process_signal_user_code_pointer_valid((uptr)frame.saved_regs.rip)" in signals,
            "sigreturn must validate restored RIP against executable user VMAs")
    require("rabbitbone_buf_out_t bo" in signals and "ksnprintf(out + len" not in signals,
            "process job diagnostics must append through the shared bounded formatter instead of pointer arithmetic past truncated output")
    require("regs->rsp < (u64)sizeof(frame) + 16u" in signals and "frame_addr > regs->rsp" in signals,
            "signal frame creation must check user stack underflow")
    require("pid == 0x80000000u" in signals and "(~pid) + 1u" in signals,
            "negative process-group signal decoding must avoid signed INT_MIN overflow")
    require("0xffffffff80000001ull" in sys_sig,
            "C signal syscall pid decoder must reject sign-extended INT_MIN")
    require("(!act_ptr && !old_ptr)" in sys_sig and "(!set_ptr && !old_ptr)" in sys_sig,
            "C signal syscall wrappers must reject no-op sigaction/sigprocmask calls like Rust pre-validation")
    require("pid != 0x8000_0000" in rust_val and "0x8000_0000u64" in rust_selftest and "0xffff_fffeu64" in rust_selftest,
            "Rust signal syscall validator must align with C for raw 32-bit negative pid values")
    require("pid >= 0xffff_ffff_8000_0001" in rust_val and "0xffff_ffff_8000_0000" in rust_selftest,
            "Rust signal syscall validator must reject sign-extended INT_MIN")


def check_handle_snapshot_stack_safety() -> None:
    lifecycle = text("kernel/sys/syscall/lifecycle.inc")
    files = text("kernel/sys/syscall/files_and_handles.inc")
    selftest = text("kernel/sys/syscall/selftest.inc")

    require("sys_handle_t *handles = (sys_handle_t *)dst" in lifecycle and
            "init_stdio_handles(handles);" in lifecycle and
            "sys_handle_t tmp[SYSCALL_MAX_HANDLES]" not in lifecycle,
            "initial user handle snapshots must be initialized in-place, not staged on the kernel stack")
    require("handle_table_can_replace_two_kinds" in files and
            "handle_table_remove_target_from_counts" in files and
            "handle_table_add_kind_to_counts" in files,
            "pipe snapshot collision checks must simulate fd replacement without a full stack copy")
    require("sys_handle_t probe[SYSCALL_MAX_HANDLES]" not in files and
            "memcpy(probe, writer_snapshot" not in files,
            "pipe snapshot creation must not allocate a full sys_handle table on the kernel stack")
    require("syscall_selftest_pipe_snapshot_collision" in selftest and
            "kmalloc(SYSCALL_USER_HANDLE_SNAPSHOT_BYTES)" in selftest and
            "kfree(pipe_snapshot);" in selftest,
            "syscall selftests must heap-allocate full handle snapshots instead of putting them on the kernel stack")

    offenders = []
    aggregate_snapshot_owners = {
        "kernel/proc/process/internal.h",
        "kernel/proc/process/ktest_context.inc",
    }
    for path in ROOT.joinpath("kernel").rglob("*"):
        if not path.is_file() or path.suffix not in {".c", ".h", ".inc"}:
            continue
        rel = path.relative_to(ROOT).as_posix()
        data = path.read_text(encoding="utf-8")
        if re.search(r"sys_handle_t\s+\w+\s*\[\s*SYSCALL_MAX_HANDLES\s*\]", data):
            offenders.append(f"{rel}:sys_handle_t[SYSCALL_MAX_HANDLES]")
        if rel not in aggregate_snapshot_owners and re.search(r"u8\s+\w+\s*\[\s*SYSCALL_USER_HANDLE_SNAPSHOT_BYTES\s*\]", data):
            offenders.append(f"{rel}:u8[SYSCALL_USER_HANDLE_SNAPSHOT_BYTES]")
    require(not offenders, "full syscall handle snapshots must not live on the kernel stack: " + ", ".join(offenders))


def check_debug_shell_stack_safety() -> None:
    shell = text("kernel/core/shell.c")
    require("#include <rabbitbone/arch/io.h>" in shell,
            "debug shell must include the cpu_sti/cpu_hlt declarations it uses when RABBITBONE_DEBUG_SHELL is enabled")
    require("debug_print_formatted" in shell and "kmalloc(out_size)" in shell and "kfree(out);" in shell,
            "debug shell diagnostic format buffers must use bounded heap allocations instead of large stack arrays")
    require(not re.search(r"char\s+out\s*\[\s*(1024|2048|4096)\s*\]", shell),
            "debug shell must not allocate large diagnostic output buffers on the kernel stack")



def check_firmware_probe_bounds() -> None:
    acpi = text("kernel/drivers/acpi.c")
    pci = text("kernel/drivers/pci.c")
    require("rabbitbone_align_up_u64_checked((u64)start, 16u" in acpi and "__builtin_add_overflow(p, (uptr)sizeof(acpi_rsdp_v1_t)" in acpi,
            "ACPI RSDP scanner must use checked alignment and checked range end arithmetic")
    require("acpi_span_has" in acpi and "while (acpi_span_has(p, end, sizeof(acpi_madt_entry_header_t)))" in acpi,
            "ACPI MADT parser must use bounded span checks instead of raw p+sizeof pointer arithmetic")
    scan_body = acpi[acpi.index("static const acpi_rsdp_v2_t *scan_rsdp_range"):acpi.index("static const acpi_rsdp_v2_t *find_rsdp")]
    require("RABBITBONE_ALIGN_UP(" not in scan_body and "p + sizeof(acpi_rsdp_v1_t)" not in scan_body,
            "ACPI RSDP scanner must not use wrapping ALIGN_UP or p+sizeof loop bounds")
    require("pci_cfg_valid_width" in pci and "offset + (u32)width > 256u" in pci and "((u32)offset & ((u32)width - 1u)) == 0" in pci,
            "PCI config accessors must validate access width, bounds, and alignment")
    require("pci_cfg_valid(dev, func, offset)" not in pci,
            "PCI config code must not use width-blind offset validation")

def check_printf_stack_safety() -> None:
    printf = text("kernel/core/printf.c")
    fmt = text("kernel/lib/format.c")
    kctl = text("kernel/sys/syscall/kctl_syscalls.inc")
    pci = text("kernel/drivers/pci.c")
    require("rabbitbone_vprintf_emit_fn emit" in printf and "console_emit" in printf and "kvformat(&out, fmt, ap)" in printf,
            "kprintf must format through a console sink instead of staging through a fixed stack buffer")
    require("char tmp[1024]" not in printf and "kvsnprintf(tmp" not in printf,
            "kprintf must not allocate a large fixed formatting buffer on the kernel stack")
    require("KPRINTF_CHUNK_SIZE" in printf and "out_flush(&out)" in printf and "console_write_n(s, n)" in printf,
            "kprintf must batch console output into chunks instead of emitting one console_write_n per character")
    require("if (out->emit) out->emit(&c, 1u" not in printf and "out_emit(&c, 1u" not in printf,
            "kprintf sink must not flush one byte per formatted character")
    require("char tmp[192]" not in fmt and "kvsnprintf(out->buf + out->used" in fmt,
            "rabbitbone_buf_appendf must append directly into the caller buffer instead of truncating through a tiny stack temp")
    require("char tmp[256]" not in kctl and "kmalloc(RABBITBONE_KCTL_OUT_MAX)" in kctl and "char tmp[RABBITBONE_KCTL_OUT_MAX]" not in kctl,
            "kctl must not allocate its 4 KiB output buffer on the syscall stack or truncate appendf through a tiny temp")
    require("kctl_len_add" in kctl and "kctl_stack_top_checked" in kctl and "__builtin_add_overflow((uptr)base" in kctl and "((uptr)base + size)" not in kctl,
            "kctl diagnostic length accounting and private stack-top calculation must be overflow-checked")
    require("rabbitbone_buf_out_t bo" in pci and "char tmp[192]" not in pci and "static void appendf" not in pci,
            "PCI status formatting must use the shared bounded formatter instead of private stack temporaries")


def check_panic_and_extent_hardening() -> None:
    panic = text("kernel/core/panic.c")
    console_h = text("kernel/include/rabbitbone/console.h")
    printf = text("kernel/core/printf.c")
    ext4_internal = text("kernel/fs/ext4/internal.h")
    ext4_write = text("kernel/fs/ext4/extents_array_write.inc")
    ext4_free = text("kernel/fs/ext4/extents_free_demote.inc")
    gdt = text("kernel/arch/x86_64/gdt.c")

    require("g_panic_msg[PANIC_MSG_BUF_SIZE]" in panic and "g_panic_serial_chunk[PANIC_SERIAL_CHUNK_SIZE]" in panic,
            "panic formatter must use static panic buffers instead of large stack buffers")
    for needle in ("char buf[512]", "char msg[512]"):
        require(needle not in panic, f"panic path still allocates large stack formatting buffer: {needle}")
    require("panic_try_claim" in panic and "panic_emit_claimed" in panic,
            "panic path must claim the panic guard before formatting into shared static buffers")
    require("kvprintf_emit_buffered" in console_h and "kvprintf_emit_buffered" in printf and "panic_serial_emit" in panic,
            "panic serial formatting must use the shared chunked formatter, not a fixed stack buffer")

    require("checked_add_u32" in ext4_internal and "extent_logical_end_checked" in ext4_internal and "extent_physical_end_checked" in ext4_internal,
            "ext4 extent code must have checked logical/physical range helpers")
    forbidden = ("first + len", "start + len", "phys + 1u", "logical + 1u", "prev_start + prev_len",
                 "prev_first + prev_len", "left_len + right_len", "a_len + b_len", "nb + 1u")
    for needle in forbidden:
        require(needle not in ext4_write and needle not in ext4_free,
                f"ext4 extent mutation/free path still has unchecked arithmetic: {needle}")
    require("extent_logical_end_checked" in ext4_write and "extent_physical_end_checked" in ext4_write and
            "checked_add_u64(phys, 1u" in ext4_free,
            "ext4 extent mutation/free paths must use checked arithmetic for logical and physical boundaries")
    require("gdt_stack_top_checked" in gdt and "__builtin_add_overflow((uptr)base" in gdt and
            "align_down16((uptr)ring0 + ring0_size)" not in gdt and "align_down16((uptr)dynamic_kernel_stack + dynamic_kernel_stack_size)" not in gdt,
            "GDT dynamic stack top calculation must be checked for address overflow")


def check_final_polish_hardening() -> None:
    log = text("kernel/core/log.c")
    log_h = text("kernel/include/rabbitbone/log.h")
    fmt = text("kernel/lib/format.c")
    kctl = text("kernel/sys/syscall/kctl_syscalls.inc")
    userlog = text("kernel/sys/syscall/terminal_log_dispatch.inc")
    devfs = text("kernel/vfs/devfs.c")
    ext4_internal = text("kernel/fs/ext4/internal.h")
    ext4_alloc = text("kernel/fs/ext4/allocation.inc")
    ext4_journal = text("kernel/fs/ext4/journal.inc")
    ext4_mount = text("kernel/fs/ext4/mount_sync.inc")
    ext4_meta = text("kernel/fs/ext4/directory_metadata_validation.inc")
    ext4_repair = text("kernel/fs/ext4/dirent_repair.inc")
    ext4_extent_meta = text("kernel/fs/ext4/extent_metadata_validation.inc")
    ext4_orphans = text("kernel/fs/ext4/orphan_cleanup.inc")
    ext4_dirent_mut = text("kernel/fs/ext4/directory_entry_mutation.inc")
    ext4_dir_index = text("kernel/fs/ext4/directory_index.inc")
    ext4_rename = text("kernel/fs/ext4/rename_ops.inc")
    ext4_link_unlink = text("kernel/fs/ext4/link_unlink.inc")
    ext4_node_create = text("kernel/fs/ext4/node_create.inc")
    ktest_internal = text("kernel/core/ktest/internal.h")
    ktest_tarfs = text("kernel/core/ktest/tarfs_tests.inc")
    rbsh_kctl = text("user/bin/rbsh/builtins_kctl.inc")

    require("rabbitbone_buf_vappendf" in text("kernel/include/rabbitbone/format.h") and "rabbitbone_buf_vappendf" in fmt,
            "bounded formatter must expose a vappend helper so callers stop staging varargs through stack temporaries")
    require("if (out->used >= out->cap) out->used = out->cap - 1u" in fmt and "out->used < out->cap - 1u" in fmt,
            "bounded raw append must clamp corrupt used values before writing")
    require("char msg[320]" not in log and "char line[LOG_HEAP_LINE_LEN]" not in log and "rabbitbone_buf_vappendf(&line" in log,
            "kernel log path must format directly into the ring slot instead of staging large stack buffers")
    require("log_vwrite" in log_h and "void log_vwrite" in log and "log_vwrite(level, \"ktest\", fmt, ap)" in ktest_internal and "char msg[256]" not in ktest_internal,
            "ktest logging must forward varargs directly into the kernel log ring instead of staging through a stack buffer")
    require("log_dump_ring_ctx" in log_h and "log_dump_ring_ctx(log_line_writer_ctx_fn writer, void *ctx)" in log,
            "log dumping must carry caller context instead of relying on shared globals")
    require("log_dump_ring_tail_ctx" in log_h and "log_dump_ring_tail_ctx(log_line_writer_ctx_fn writer, void *ctx, usize max_bytes)" in log,
            "log dumping must support bounded recent-tail extraction for fixed-size KCTL buffers")
    require("kctl_log_target" not in kctl and "log_dump_ring_tail_ctx(kctl_log_line_writer, &out, budget)" in kctl,
            "kctl log dump must not race through a global output pointer and must tail recent entries")
    require("if (!isolated) { result = err(VFS_ERR_INVAL); goto out; }" in kctl,
            "kctl ktest isolation failure must unwind through the shared cleanup path")
    require("copy_string_arg_heap" in text("kernel/sys/syscall/usercopy_paths.inc") and "sanitize_user_log_message" in userlog and "kfree(msg);" in userlog,
            "userspace log strings must be heap-backed, sanitized, and freed on all non-panic paths")
    require("DEV_KMSG_MAX" in devfs and "msg[i] =" in devfs and "kfree(msg);" in devfs and "char msg[161]" not in devfs,
            "/dev/kmsg writes must sanitize control bytes without a stack log buffer")
    require("checked_add_u64(gdt_first, gdt_blocks" in ext4_alloc and "checked_add_u64(inode_table, table_blocks" in ext4_alloc,
            "ext4 metadata range checks must not use unchecked start+length arithmetic")
    require("checked_add_u64(mnt->journal_first_block, mnt->journal_blocks" in ext4_journal and "journal_end > mnt->blocks_count" in ext4_journal,
            "ext4 journal range checks must reject wrapped or out-of-filesystem journal metadata")
    require("checked_add_u64(first, i, &block)" in ext4_mount and "checked_add_u64(first, 1u, &payload_block)" in ext4_mount and
            "checked_add_u64(out->journal_first_block, i, &reserve_block)" in ext4_mount and
            "checked_add_u64(mnt->journal_first_block, 1u, &payload_block)" in ext4_journal and
            "checked_add_u64(mnt->journal_first_block, 1u, &payload_block)" in ext4_alloc,
            "ext4 journal reservation/marker/commit/replay code must not use unchecked journal_first+offset arithmetic")
    require("old_gfree >= valid" in ext4_alloc and "old_sfree >= mnt->blocks_count" in ext4_alloc and
            "old_gfree >= mnt->inodes_per_group" in ext4_alloc and "old_sfree >= mnt->inodes_count" in ext4_alloc,
            "ext4 free counters must be range-checked before incrementing metadata counters")
    require("gfree == 0 || sfree == 0" in ext4_alloc and "old_gfree == 0 || old_sfree == 0" in ext4_alloc and
            "old_used_dirs == 0xffffffffu" in ext4_alloc and "dir && old_used_dirs == 0" in ext4_alloc,
            "ext4 allocation counters must reject corrupt zero/overflow state before decrementing or incrementing")
    require("bitmap_set8(bm, bit);" in ext4_alloc and "gd_set_free_inodes(&gd, old_gfree);" in ext4_alloc and
            "if (dir) gd_set_used_dirs(&gd, old_used_dirs);" in ext4_alloc,
            "ext4 inode allocation/free failure paths must roll back bitmap and group counters")
    require("u64 ino64 = (u64)group * (u64)mnt->inodes_per_group" in ext4_alloc and
            "u64 ino64 = (u64)group * (u64)mnt->inodes_per_group" in ext4_meta and
            "u64 ino64 = (u64)group * (u64)mnt->inodes_per_group" in ext4_repair,
            "ext4 inode allocation/metadata scan/repair code must avoid wrapped u32 inode number arithmetic")
    require("checked_add_u64(inode_table, inode_table_blocks, &inode_table_end)" in ext4_meta,
            "ext4 metadata validator must check inode-table span arithmetic")
    require("checked_add_u64(start, j, &data_block)" in ext4_extent_meta and "checked_add_u64(start, j, &data_block)" in ext4_orphans,
            "ext4 extent allocation validation/free paths must check per-block start+index arithmetic")
    require("ext4_links_inc_checked" in ext4_internal and "ext4_links_dec_checked" in ext4_internal and
            "ext4_links_inc_checked(&inode)" in ext4_link_unlink and "ext4_links_dec_checked(&parent)" in ext4_link_unlink and
            "ext4_links_inc_checked(&parent)" in ext4_node_create and "ext4_links_inc_checked(&new_parent)" in ext4_rename,
            "ext4 link-count mutations must use checked helpers and reject wrap/underflow")
    require("ext4_block_count_u32_checked" in ext4_internal and "ext4_logical_offset_checked" in ext4_internal and
            "ext4_block_count_u32_checked(mnt, size" in ext4_dirent_mut and
            "checked_add_u64(size, mnt->block_size" in ext4_dirent_mut and
            "ext4_block_count_u32_checked(mnt, size" in ext4_dir_index and
            "ext4_logical_offset_checked(mnt, logical" in ext4_rename,
            "ext4 directory scans and growth must bound inode size/block-count arithmetic")
    for name, body in (("dirent mutation", ext4_dirent_mut), ("dir index", ext4_dir_index), ("rename/list", ext4_rename)):
        require("p + rec_len" not in body and "p += rec_len" not in body,
                f"ext4 {name} still advances dirent offsets with unchecked arithmetic")
    require("malloc(RABBITBONE_KCTL_OUT_MAX)" in rbsh_kctl and "free(out);" in rbsh_kctl and "char out[RABBITBONE_KCTL_OUT_MAX]" not in rbsh_kctl,
            "rbsh kctl must not put the 4 KiB kctl output buffer on the user stack")
    for needle in ("tarfs rejects root-only ./ entry", "tarfs rejects canonical duplicate names", "tarfs rejects unsafe archive name bytes"):
        require(needle in ktest_tarfs, f"ktest tarfs suite missing final archive-name policy check: {needle}")

    ktest_hardening = text("kernel/core/ktest/hardening_regression_tests.inc")
    for needle in (
        "path normalizer keeps valid paths with more than 32 components",
        "path normalizer rejects root escape and clears stale output",
        "path_next_component rejects unsafe later component and clears output",
        "bounded formatter clamps corrupted used before raw append",
        "log ring dump ctx exposes sanitized /dev/kmsg line without unsafe control bytes",
        "kctl logs returns caller-local sanitized log output",
        "syscall kill rejects raw 32-bit INT_MIN pid",
        "syscall kill accepts raw 32-bit negative process-group pid shape before backend resolution",
        "syscall mprotect rejects W+X before VM layer",
        "syscall chmod rejects setgid bit in Rust/C boundary policy",
    ):
        require(needle in ktest_hardening, f"ktest hardening regression suite missing coverage: {needle}")
    require('test_hardening_regressions();' in text("kernel/core/ktest/coverage_runner.inc"),
            "hardening regression suite must run as part of full ktest")

    procctl = text("user/bin/procctl.c")
    for needle in ("data_handler", "au_sigaction(RABBITBONE_SIGUSR2, &bad_act, 0) >= 0",
                   "au_syscall3(AU_SYS_KILL, 0x80000000ull", "au_syscall3(AU_SYS_KILL, 0xfffffffeull"):
        require(needle in procctl, f"ring3 procctl missing signal/pid boundary regression: {needle}")

def main() -> int:
    check_paths()
    check_auth()
    check_string_api()
    check_elf_and_pipes()
    check_open_files()
    check_tarfs_headers()
    check_shell_redirections()
    check_syscall_boundary_validation()
    check_process_lifetime_and_usercopy()
    check_checked_alignment_edges()
    check_ext4_metadata_and_signals()
    check_handle_snapshot_stack_safety()
    check_debug_shell_stack_safety()
    check_firmware_probe_bounds()
    check_printf_stack_safety()
    check_panic_and_extent_hardening()
    check_final_polish_hardening()
    print("source-hardening: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
