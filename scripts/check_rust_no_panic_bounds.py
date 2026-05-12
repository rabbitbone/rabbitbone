#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path

FORBIDDEN_SUBSTRINGS = (
    'panicking',
    'core::panicking',
    'core::slice::index',
    'panic_bounds_check',
    'panic_fmt',
    'panic_nounwind',
    'panic_cannot_unwind',
    'panic_in_cleanup',
)

if len(sys.argv) != 2:
    print('usage: check_rust_no_panic_bounds.py build/kernel/rust/lib.o', file=sys.stderr)
    raise SystemExit(2)
obj = Path(sys.argv[1])
if not obj.exists():
    print(f'missing {obj}', file=sys.stderr)
    raise SystemExit(1)
try:
    out = subprocess.check_output(['nm', '-u', str(obj)], text=True, stderr=subprocess.STDOUT)
except Exception as exc:
    print(f'nm failed: {exc}', file=sys.stderr)
    raise SystemExit(1)
for line in out.splitlines():
    demangled = line
    if any(token in demangled for token in FORBIDDEN_SUBSTRINGS):
        print(f'forbidden Rust panic/bounds dependency: {line}')
        raise SystemExit(1)
print('rust core panic/bounds dependency checks passed')
