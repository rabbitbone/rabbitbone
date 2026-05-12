#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SPLIT_UNITS: dict[str, list[str]] = {
    "kernel/core/ktest.c": ["kernel/core/ktest"],
    "kernel/drivers/ata_pio.c": ["kernel/drivers/ata_pio"],
    "kernel/fs/ext4.c": ["kernel/fs/ext4"],
    "kernel/mm/kmem.c": ["kernel/mm/kmem"],
    "kernel/mm/vmm.c": ["kernel/mm/vmm"],
    "kernel/proc/process.c": ["kernel/proc/process"],
    "kernel/sched/scheduler.c": ["kernel/sched/scheduler"],
    "kernel/sys/syscall.c": ["kernel/sys/syscall"],
    "kernel/vfs/ext4_vfs.c": ["kernel/vfs/ext4_vfs"],
    "kernel/vfs/ramfs.c": ["kernel/vfs/ramfs"],
    "kernel/vfs/tarfs.c": ["kernel/vfs/tarfs"],
    "kernel/vfs/vfs.c": ["kernel/vfs/vfs"],
    "tests/test_main.cpp": ["tests/main"],
    "tools/installer/main.cpp": ["tools/installer/installer"],
    "user/lib/aurora.c": ["user/lib/aurora"],
}

RUST_SPLIT_UNITS: dict[str, list[str]] = {
    "kernel/rust/syscall_dispatch.rs": ["kernel/rust/syscall_dispatch"],
}

C_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
RUST_INCLUDE_RE = re.compile(r'^\s*include!\("([^"]+)"\);')
RUST_ATTR_RE = re.compile(r'^\s*#\s*\[')


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace('\\', '/')


def source_hashes() -> dict[str, str]:
    suffixes = {'.c', '.h', '.hpp', '.inc', '.rs', '.cpp', '.S', '.py', '.sh', '.yml', '.toml', '.md'}
    result: dict[str, str] = {}
    for path in ROOT.rglob('*'):
        if not path.is_file() or path.suffix not in suffixes:
            continue
        if any(part in {'build', '.git'} for part in path.parts):
            continue
        result[rel(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def resolve_local_include(source: Path, target: str) -> Path | None:
    candidate = (source.parent / target).resolve()
    try:
        candidate.relative_to(ROOT)
    except ValueError:
        return None
    return candidate if candidate.exists() else None


def collect_c_includes(path: Path, seen: set[Path] | None = None) -> set[Path]:
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen:
        return seen
    seen.add(path)
    for line in path.read_text(encoding='utf-8').splitlines():
        match = C_INCLUDE_RE.match(line)
        if not match:
            continue
        included = resolve_local_include(path, match.group(1))
        if included is not None:
            collect_c_includes(included, seen)
    return seen


def collect_rust_includes(path: Path, seen: set[Path] | None = None) -> set[Path]:
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen:
        return seen
    seen.add(path)
    for line in path.read_text(encoding='utf-8').splitlines():
        match = RUST_INCLUDE_RE.match(line)
        if not match:
            continue
        included = resolve_local_include(path, match.group(1))
        if included is None:
            raise RuntimeError(f"missing Rust include from {rel(path)}: {match.group(1)}")
        collect_rust_includes(included, seen)
    return seen


def expected_fragments(fragment_dirs: list[str], suffixes: tuple[str, ...]) -> set[Path]:
    found: set[Path] = set()
    for directory in fragment_dirs:
        base = ROOT / directory
        if not base.exists():
            raise RuntimeError(f"missing split directory: {directory}")
        for suffix in suffixes:
            found.update(p.resolve() for p in base.rglob(f"*{suffix}"))
    return found


def check_c_units(errors: list[str]) -> None:
    for wrapper, dirs in SPLIT_UNITS.items():
        wrapper_path = ROOT / wrapper
        if not wrapper_path.exists():
            errors.append(f"missing wrapper: {wrapper}")
            continue
        included = collect_c_includes(wrapper_path)
        expected = expected_fragments(dirs, ('.inc', '.h', '.hpp'))
        missing = sorted(expected - included)
        if missing:
            errors.append(f"{wrapper}: unreferenced split fragments: {', '.join(rel(p) for p in missing)}")


def check_rust_units(errors: list[str]) -> None:
    for wrapper, dirs in RUST_SPLIT_UNITS.items():
        wrapper_path = ROOT / wrapper
        if not wrapper_path.exists():
            errors.append(f"missing Rust wrapper: {wrapper}")
            continue
        try:
            included = collect_rust_includes(wrapper_path)
        except RuntimeError as exc:
            errors.append(str(exc))
            continue
        expected = expected_fragments(dirs, ('.rs',))
        missing = sorted(expected - included)
        if missing:
            errors.append(f"{wrapper}: unreferenced Rust fragments: {', '.join(rel(p) for p in missing)}")


def check_rust_dangling_attrs(errors: list[str]) -> None:
    for path in ROOT.glob('kernel/rust/**/*.rs'):
        lines = path.read_text(encoding='utf-8').splitlines()
        attr_start = None
        for index, line in enumerate(lines):
            if RUST_ATTR_RE.match(line):
                if attr_start is None:
                    attr_start = index + 1
                continue
            if not line.strip() or line.lstrip().startswith('//'):
                continue
            attr_start = None
        if attr_start is not None:
            errors.append(f"{rel(path)}:{attr_start}: dangling Rust attribute at end of file")


def check_empty_fragments(errors: list[str]) -> None:
    for path in ROOT.glob('**/*'):
        if path.suffix not in {'.inc', '.h', '.hpp', '.rs'}:
            continue
        if any(part == 'build' for part in path.parts):
            continue
        text = path.read_text(encoding='utf-8').strip()
        if not text:
            errors.append(f"empty split/source fragment: {rel(path)}")


def main() -> int:
    before = source_hashes()
    errors: list[str] = []
    check_c_units(errors)
    check_rust_units(errors)
    check_rust_dangling_attrs(errors)
    check_empty_fragments(errors)
    after = source_hashes()
    if before != after:
        errors.append('source tree changed while split-integrity check was running')
    if errors:
        for error in errors:
            print(f"split-integrity: {error}", file=sys.stderr)
        return 1
    print("split-integrity: ok")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
