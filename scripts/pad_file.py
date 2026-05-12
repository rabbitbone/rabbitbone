#!/usr/bin/env python3
import os
import sys
import tempfile
from pathlib import Path

MAX_PAD_BYTES = 64 * 1024 * 1024

if len(sys.argv) != 3:
    raise SystemExit(f'usage: {sys.argv[0]} path size')

p = Path(sys.argv[1])
try:
    size = int(sys.argv[2], 0)
except ValueError as exc:
    raise SystemExit(f'invalid size: {sys.argv[2]}') from exc

if size < 0 or size > MAX_PAD_BYTES:
    raise SystemExit(f'{p}: requested padded size {size} is outside 0..{MAX_PAD_BYTES}')

data = p.read_bytes()
if len(data) > size:
    raise SystemExit(f'{p}: {len(data)} bytes exceeds fixed boot allocation {size}')

fd, tmp_name = tempfile.mkstemp(prefix=p.name + '.', suffix='.tmp', dir=str(p.parent or Path('.')))
try:
    with os.fdopen(fd, 'wb') as tmp:
        tmp.write(data)
        tmp.write(b'\x00' * (size - len(data)))
        tmp.flush()
        os.fsync(tmp.fileno())
    os.replace(tmp_name, p)
finally:
    try:
        os.unlink(tmp_name)
    except FileNotFoundError:
        pass
