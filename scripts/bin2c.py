#!/usr/bin/env python3
import argparse
import re
from pathlib import Path

C_IDENT_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

def sym(name: str) -> str:
    out = []
    for ch in name:
        if ch.isalnum(): out.append(ch)
        else: out.append('_')
    value = ''.join(out).strip('_') or 'blob'
    if value[0].isdigit():
        value = '_' + value
    if not C_IDENT_RE.match(value):
        raise SystemExit(f'invalid generated symbol {value!r}')
    return value

def c_string(value: str) -> str:
    escaped = []
    for ch in value:
        o = ord(ch)
        if ch == '"': escaped.append('\\"')
        elif ch == '\\': escaped.append('\\\\')
        elif ch == '\n': escaped.append('\\n')
        elif ch == '\r': escaped.append('\\r')
        elif ch == '\t': escaped.append('\\t')
        elif 32 <= o <= 126: escaped.append(ch)
        else: escaped.append(f'\\x{o:02x}')
    return '"' + ''.join(escaped) + '"'

def validate_dest(dest: str) -> None:
    if not dest.startswith('/') or '\x00' in dest:
        raise SystemExit(f'invalid destination path {dest!r}')
    if '//' in dest or '/../' in dest or dest.endswith('/..') or '/./' in dest or dest.endswith('/.'):
        raise SystemExit(f'non-normal destination path {dest!r}')

def emit_array(name: str, data: bytes) -> str:
    lines = [f'static const unsigned char {name}[] __attribute__((aligned(16))) = {{']
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',')
    lines.append('};')
    return '\n'.join(lines)

def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('mapping', nargs='+', help='src:dest')
    ns = ap.parse_args()

    entries = []
    blobs = []
    by_source = {}
    seen_dest = set()
    src = ['#include <aurora/user_bins.h>', '#include <aurora/ramfs.h>', '#include <aurora/vfs.h>', '#include <aurora/abi.h>', '#include <aurora/libc.h>', '']

    for idx, m in enumerate(ns.mapping):
        if ':' not in m:
            raise SystemExit(f'invalid mapping {m}')
        path_text, dest = m.split(':', 1)
        validate_dest(dest)
        if dest in seen_dest:
            raise SystemExit(f'duplicate destination path {dest!r}')
        seen_dest.add(dest)

        path = Path(path_text)
        data = path.read_bytes()
        key = str(path.resolve(strict=True))
        blob = by_source.get(key)
        if blob is None:
            name = f'userbin_{len(blobs)}_{sym(dest)}'
            blob = { 'name': name, 'size': len(data), 'data': data, 'first_dest': dest }
            by_source[key] = blob
            blobs.append(blob)
        elif blob['data'] != data:
            raise SystemExit(f'source changed while generating bins: {path_text}')

        entries.append((dest, blob['name'], blob['size'], blob['first_dest']))

    for blob in blobs:
        src.append(emit_array(blob['name'], blob['data']))
        src.append('')

    src.append('typedef struct user_bin_entry { const char *path; const unsigned char *data; unsigned long size; const char *link_from; } user_bin_entry_t;')
    src.append('static const user_bin_entry_t user_bins[] = {')
    for dest, name, size, first_dest in entries:
        link_from = '0' if first_dest == dest else c_string(first_dest)
        src.append(f'    {{ {c_string(dest)}, {name}, {size}u, {link_from} }},')
    src.append('};')
    src.append('')

    parent_dirs = []
    for dest, _, _, _ in entries:
        parent = str(Path(dest).parent)
        if parent not in ('/', '.') and parent not in parent_dirs:
            parent_dirs.append(parent)
    parent_dirs.sort(key=lambda x: (x.count('/'), x))

    src.append('void user_bins_install(void) {')
    for parent in parent_dirs:
        src.append(f'    (void)vfs_mkdir({c_string(parent)});')
    for dest, name, size, first_dest in entries:
        d = c_string(dest)
        src.append(f'    (void)vfs_unlink({d});')
        if first_dest == dest:
            src.append(f'    (void)vfs_create({d}, {name}, {size}u);')
            src.append(f'    (void)vfs_chmod({d}, 0755u);')
            src.append(f'    (void)vfs_chown({d}, AURORA_UID_ROOT, AURORA_GID_ROOT);')
        else:
            src.append(f'    if (vfs_link({c_string(first_dest)}, {d}) != VFS_OK) (void)vfs_create({d}, {name}, {size}u);')
            src.append(f'    (void)vfs_chmod({d}, 0755u);')
            src.append(f'    (void)vfs_chown({d}, AURORA_UID_ROOT, AURORA_GID_ROOT);')
    src.append('}')
    src.append('')

    src.append('bool user_bins_selftest(void) {')
    src.append('    for (unsigned long i = 0; i < sizeof(user_bins) / sizeof(user_bins[0]); ++i) {')
    src.append('        vfs_stat_t st;')
    src.append('        if (vfs_stat(user_bins[i].path, &st) != VFS_OK) return false;')
    src.append('        if (st.type != VFS_NODE_FILE || st.size != user_bins[i].size) return false;')
    src.append('        unsigned char head[4]; unsigned long got = 0;')
    src.append('        if (vfs_read(user_bins[i].path, 0, head, sizeof(head), &got) != VFS_OK || got != sizeof(head)) return false;')
    src.append('        if (head[0] != 0x7f || head[1] != 0x45 || head[2] != 0x4c || head[3] != 0x46) return false;')
    src.append('    }')
    src.append('    return true;')
    src.append('}')

    out = Path(ns.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(out.suffix + '.tmp')
    tmp.write_text('\n'.join(src) + '\n', encoding='utf-8')
    tmp.replace(out)

if __name__ == '__main__':
    main()
