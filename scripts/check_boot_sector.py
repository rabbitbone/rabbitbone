#!/usr/bin/env python3
import sys
from pathlib import Path
p = Path(sys.argv[1])
data = p.read_bytes()
if len(data) != 512:
    raise SystemExit(f'{p}: boot sector must be exactly 512 bytes, got {len(data)}')
if data[510:512] != b'\x55\xaa':
    raise SystemExit(f'{p}: missing 0x55aa boot signature')
