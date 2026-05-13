#!/usr/bin/env python3
import argparse
import math
import subprocess
import sys
from pathlib import Path

STAGE2_SECTORS_MAX = 64
SECTOR_SIZE = 512
KERNEL_LOAD_BASE = 0x10000
KERNEL_LOAD_LIMIT = 0x9f000
KERNEL_SECTORS_MAX = (KERNEL_LOAD_LIMIT - KERNEL_LOAD_BASE) // SECTOR_SIZE
U64_MAX = (1 << 64) - 1

p = argparse.ArgumentParser()
p.add_argument('--stage2-sectors', type=int, required=True)
p.add_argument('--kernel-lba', type=int, required=True)
g = p.add_mutually_exclusive_group(required=True)
g.add_argument('--kernel-sectors', type=int)
g.add_argument('--kernel-bin', type=Path)
p.add_argument('--kernel-elf', type=Path)
a = p.parse_args()

if a.kernel_bin is not None:
    try:
        kernel_size = a.kernel_bin.stat().st_size
    except OSError as exc:
        print(f'gen_boot_config.py: cannot stat kernel binary {a.kernel_bin}: {exc}', file=sys.stderr)
        raise SystemExit(1) from exc
    kernel_sectors = max(1, math.ceil(kernel_size / SECTOR_SIZE))
else:
    kernel_size = None
    kernel_sectors = a.kernel_sectors

def read_kernel_symbols(path: Path):
    try:
        out = subprocess.check_output(['nm', '-n', str(path)], text=True)
    except (OSError, subprocess.CalledProcessError) as exc:
        print(f'gen_boot_config.py: cannot read symbols from {path}: {exc}', file=sys.stderr)
        raise SystemExit(1) from exc
    syms = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                syms[parts[2]] = int(parts[0], 16)
            except ValueError:
                pass
    return syms

kernel_symbols = read_kernel_symbols(a.kernel_elf) if a.kernel_elf is not None else {}

errors = []
if not (1 <= a.stage2_sectors <= STAGE2_SECTORS_MAX):
    errors.append(f'--stage2-sectors must be in 1..{STAGE2_SECTORS_MAX}')
if not (1 <= kernel_sectors <= KERNEL_SECTORS_MAX):
    detail = f' ({kernel_size} bytes)' if kernel_size is not None else ''
    errors.append(f'kernel image{detail} requires {kernel_sectors} sectors; maximum is {KERNEL_SECTORS_MAX} sectors for 0x{KERNEL_LOAD_BASE:x}..0x{KERNEL_LOAD_LIMIT:x}')
if kernel_symbols:
    missing = [name for name in ('__kernel_start', '__kernel_end') if name not in kernel_symbols]
    if missing:
        errors.append('kernel ELF is missing required layout symbols: ' + ', '.join(missing))
    else:
        start = kernel_symbols['__kernel_start']
        end = kernel_symbols['__kernel_end']
        if start != KERNEL_LOAD_BASE:
            errors.append(f'kernel ELF start 0x{start:x} does not match stage2 load base 0x{KERNEL_LOAD_BASE:x}')
        if end > KERNEL_LOAD_LIMIT:
            errors.append(f'kernel memory image ends at 0x{end:x}; maximum safe low-memory end is 0x{KERNEL_LOAD_LIMIT:x}')
if not (1 <= a.kernel_lba <= U64_MAX):
    errors.append('--kernel-lba must be in 1..2^64-1')
elif a.kernel_lba + max(kernel_sectors, 0) - 1 > U64_MAX:
    errors.append('--kernel-lba + kernel sectors overflows 64-bit LBA')

if errors:
    for err in errors:
        print(f'gen_boot_config.py: {err}', file=sys.stderr)
    raise SystemExit(1)

print(f'.equ STAGE2_SECTORS, {a.stage2_sectors}')
print(f'.equ KERNEL_LBA, {a.kernel_lba}')
print(f'.equ KERNEL_SECTORS, {kernel_sectors}')
