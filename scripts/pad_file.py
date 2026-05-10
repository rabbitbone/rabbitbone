#!/usr/bin/env python3
import sys
from pathlib import Path
p = Path(sys.argv[1])
size = int(sys.argv[2])
data = p.read_bytes()
if len(data) > size:
    raise SystemExit(f'{p}: {len(data)} bytes exceeds fixed boot allocation {size}')
p.write_bytes(data + b'\x00' * (size - len(data)))
