#!/usr/bin/env python3
import re
import subprocess
import sys
from pathlib import Path

if len(sys.argv) != 3:
    print('usage: check_kernel_layout.py KERNEL_ELF KERNEL_MAP', file=sys.stderr)
    sys.exit(2)
elf = Path(sys.argv[1])
map_file = Path(sys.argv[2])
if not elf.exists() or not map_file.exists():
    print('kernel layout check: missing input', file=sys.stderr)
    sys.exit(2)

nm = subprocess.check_output(['nm', '-n', str(elf)], text=True)
syms = {}
max_alloc_sym = 0
max_alloc_name = ''
for line in nm.splitlines():
    parts = line.split()
    if len(parts) < 3:
        continue
    try:
        addr = int(parts[0], 16)
    except ValueError:
        continue
    typ = parts[1]
    name = parts[2]
    syms[name] = addr
    if typ in 'TtRrDdBb' and not name.startswith('__'):
        if addr > max_alloc_sym:
            max_alloc_sym = addr
            max_alloc_name = name

required = ['__kernel_start', '__kernel_end', '__bss_start', '__bss_end']
missing = [s for s in required if s not in syms]
if missing:
    print('kernel layout check: missing symbols: ' + ', '.join(missing), file=sys.stderr)
    sys.exit(1)

if not (syms['__kernel_start'] < syms['__bss_start'] <= syms['__bss_end'] <= syms['__kernel_end']):
    print('kernel layout check: invalid symbol order', file=sys.stderr)
    sys.exit(1)

if max_alloc_sym >= syms['__kernel_end']:
    print(f'kernel layout check: symbol {max_alloc_name} at 0x{max_alloc_sym:x} is beyond __kernel_end=0x{syms["__kernel_end"]:x}', file=sys.stderr)
    sys.exit(1)

LOW_MEMORY_RESERVED_START = 0x9f000
EARLY_STACK_TOP = 0x1f0000
MIN_EARLY_STACK_BYTES = 64 * 1024
if syms['__kernel_end'] > LOW_MEMORY_RESERVED_START:
    print(f'kernel layout check: kernel low-memory image overlaps reserved BIOS/video region: __kernel_end=0x{syms["__kernel_end"]:x} limit=0x{LOW_MEMORY_RESERVED_START:x}', file=sys.stderr)
    sys.exit(1)
if syms['__kernel_end'] + MIN_EARLY_STACK_BYTES > EARLY_STACK_TOP:
    print(f'kernel layout check: early stack margin too small: __kernel_end=0x{syms["__kernel_end"]:x} stack_top=0x{EARLY_STACK_TOP:x} min={MIN_EARLY_STACK_BYTES}', file=sys.stderr)
    sys.exit(1)

text = map_file.read_text(errors='replace')
for orphan in ('.ldata', '.lbss', '.lrodata'):
    if re.search(rf'^\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+.*\s{re.escape(orphan)}\s*$', text, re.M):
        print(f'kernel layout check: orphan output section {orphan} detected', file=sys.stderr)
        sys.exit(1)

bss_len = syms['__bss_end'] - syms['__bss_start']
kern_len = syms['__kernel_end'] - syms['__kernel_start']
print(f'kernel layout checks passed: kernel=0x{syms["__kernel_start"]:x}..0x{syms["__kernel_end"]:x} bytes={kern_len} bss={bss_len} early_stack_margin={EARLY_STACK_TOP - syms["__kernel_end"]} max_symbol={max_alloc_name}@0x{max_alloc_sym:x}')
