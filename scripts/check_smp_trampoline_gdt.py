#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "kernel/arch/x86_64/smp_trampoline.S"


def fail(msg: str) -> None:
    print(f"smp-trampoline-gdt: {msg}", file=sys.stderr)
    raise SystemExit(1)


def access_byte(desc: int) -> int:
    return (desc >> 40) & 0xff


def bit(desc: int, n: int) -> int:
    return (desc >> n) & 1


def main() -> None:
    text = SRC.read_text(encoding="utf-8")
    match = re.search(r"smp_trampoline_gdt:\s*(.*?)smp_trampoline_gdt_end:", text, re.S)
    if not match:
        fail("GDT block not found")
    quads = [int(x, 16) for x in re.findall(r"\.quad\s+0x([0-9a-fA-F]+)", match.group(1))]
    if len(quads) != 5:
        fail(f"expected 5 descriptors, got {len(quads)}")
    null, code64, data, reserved, code32 = quads
    if null != 0 or reserved != 0:
        fail("null/reserved descriptors must stay zero")
    for name, desc in (("code64", code64), ("data", data), ("code32", code32)):
        if (access_byte(desc) & 0x80) == 0:
            fail(f"{name} descriptor is not present")
        if (access_byte(desc) & 0x01) == 0:
            fail(f"{name} descriptor accessed bit is clear; AP can #PF after trampoline page becomes read-only")
    if bit(code64, 53) != 1 or bit(code64, 54) != 0:
        fail("code64 descriptor must be long-mode code")
    if bit(code32, 53) != 0 or bit(code32, 54) != 1:
        fail("code32 descriptor must be 32-bit protected-mode code")
    if (access_byte(data) & 0x18) != 0x10:
        fail("data descriptor must be a data segment")
    print("smp-trampoline-gdt: ok")


if __name__ == "__main__":
    main()
