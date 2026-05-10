#!/usr/bin/env python3
from pathlib import Path

version = Path('include/aurora/version.h').read_text()
required = [
    'AURORA_VERSION_MAJOR 0u',
    'AURORA_VERSION_MINOR 0u',
    'AURORA_VERSION_PATCH 1u',
    'AURORA_VERSION_FIX 12u',
    'AURORA_VERSION_TEXT "0.0.1.12"',
    'AURORA_SYSCALL_ABI_VERSION 0x0000010cull',
]
missing = [x for x in required if x not in version]
if missing:
    raise SystemExit('version contract failed: missing ' + ', '.join(missing))
for bad in ['stage5.0', 'stage6.0', 'stage7.0', 'stage8.0', '0x00050000', '0x00060000', '0x00070000', '0x00080000']:
    for path in [Path('include/aurora/version.h'), Path('kernel/core/kmain.c'), Path('kernel/core/shell.c'), Path('kernel/core/ktest.c')]:
        if bad in path.read_text(errors='ignore'):
            raise SystemExit(f'version contract failed: {bad} in {path}')
print('version contract checks passed')
