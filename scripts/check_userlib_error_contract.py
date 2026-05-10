#!/usr/bin/env python3
from pathlib import Path
import sys
src = Path('user/lib/aurora.c').read_text()
if 'return r.error ? -r.error : r.value;' in src:
    print('userlib error contract failed: wrappers must return negative kernel error codes as-is, not negate them')
    sys.exit(1)
count = src.count('return r.error ? r.error : r.value;')
if count < 10:
    print(f'userlib error contract failed: expected wrappers to use negative-error convention, found {count}')
    sys.exit(1)
print('userlib error contract checks passed')
