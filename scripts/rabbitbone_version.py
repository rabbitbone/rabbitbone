#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION_H = ROOT / "include" / "rabbitbone" / "version.h"


@dataclass(frozen=True)
class RabbitboneVersion:
    major: int
    minor: int
    patch: int
    fix: int
    text: str
    syscall_abi: int

    @property
    def expected_text(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}.{self.fix}"

    @property
    def iso_volume_id(self) -> str:
        return f"RABBITBONE_{self.syscall_abi & 0xFFFFF:05X}"

    @property
    def fat_serial(self) -> int:
        return 0xA0000000 | ((self.syscall_abi & 0xFFFFF) << 8) | 0xEF

    @property
    def tag(self) -> str:
        return f"v{self.text}"


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise SystemExit(f"missing required file: {path.relative_to(ROOT)}")


def define_u(text: str, name: str) -> int:
    match = re.search(rf"^#define[ \t]+{re.escape(name)}[ \t]+([0-9]+)u[ \t]*$", text, re.MULTILINE)
    if not match:
        raise SystemExit(f"missing numeric define: {name}")
    return int(match.group(1))


def define_str(text: str, name: str) -> str:
    match = re.search(rf'^#define[ \t]+{re.escape(name)}[ \t]+"([^"]+)"[ \t]*$', text, re.MULTILINE)
    if not match:
        raise SystemExit(f"missing string define: {name}")
    return match.group(1)


def define_hex(text: str, name: str) -> int:
    match = re.search(rf"^#define[ \t]+{re.escape(name)}[ \t]+(0x[0-9a-fA-F]+)ull[ \t]*$", text, re.MULTILINE)
    if not match:
        raise SystemExit(f"missing hex define: {name}")
    return int(match.group(1), 16)


def load_version(path: Path = VERSION_H) -> RabbitboneVersion:
    text = read_text(path)
    version = RabbitboneVersion(
        major=define_u(text, "RABBITBONE_VERSION_MAJOR"),
        minor=define_u(text, "RABBITBONE_VERSION_MINOR"),
        patch=define_u(text, "RABBITBONE_VERSION_PATCH"),
        fix=define_u(text, "RABBITBONE_VERSION_FIX"),
        text=define_str(text, "RABBITBONE_VERSION_TEXT"),
        syscall_abi=define_hex(text, "RABBITBONE_SYSCALL_ABI_VERSION"),
    )
    if version.text != version.expected_text:
        raise SystemExit(f"RABBITBONE_VERSION_TEXT is {version.text}, expected {version.expected_text}")
    expected_abi = (version.patch << 8) | version.fix
    if version.syscall_abi != expected_abi:
        raise SystemExit(
            f"RABBITBONE_SYSCALL_ABI_VERSION is 0x{version.syscall_abi:08x}, expected 0x{expected_abi:08x}"
        )
    return version


def main() -> int:
    parser = argparse.ArgumentParser(description="Print Rabbitbone release metadata derived from version.h")
    parser.add_argument("--print-version", action="store_true")
    parser.add_argument("--print-tag", action="store_true")
    parser.add_argument("--print-iso-volume-id", action="store_true")
    parser.add_argument("--print-fat-serial", action="store_true")
    args = parser.parse_args()
    version = load_version()
    if args.print_version:
        print(version.text)
    elif args.print_tag:
        print(version.tag)
    elif args.print_iso_volume_id:
        print(version.iso_volume_id)
    elif args.print_fat_serial:
        print(f"0x{version.fat_serial:08X}")
    else:
        print(f"{version.text} {version.tag} {version.iso_volume_id} 0x{version.fat_serial:08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
