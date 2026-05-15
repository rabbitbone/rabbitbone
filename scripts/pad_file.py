#!/usr/bin/env python3
import argparse
import os
import stat
import tempfile
from pathlib import Path

MAX_PAD_BYTES = 64 * 1024 * 1024

p = argparse.ArgumentParser()
p.add_argument('--multiple', type=int, default=None, help='pad to the next multiple of this size')
p.add_argument('path', type=Path)
p.add_argument('size', nargs='?', help='fixed output size in bytes')
a = p.parse_args()

if a.multiple is not None and a.size is not None:
    raise SystemExit('use either --multiple or fixed size, not both')
if a.multiple is None and a.size is None:
    raise SystemExit(f'usage: {p.prog} [--multiple N] path [size]')
if a.multiple is not None and a.multiple <= 0:
    raise SystemExit('--multiple must be positive')

try:
    st = a.path.stat(follow_symlinks=False)
except OSError as exc:
    raise SystemExit(f'{a.path}: cannot stat: {exc}') from exc
if stat.S_ISLNK(st.st_mode):
    raise SystemExit(f'{a.path}: refusing to pad symlink')
if not stat.S_ISREG(st.st_mode):
    raise SystemExit(f'{a.path}: refusing to pad non-regular file')
if st.st_size > MAX_PAD_BYTES:
    raise SystemExit(f'{a.path}: input size {st.st_size} exceeds maximum {MAX_PAD_BYTES}')
try:
    data = a.path.read_bytes()
except OSError as exc:
    raise SystemExit(f'{a.path}: cannot read: {exc}') from exc
if len(data) != st.st_size:
    raise SystemExit(f'{a.path}: file changed while being read')

if a.multiple is not None:
    size = ((len(data) + a.multiple - 1) // a.multiple) * a.multiple
else:
    try:
        size = int(a.size, 0)
    except ValueError as exc:
        raise SystemExit(f'invalid size: {a.size}') from exc

if size < 0 or size > MAX_PAD_BYTES:
    raise SystemExit(f'{a.path}: requested padded size {size} is outside 0..{MAX_PAD_BYTES}')
if len(data) > size:
    raise SystemExit(f'{a.path}: {len(data)} bytes exceeds fixed boot allocation {size}')

fd, tmp_name = tempfile.mkstemp(prefix=a.path.name + '.', suffix='.tmp', dir=str(a.path.parent or Path('.')))
try:
    with os.fdopen(fd, 'wb') as tmp:
        tmp.write(data)
        tmp.write(b'\x00' * (size - len(data)))
        tmp.flush()
        os.fsync(tmp.fileno())
    os.replace(tmp_name, a.path)
finally:
    try:
        os.unlink(tmp_name)
    except FileNotFoundError:
        pass
