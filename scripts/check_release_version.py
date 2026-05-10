#!/usr/bin/env python3
import argparse
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_H = ROOT / "include" / "aurora" / "version.h"
README = ROOT / "README.md"
STATUS = ROOT / "docs" / "STATUS.md"
RELEASES = ROOT / "docs" / "RELEASES.md"


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        raise SystemExit(f"missing required file: {path.relative_to(ROOT)}")


def define_u(text: str, name: str) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+([0-9]+)u$", text, re.MULTILINE)
    if not m:
        raise SystemExit(f"missing numeric define: {name}")
    return int(m.group(1))


def define_str(text: str, name: str) -> str:
    m = re.search(rf'^#define\s+{re.escape(name)}\s+"([^"]+)"$', text, re.MULTILINE)
    if not m:
        raise SystemExit(f"missing string define: {name}")
    return m.group(1)


def define_hex(text: str, name: str) -> int:
    m = re.search(rf"^#define\s+{re.escape(name)}\s+(0x[0-9a-fA-F]+)ull$", text, re.MULTILINE)
    if not m:
        raise SystemExit(f"missing hex define: {name}")
    return int(m.group(1), 16)


def require_contains(path: Path, needle: str) -> None:
    text = read(path)
    if needle not in text:
        rel = path.relative_to(ROOT)
        raise SystemExit(f"{rel} does not contain expected version marker: {needle}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--print-version", action="store_true")
    parser.add_argument("--github-output", action="store_true")
    args = parser.parse_args()

    version_h = read(VERSION_H)
    major = define_u(version_h, "AURORA_VERSION_MAJOR")
    minor = define_u(version_h, "AURORA_VERSION_MINOR")
    patch = define_u(version_h, "AURORA_VERSION_PATCH")
    fix = define_u(version_h, "AURORA_VERSION_FIX")
    text = define_str(version_h, "AURORA_VERSION_TEXT")
    abi = define_hex(version_h, "AURORA_SYSCALL_ABI_VERSION")

    expected = f"{major}.{minor}.{patch}.{fix}"
    if text != expected:
        raise SystemExit(f"AURORA_VERSION_TEXT is {text}, expected {expected}")

    expected_abi = 0x100 + fix
    if abi != expected_abi:
        raise SystemExit(f"AURORA_SYSCALL_ABI_VERSION is 0x{abi:08x}, expected 0x{expected_abi:08x}")

    require_contains(README, f"badge/version-{text}-")
    require_contains(README, f"The current release line is `{text}`.")
    require_contains(STATUS, f"AuroraOS is currently at `{text}`.")
    require_contains(RELEASES, f"## {text}")

    if args.github_output:
        output_name = os.environ.get("GITHUB_OUTPUT")
        if output_name:
            output = Path(output_name)
            with output.open("a", encoding="utf-8") as f:
                f.write(f"version={text}\n")
                f.write(f"tag=v{text}\n")

    if args.print_version:
        print(text)
    else:
        print(f"release version OK: {text} / ABI 0x{abi:08x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
