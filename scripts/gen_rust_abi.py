#!/usr/bin/env python3
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / 'include' / 'rabbitbone' / 'abi.h'
WANTED = {
    'RABBITBONE_SYS_MAX',
    'RABBITBONE_KCTL_OUT_MAX',
    'RABBITBONE_KCTL_OP_MAX',
    'RABBITBONE_PROCESS_HANDLE_CAP',
    'RABBITBONE_ENV_MAX',
    'RABBITBONE_FD_CLOEXEC',
    'RABBITBONE_FDCTL_GET',
    'RABBITBONE_FDCTL_SET',
    'RABBITBONE_PROT_READ',
    'RABBITBONE_PROT_WRITE',
    'RABBITBONE_PROT_EXEC',
    'RABBITBONE_MAP_ANON',
    'RABBITBONE_MAP_PRIVATE',
    'RABBITBONE_MAP_FIXED',
    'RABBITBONE_MAP_SHARED',
    'RABBITBONE_NSIG',
    'RABBITBONE_SIGUSR1',
    'RABBITBONE_SIGTERM',
    'RABBITBONE_SIG_SETMASK',
    'RABBITBONE_O_RDONLY',
    'RABBITBONE_O_WRONLY',
    'RABBITBONE_O_RDWR',
    'RABBITBONE_O_ACCMODE',
    'RABBITBONE_O_CREAT',
    'RABBITBONE_O_EXCL',
    'RABBITBONE_O_TRUNC',
    'RABBITBONE_O_APPEND',
    'RABBITBONE_O_DIRECTORY',
    'RABBITBONE_O_CLOEXEC',
    'RABBITBONE_SEEK_SET',
    'RABBITBONE_SEEK_CUR',
    'RABBITBONE_SEEK_END',
    'RABBITBONE_POLL_READ',
    'RABBITBONE_POLL_WRITE',
    'RABBITBONE_POLL_HUP',
    'RABBITBONE_TTY_MODE_RAW',
    'RABBITBONE_TTY_MODE_ECHO',
    'RABBITBONE_TTY_MODE_CANON',
    'RABBITBONE_TTY_READ_NONBLOCK',
    'RABBITBONE_THEME_OP_GET',
    'RABBITBONE_THEME_OP_SET',
    'RABBITBONE_THEME_MAX',
    'RABBITBONE_SUDO_MAX_TTL_TICKS',
    'RABBITBONE_SUDO_DEFAULT_TTL_TICKS',
    'RABBITBONE_SUDO_FLAG_PERSIST',
    'RABBITBONE_SUDO_FLAG_ACTIVATE',
    'RABBITBONE_SUDO_OP_SET_TIMEOUT',
    'RABBITBONE_SUDO_OP_INVALIDATE',
    'RABBITBONE_SUDO_OP_DROP',
    'RABBITBONE_SUDO_OP_VALIDATE',
    'RABBITBONE_SUDO_OP_STATUS',
    'RABBITBONE_CRED_OP_USERINFO',
    'RABBITBONE_CRED_OP_SET_EUID',
    'RABBITBONE_CRED_OP_SET_USER',
    'RABBITBONE_CRED_OP_LOGIN',
    'RABBITBONE_CRED_OP_GET',
    'RABBITBONE_GID_ROOT',
    'RABBITBONE_UID_ROOT',
}
IDENT_RE = re.compile(r'^[A-Z][A-Z0-9_]*$')
DEFINE_RE = re.compile(r'^#define\s+(RABBITBONE_[A-Z0-9_]+)\s+((?:0x[0-9a-fA-F]+|[0-9]+)u?)$', re.MULTILINE)
GENERATED_RE = re.compile(r'(//[^\n]*\n|pub const RABBITBONE_[A-Z0-9_]+: u64 = [0-9]+;\n)+')


def atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=str(path.parent))
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as tmp:
            tmp.write(text)
            tmp.flush()
            os.fsync(tmp.fileno())
        os.replace(tmp_name, path)
    finally:
        try:
            os.unlink(tmp_name)
        except FileNotFoundError:
            pass


def main(argv: list[str]) -> int:
    if len(argv) > 2:
        print(f'usage: {argv[0]} [OUT]', file=sys.stderr)
        return 2
    out = Path(argv[1]) if len(argv) == 2 else ROOT / 'build' / 'kernel' / 'rust' / 'abi_generated.rs'
    text = ABI.read_text(encoding='utf-8')
    values = {}
    for m in DEFINE_RE.finditer(text):
        name, raw = m.group(1), m.group(2)
        if name in WANTED:
            if not IDENT_RE.match(name):
                raise SystemExit(f'invalid ABI identifier: {name}')
            value = int(raw.rstrip('u'), 0)
            if value < 0:
                raise SystemExit(f'invalid negative ABI value: {name}')
            values[name] = value
    missing = sorted(WANTED - values.keys())
    if missing:
        raise SystemExit('missing ABI defines: ' + ', '.join(missing))

    body = ['// Generated from include/rabbitbone/abi.h by scripts/gen_rust_abi.py.', '// Do not edit by hand.']
    for name in sorted(values):
        body.append(f'pub const {name}: u64 = {values[name]};')
    generated = '\n'.join(body) + '\n'
    if not GENERATED_RE.fullmatch(generated):
        raise SystemExit('generated Rust ABI failed syntax whitelist')
    atomic_write_text(out, generated)
    return 0


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
