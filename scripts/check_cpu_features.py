#!/usr/bin/env python3
from pathlib import Path
src = Path('kernel/arch/x86_64/cpu.c').read_text()
required = [
    '(1ull << 9)',
    '(1ull << 10)',
    '~(1ull << 2)',
    'cpu_sse_enabled',
]
missing = [s for s in required if s not in src]
if missing:
    raise SystemExit('missing CPU feature setup markers: ' + ', '.join(missing))
print('cpu feature setup checks passed')
