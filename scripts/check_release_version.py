#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True

from rabbitbone_version import ROOT, VERSION_H, RabbitboneVersion, load_version, read_text

README = ROOT / "README.md"
STATUS = ROOT / "docs" / "STATUS.md"
RELEASES = ROOT / "docs" / "RELEASES.md"
MAKEFILE = ROOT / "Makefile"
LIVE_ISO = ROOT / "scripts" / "make_live_iso.py"
KERNEL_ABI_FORWARD = ROOT / "kernel" / "include" / "rabbitbone" / "abi.h"
PUBLIC_ABI = ROOT / "include" / "rabbitbone" / "abi.h"
VFS_H = ROOT / "kernel" / "include" / "rabbitbone" / "vfs.h"

TEXT_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hpp", ".inc", ".rs", ".S", ".py", ".sh", ".yml", ".yaml", ".toml", ".md", ".txt", ".ld", ".example", ".vmx",
}
SKIPPED_DIRS = {".git", "build", "dist", "target", "__pycache__"}
SKIPPED_SUFFIXES = {".zip", ".png", ".iso", ".img", ".bin", ".elf", ".o", ".obj", ".pyc", ".pyo"}


def require_contains(path: Path, needle: str) -> None:
    text = read_text(path)
    if needle not in text:
        rel = path.relative_to(ROOT)
        raise SystemExit(f"{rel} does not contain expected version marker: {needle}")


def require_not_contains(path: Path, needle: str) -> None:
    text = read_text(path)
    if needle in text:
        rel = path.relative_to(ROOT)
        raise SystemExit(f"{rel} contains obsolete marker: {needle}")


def iter_text_files() -> list[Path]:
    paths: list[Path] = []
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        rel_parts = path.relative_to(ROOT).parts
        if any(part in SKIPPED_DIRS for part in rel_parts):
            continue
        if path.suffix.lower() in SKIPPED_SUFFIXES:
            continue
        if path.suffix and path.suffix not in TEXT_SUFFIXES:
            continue
        paths.append(path)
    return paths


def require_no_obsolete_release_stage() -> None:
    obsolete_compact = "stage" + "20.15"
    obsolete_spaced = "stage " + "20.15"
    for path in iter_text_files():
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if obsolete_compact in text or obsolete_spaced in text:
            rel = path.relative_to(ROOT)
            raise SystemExit(f"{rel} contains obsolete release-stage marker")


def load_make_iso_module():
    spec = importlib.util.spec_from_file_location("rabbitbone_make_live_iso_check", LIVE_ISO)
    if spec is None or spec.loader is None:
        raise SystemExit("could not import scripts/make_live_iso.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require_generated_release_metadata(version: RabbitboneVersion) -> None:
    makefile = read_text(MAKEFILE)
    if "ISO_VOLUME_ID ?= $(shell python3 scripts/rabbitbone_version.py --print-iso-volume-id)" not in makefile:
        raise SystemExit("Makefile must derive ISO_VOLUME_ID from scripts/rabbitbone_version.py")
    if re.search(r"RABBITBONE_[0-9A-Fa-f]{5}\b", makefile):
        raise SystemExit("Makefile contains a hard-coded Rabbitbone ISO volume id")

    module = load_make_iso_module()
    try:
        iso_volume_id = module.default_iso_volume_id()
        fat_serial = module.default_fat_serial()
    except AttributeError as exc:
        raise SystemExit(f"scripts/make_live_iso.py is missing generated metadata helper: {exc}") from exc
    if iso_volume_id != version.iso_volume_id:
        raise SystemExit(f"make_live_iso default volume id is {iso_volume_id}, expected {version.iso_volume_id}")
    if fat_serial != version.fat_serial:
        raise SystemExit(f"make_live_iso FAT serial is 0x{fat_serial:08x}, expected 0x{version.fat_serial:08x}")

    live_iso_text = read_text(LIVE_ISO)
    stale_patterns = [
        r"default\s*=\s*['\"]RABBITBONE_[0-9A-Fa-f]{5}['\"]",
        r"le32\(0xA[0-9A-Fa-f]{7}\)",
    ]
    for pattern in stale_patterns:
        if re.search(pattern, live_iso_text):
            raise SystemExit(f"scripts/make_live_iso.py contains stale hard-coded release metadata matching {pattern}")


def require_single_abi_source() -> None:
    text = read_text(KERNEL_ABI_FORWARD)
    expected_include = '#include "../../../include/rabbitbone/abi.h"'
    if expected_include not in text:
        raise SystemExit("kernel/include/rabbitbone/abi.h must be a forwarding header to include/rabbitbone/abi.h")
    public_text = read_text(PUBLIC_ABI)
    if public_text == text:
        raise SystemExit("kernel/include/rabbitbone/abi.h duplicated the public ABI header instead of forwarding it")


def require_vfs_uses_public_abi() -> None:
    text = read_text(VFS_H)
    required_markers = [
        "#include <rabbitbone/abi.h>",
        "#define VFS_NAME_MAX RABBITBONE_NAME_MAX",
        "#define VFS_PATH_MAX RABBITBONE_PATH_MAX",
        "VFS_ERR_NOENT = RABBITBONE_ERR_NOENT",
        "VFS_ERR_BUSY = RABBITBONE_ERR_BUSY",
    ]
    for marker in required_markers:
        if marker not in text:
            raise SystemExit(f"kernel/include/rabbitbone/vfs.h must derive VFS ABI aliases from public ABI: missing {marker}")
    stale_patterns = [
        r"#define[ \t]+VFS_NAME_MAX[ \t]+64u",
        r"#define[ \t]+VFS_PATH_MAX[ \t]+256u",
        r"VFS_ERR_NOENT[ \t]*=[ \t]*-1",
        r"VFS_ERR_BUSY[ \t]*=[ \t]*-12",
    ]
    for pattern in stale_patterns:
        if re.search(pattern, text):
            raise SystemExit(f"kernel/include/rabbitbone/vfs.h contains stale duplicated ABI value matching {pattern}")


def require_rust_abi_env_include() -> None:
    lib_rs = read_text(ROOT / "kernel" / "rust" / "lib.rs")
    if 'include!(env!("RABBITBONE_RUST_ABI_RS"));' not in lib_rs:
        raise SystemExit("kernel/rust/lib.rs must include generated ABI through RABBITBONE_RUST_ABI_RS")
    makefile = read_text(MAKEFILE)
    if 'RABBITBONE_RUST_ABI_RS="$(K_RUST_ABI)"' not in makefile:
        raise SystemExit("Makefile must pass RABBITBONE_RUST_ABI_RS to rustc")
    dead_target = ROOT / "kernel" / "rust" / "x86_64-rabbitbone-kernel.json"
    if dead_target.exists():
        raise SystemExit("remove unused kernel/rust/x86_64-rabbitbone-kernel.json or wire it into RUST_TARGET")


def require_python_cache_ignored() -> None:
    gitignore = read_text(ROOT / ".gitignore")
    for marker in ("__pycache__/", "*.pyc", "*.pyo"):
        if marker not in gitignore:
            raise SystemExit(f".gitignore missing Python cache ignore: {marker}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--print-version", action="store_true")
    parser.add_argument("--print-tag", action="store_true")
    parser.add_argument("--print-iso-volume-id", action="store_true")
    parser.add_argument("--print-fat-serial", action="store_true")
    parser.add_argument("--github-output", action="store_true")
    args = parser.parse_args()

    version = load_version()

    if args.print_version:
        print(version.text)
        return 0
    if args.print_tag:
        print(version.tag)
        return 0
    if args.print_iso_volume_id:
        print(version.iso_volume_id)
        return 0
    if args.print_fat_serial:
        print(f"0x{version.fat_serial:08X}")
        return 0

    require_contains(README, f"badge/version-{version.text}-")
    require_contains(README, f"The current release line is `{version.text}`.")
    require_contains(STATUS, f"Rabbitbone is currently at `{version.text}`.")
    require_contains(RELEASES, f"## {version.text}")
    require_contains(VERSION_H, '#define RABBITBONE_KTEST_TITLE RABBITBONE_VERSION_FULL " self-test"')
    require_no_obsolete_release_stage()
    require_generated_release_metadata(version)
    require_single_abi_source()
    require_vfs_uses_public_abi()
    require_rust_abi_env_include()
    require_python_cache_ignored()

    if args.github_output:
        output_name = os.environ.get("GITHUB_OUTPUT")
        if output_name:
            output = Path(output_name)
            with output.open("a", encoding="utf-8") as f:
                f.write(f"version={version.text}\n")
                f.write(f"tag={version.tag}\n")
                f.write(f"iso_volume_id={version.iso_volume_id}\n")
                f.write(f"fat_serial=0x{version.fat_serial:08X}\n")

    print(f"release version OK: {version.text} / ABI 0x{version.syscall_abi:08x} / ISO {version.iso_volume_id}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
