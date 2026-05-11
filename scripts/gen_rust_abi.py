#!/usr/bin/env python3
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / 'include' / 'aurora' / 'abi.h'
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'kernel' / 'rust' / 'abi_generated.rs'
WANTED = {
    'AURORA_SYS_MAX',
    'AURORA_PROCESS_HANDLE_CAP',
    'AURORA_ENV_MAX',
    'AURORA_FD_CLOEXEC',
    'AURORA_FDCTL_GET',
    'AURORA_FDCTL_SET',
    'AURORA_O_RDONLY',
    'AURORA_O_WRONLY',
    'AURORA_O_RDWR',
    'AURORA_O_ACCMODE',
    'AURORA_O_CREAT',
    'AURORA_O_EXCL',
    'AURORA_O_TRUNC',
    'AURORA_O_APPEND',
    'AURORA_O_DIRECTORY',
    'AURORA_O_CLOEXEC',
    'AURORA_SEEK_SET',
    'AURORA_SEEK_CUR',
    'AURORA_SEEK_END',
    'AURORA_POLL_READ',
    'AURORA_POLL_WRITE',
    'AURORA_POLL_HUP',
    'AURORA_TTY_MODE_RAW',
    'AURORA_TTY_MODE_ECHO',
    'AURORA_TTY_MODE_CANON',
    'AURORA_TTY_READ_NONBLOCK',
}
text = ABI.read_text(encoding='utf-8')
values = {}
for m in re.finditer(r'^#define\s+(AURORA_[A-Z0-9_]+)\s+((?:0x[0-9a-fA-F]+|[0-9]+)u?)$', text, re.MULTILINE):
    name, raw = m.group(1), m.group(2)
    if name in WANTED:
        values[name] = int(raw.rstrip('u'), 0)
missing = sorted(WANTED - values.keys())
if missing:
    raise SystemExit('missing ABI defines: ' + ', '.join(missing))
OUT.parent.mkdir(parents=True, exist_ok=True)
body = ['// Generated from include/aurora/abi.h by scripts/gen_rust_abi.py.', '// Do not edit by hand.']
for name in sorted(values):
    body.append(f'pub const {name}: u64 = {values[name]};')
OUT.write_text('\n'.join(body) + '\n', encoding='utf-8')
