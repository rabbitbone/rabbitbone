#!/usr/bin/env python3
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / 'include' / 'aurora' / 'abi.h'
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'kernel' / 'rust' / 'abi_generated.rs'
WANTED = {
    'AURORA_SYS_MAX',
    'AURORA_KCTL_OUT_MAX',
    'AURORA_KCTL_OP_MAX',
    'AURORA_PROCESS_HANDLE_CAP',
    'AURORA_ENV_MAX',
    'AURORA_FD_CLOEXEC',
    'AURORA_FDCTL_GET',
    'AURORA_FDCTL_SET',
    'AURORA_PROT_READ',
    'AURORA_PROT_WRITE',
    'AURORA_PROT_EXEC',
    'AURORA_MAP_ANON',
    'AURORA_MAP_PRIVATE',
    'AURORA_MAP_FIXED',
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
    'AURORA_THEME_OP_GET',
    'AURORA_THEME_OP_SET',
    'AURORA_THEME_MAX',
    'AURORA_SUDO_MAX_TTL_TICKS',
    'AURORA_SUDO_DEFAULT_TTL_TICKS',
    'AURORA_SUDO_FLAG_PERSIST',
    'AURORA_SUDO_FLAG_ACTIVATE',
    'AURORA_SUDO_OP_SET_TIMEOUT',
    'AURORA_SUDO_OP_INVALIDATE',
    'AURORA_SUDO_OP_DROP',
    'AURORA_SUDO_OP_VALIDATE',
    'AURORA_SUDO_OP_STATUS',
    'AURORA_CRED_OP_USERINFO',
    'AURORA_CRED_OP_SET_EUID',
    'AURORA_CRED_OP_SET_USER',
    'AURORA_CRED_OP_LOGIN',
    'AURORA_CRED_OP_GET',
    'AURORA_GID_ROOT',
    'AURORA_UID_ROOT',
}
IDENT_RE = re.compile(r'^[A-Z][A-Z0-9_]*$')
text = ABI.read_text(encoding='utf-8')
values = {}
for m in re.finditer(r'^#define\s+(AURORA_[A-Z0-9_]+)\s+((?:0x[0-9a-fA-F]+|[0-9]+)u?)$', text, re.MULTILINE):
    name, raw = m.group(1), m.group(2)
    if name in WANTED:
        if not IDENT_RE.match(name):
            raise SystemExit(f'invalid ABI identifier: {name}')
        values[name] = int(raw.rstrip('u'), 0)
missing = sorted(WANTED - values.keys())
if missing:
    raise SystemExit('missing ABI defines: ' + ', '.join(missing))
OUT.parent.mkdir(parents=True, exist_ok=True)
body = ['// Generated from include/aurora/abi.h by scripts/gen_rust_abi.py.', '// Do not edit by hand.']
for name in sorted(values):
    body.append(f'pub const {name}: u64 = {values[name]};')
generated = '\n'.join(body) + '\n'
if not re.fullmatch(r'(//[^\n]*\n|pub const AURORA_[A-Z0-9_]+: u64 = [0-9]+;\n)+', generated):
    raise SystemExit('generated Rust ABI failed syntax whitelist')
tmp = OUT.with_suffix(OUT.suffix + '.tmp')
tmp.write_text(generated, encoding='utf-8')
os.replace(tmp, OUT)
