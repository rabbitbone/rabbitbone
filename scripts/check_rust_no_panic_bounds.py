#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 2:
    print('usage: check_rust_no_panic_bounds.py build/kernel/rust/lib.o', file=sys.stderr)
    raise SystemExit(2)
obj = Path(sys.argv[1])
if not obj.exists():
    print(f'missing {obj}', file=sys.stderr)
    raise SystemExit(1)
out = subprocess.check_output(['nm', '-u', str(obj)], text=True, stderr=subprocess.STDOUT)
for line in out.splitlines():
    if 'core' in line and 'panicking' in line:
        print(f'forbidden Rust core panic dependency: {line}')
        raise SystemExit(1)
print('rust core panic dependency checks passed')
