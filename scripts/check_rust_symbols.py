#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 2:
    print('usage: check_rust_symbols.py build/kernel.elf', file=sys.stderr)
    raise SystemExit(2)
elf = Path(sys.argv[1])
if not elf.exists():
    print(f'missing {elf}', file=sys.stderr)
    raise SystemExit(1)
try:
    out = subprocess.check_output(['nm', str(elf)], text=True, stderr=subprocess.STDOUT)
except Exception as exc:
    print(f'nm failed: {exc}', file=sys.stderr)
    raise SystemExit(1)
required = [
    'aurora_rust_syscall_dispatch',
    'aurora_rust_syscall_selftest',
    'aurora_rust_vfs_route',
    'aurora_rust_vfs_route_selftest',
    'aurora_rust_path_policy_check',
    'aurora_rust_path_policy_selftest',
    'aurora_rust_user_range_check',
    'aurora_rust_user_copy_step',
    'aurora_rust_usercopy_selftest',
]
missing = [sym for sym in required if sym not in out]
if missing:
    for sym in missing:
        print(f'missing Rust symbol: {sym}')
    raise SystemExit(1)
print('rust linked symbol checks passed')
