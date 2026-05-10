#!/usr/bin/env python3
import argparse
from pathlib import Path

def sym(name: str) -> str:
    out = []
    for ch in name:
        if ch.isalnum(): out.append(ch)
        else: out.append('_')
    return ''.join(out).strip('_') or 'blob'

def emit_array(name, data):
    lines = [f'static const unsigned char {name}[] __attribute__((aligned(16))) = {{']
    for i in range(0, len(data), 12):
        chunk = data[i:i+12]
        lines.append('    ' + ', '.join(f'0x{b:02x}' for b in chunk) + ',')
    lines.append('};')
    return '\n'.join(lines)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('mapping', nargs='+', help='src:dest')
    ns = ap.parse_args()
    entries = []
    src = ['#include <aurora/user_bins.h>', '#include <aurora/ramfs.h>', '#include <aurora/vfs.h>', '#include <aurora/libc.h>', '']
    for idx, m in enumerate(ns.mapping):
        if ':' not in m:
            raise SystemExit(f'invalid mapping {m}')
        path, dest = m.split(':', 1)
        data = Path(path).read_bytes()
        name = f'userbin_{idx}_{sym(dest)}'
        src.append(emit_array(name, data))
        src.append('')
        entries.append((dest, name, len(data)))
    src.append('typedef struct user_bin_entry { const char *path; const unsigned char *data; unsigned long size; } user_bin_entry_t;')
    src.append('static const user_bin_entry_t user_bins[] = {')
    for dest, name, size in entries:
        src.append(f'    {{ "{dest}", {name}, {size}u }},')
    src.append('};')
    src.append('')
    src.append('void user_bins_install(void) {')
    src.append('    (void)vfs_mkdir("/bin");')
    for dest, name, size in entries:
        src.append(f'    (void)vfs_unlink("{dest}");')
        src.append(f'    (void)vfs_create("{dest}", {name}, {size}u);')
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
    Path(ns.out).parent.mkdir(parents=True, exist_ok=True)
    Path(ns.out).write_text('\n'.join(src) + '\n')

if __name__ == '__main__':
    main()
