#!/usr/bin/env python3
import argparse
import sys

STAGE2_SECTORS_MAX = 64
KERNEL_LOAD_BASE = 0x10000
KERNEL_LOAD_LIMIT = 0x90000
KERNEL_SECTORS_MAX = (KERNEL_LOAD_LIMIT - KERNEL_LOAD_BASE) // 512
U64_MAX = (1 << 64) - 1

p = argparse.ArgumentParser()
p.add_argument('--stage2-sectors', type=int, required=True)
p.add_argument('--kernel-lba', type=int, required=True)
p.add_argument('--kernel-sectors', type=int, required=True)
a = p.parse_args()

errors = []
if not (1 <= a.stage2_sectors <= STAGE2_SECTORS_MAX):
    errors.append(f'--stage2-sectors must be in 1..{STAGE2_SECTORS_MAX}')
if not (1 <= a.kernel_sectors <= KERNEL_SECTORS_MAX):
    errors.append(f'--kernel-sectors must be in 1..{KERNEL_SECTORS_MAX}')
if not (1 <= a.kernel_lba <= U64_MAX):
    errors.append('--kernel-lba must be in 1..2^64-1')
elif a.kernel_lba + max(a.kernel_sectors, 0) - 1 > U64_MAX:
    errors.append('--kernel-lba + --kernel-sectors overflows 64-bit LBA')

if errors:
    for err in errors:
        print(f'gen_boot_config.py: {err}', file=sys.stderr)
    raise SystemExit(1)

print(f'.equ STAGE2_SECTORS, {a.stage2_sectors}')
print(f'.equ KERNEL_LBA, {a.kernel_lba}')
print(f'.equ KERNEL_SECTORS, {a.kernel_sectors}')
