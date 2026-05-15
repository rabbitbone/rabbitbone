#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SOURCES = [
    *sorted((ROOT / "kernel" / "core" / "ktest").glob("*.inc")),
    *sorted((ROOT / "kernel" / "core" / "ktest" / "ext4_disk").glob("*.inc")),
    ROOT / "user" / "bin" / "procctl.c",
    ROOT / "user" / "bin" / "mmapcheck.c",
    ROOT / "user" / "bin" / "mmapfilecheck.c",
    ROOT / "user" / "bin" / "mmapsharedcheck.c",
    ROOT / "user" / "bin" / "pipecheck.c",
    ROOT / "user" / "bin" / "fdcheck.c",
    ROOT / "user" / "bin" / "fdleak.c",
    ROOT / "user" / "bin" / "execfdcheck.c",
    ROOT / "kernel" / "sys" / "syscall" / "selftest.inc",
    ROOT / "kernel" / "rust" / "syscall_dispatch" / "selftest.rs",
    ROOT / "kernel" / "rust" / "vfs_route.rs",
    *sorted((ROOT / "tests" / "main").glob("*.inc")),
]

REQUIRED = {
    "path >32 component normalization": "path normalizer keeps valid paths with more than 32 components",
    "path root escape rejection": "path normalizer rejects root escape and clears stale output",
    "path unsafe byte rejection": "path normalizer rejects backslash component bytes",
    "path_next_component output clearing": "path_next_component rejects unsafe later component and clears output",
    "tarfs ./ canonicalization": "tarfs canonicalizes ./ components and reads through VFS",
    "tarfs root-only ./ rejection": "tarfs rejects root-only ./ entry",
    "tarfs canonical duplicate rejection": "tarfs rejects canonical duplicate names",
    "tarfs unsafe name rejection": "tarfs rejects unsafe archive name bytes",
    "format tiny-temp regression": "bounded formatter fills caller buffer without tiny temp truncation",
    "format corrupted used clamp": "bounded formatter clamps corrupted used before raw append",
    "format vappend path": "bounded formatter vappend path matches appendf semantics",
    "devfs kmsg sanitize": "log ring dump ctx exposes sanitized /dev/kmsg line without unsafe control bytes",
    "log dump context": "log_dump_ring_ctx carries caller-local context through dump",
    "kctl logs context": "kctl logs returns caller-local sanitized log output",
    "kctl logs tail header": "kctl logs keeps stable header while tailing recent ring entries",
    "kctl mem diagnostics": "syscall kctl mem writes bounded heap-backed diagnostics",
    "raw pid INT_MIN reject": "syscall kill rejects raw 32-bit INT_MIN pid",
    "sign-extended pid INT_MIN reject": "syscall kill rejects sign-extended INT_MIN pid",
    "raw negative pid accepted shape": "syscall kill accepts raw 32-bit negative process-group pid shape before backend resolution",
    "raw negative pid backend mapping": "syscall kill maps syntactically valid raw 32-bit process group to backend result, not validation failure",
    "sigaction no-op reject": "syscall sigaction rejects no-op null act and oldact",
    "sigprocmask no-op reject": "syscall sigprocmask rejects no-op null set and oldset",
    "mprotect W+X reject": "syscall mprotect rejects W+X before VM layer",
    "munmap unaligned reject": "syscall munmap rejects unaligned address before VM layer",
    "mmap missing sharing reject": "syscall mmap rejects missing private/shared mode before VM layer",
    "mmap both sharing reject": "syscall mmap rejects both private and shared flags",
    "mmap fixed unaligned reject": "syscall mmap rejects unaligned MAP_FIXED address",
    "chmod setgid reject": "syscall chmod rejects setgid bit in Rust/C boundary policy",
    "cred get pointer validation": "syscall cred get requires output pointer",
    "cred login shape validation": "syscall cred login requires password pointer",
    "cred euid width validation": "syscall cred set_euid rejects values wider than u32",
    "sudo flags validation": "syscall sudo validate rejects unknown flags",
    "sudo ttl validation": "syscall sudo set_timeout rejects TTL above ABI cap",
    "kctl op validation": "syscall kctl rejects op at ABI max",
    "kctl output validation": "syscall kctl rejects missing output buffer for non-terminal op",
    "ring3 non-exec signal handler rejection": "au_sigaction(RABBITBONE_SIGUSR2, &bad_act, 0) >= 0",
    "ring3 raw pid INT_MIN regression": "au_syscall3(AU_SYS_KILL, 0x80000000ull",
    "ring3 raw negative pid regression": "au_syscall3(AU_SYS_KILL, 0xfffffffeull",
    "ring3 mmap W+X reject": "RABBITBONE_PROT_WRITE | RABBITBONE_PROT_EXEC",
    "ring3 file mmap bad offset reject": "mmap(0, PAGE, RABBITBONE_PROT_READ, MAP_FILE_FLAGS, fd, 1)",
    "ring3 shared mmap flag reject": "RABBITBONE_MAP_ANON | RABBITBONE_MAP_PRIVATE | RABBITBONE_MAP_SHARED",
    "pipe/fd runtime coverage": "ring3 /bin/pipecheck exercises pipes",
    "fd exhaustion/leak runtime coverage": "second /bin/fdleak proves fd table reset on exit",
    "exec fd cloexec runtime coverage": "FD_CLOEXEC handles across execv",
    "ext4 repair coverage": "EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata",
    "ext4 htree repair coverage": "EXT4 repair-lite rebuilds corrupted htree metadata",
    "ext4 depth2 extent coverage": "EXT4 supports depth > 1 indexed extent tree",
    "ext4 link-count overflow host coverage": "ext4 mkdir rejects parent link-count overflow",
}


def main() -> int:
    corpus = "\n".join(p.read_text(encoding="utf-8") for p in SOURCES if p.exists())
    missing = [f"{name}: {needle}" for name, needle in REQUIRED.items() if needle not in corpus]
    if missing:
        raise SystemExit("ktest-coverage missing required regression coverage:\n" + "\n".join(missing))
    print(f"ktest-coverage: ok ({len(REQUIRED)} required regressions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
