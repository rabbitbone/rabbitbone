#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit(f"usage: {sys.argv[0]} build/stage2.elf")
elf = Path(sys.argv[1])
if not elf.exists():
    raise SystemExit(f"missing {elf}")

out = subprocess.check_output(["nm", "-n", str(elf)], text=True)
sym = {}
for line in out.splitlines():
    parts = line.strip().split()
    if len(parts) >= 3:
        try:
            sym[parts[2]] = int(parts[0], 16)
        except ValueError:
            pass

required = [
    "stage2_start", "protected_entry", "long_mode_entry", "pml4", "pdpt", "pd",
    "bootinfo", "E820_BUFFER", "E820_MAX_ENTRIES", "BOOTINFO_VERSION",
]
missing = [name for name in required if name not in sym]
if missing:
    raise SystemExit("stage2 layout check failed: missing symbols: " + ", ".join(missing))

errors = []

def check(cond, msg):
    if not cond:
        errors.append(msg)

stage2_start = sym["stage2_start"]
stage2_limit = 0x10000
pml4 = sym["pml4"]
pdpt = sym["pdpt"]
pd = sym["pd"]
e820 = sym["E820_BUFFER"]
e820_len = sym["E820_MAX_ENTRIES"] * 24
bootinfo = sym["bootinfo"]

check(stage2_start == 0x8000, f"stage2_start must be 0x8000, got 0x{stage2_start:x}")
check(sym["BOOTINFO_VERSION"] == 1, f"bootinfo version ABI mismatch: stage2={sym['BOOTINFO_VERSION']}, kernel expects 1")
check(pml4 % 0x1000 == 0 and pdpt % 0x1000 == 0 and pd % 0x1000 == 0, "page tables must be 4 KiB aligned")
check(pml4 == 0x9000 and pdpt == 0xA000 and pd == 0xB000, f"unexpected page table addresses: pml4=0x{pml4:x} pdpt=0x{pdpt:x} pd=0x{pd:x}")
check(0x500 <= e820 < stage2_start, f"E820 buffer must stay below stage2 load window, got 0x{e820:x}")
check(e820 + e820_len <= stage2_start, f"E820 buffer 0x{e820:x}..0x{e820+e820_len:x} overlaps stage2/page tables")
check(not (e820 < pml4 + 0x3000 and e820 + e820_len > pml4), "E820 buffer overlaps pml4/pdpt/pd page tables")
check(bootinfo < stage2_limit, f"bootinfo escaped fixed stage2 window: 0x{bootinfo:x}")
check(sym["long_mode_entry"] < pml4, "long-mode trampoline must remain before page table page")

if errors:
    for err in errors:
        print(err, file=sys.stderr)
    raise SystemExit(1)
print(f"stage2 layout checks passed: e820=0x{e820:x}..0x{e820+e820_len:x}, pml4=0x{pml4:x}, bootinfo=0x{bootinfo:x}")
