#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

ELF_MAGIC = b'\x7fELF'
EM_X86_64 = 62
ET_EXEC = 2
PT_LOAD = 1
PF_X = 1
PF_W = 2


def check(path: Path) -> None:
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != ELF_MAGIC:
        raise SystemExit(f'{path}: not an ELF file')
    if data[4] != 2 or data[5] != 1:
        raise SystemExit(f'{path}: not ELF64 little-endian')
    (e_type, e_machine, e_version, e_entry, e_phoff, _e_shoff, _e_flags, e_ehsize,
     e_phentsize, e_phnum, *_rest) = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    if e_type != ET_EXEC or e_machine != EM_X86_64 or e_version != 1:
        raise SystemExit(f'{path}: unsupported ELF header')
    if e_entry < 0x0000010000000000 or e_entry >= 0x0000800000000000:
        raise SystemExit(f'{path}: entry outside isolated high user window: 0x{e_entry:x}')
    if e_ehsize != 64 or e_phentsize != 56 or e_phnum == 0:
        raise SystemExit(f'{path}: invalid phdr table')
    loads = 0
    entry_in_executable_load = False
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if off + 56 > len(data):
            raise SystemExit(f'{path}: phdr outside file')
        p_type, p_flags, p_offset, p_vaddr, _p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from('<IIQQQQQQ', data, off)
        if p_type != PT_LOAD:
            continue
        loads += 1
        if (p_flags & (PF_W | PF_X)) == (PF_W | PF_X):
            raise SystemExit(f'{path}: writable+executable PT_LOAD segment is forbidden')
        if p_filesz > p_memsz:
            raise SystemExit(f'{path}: filesz > memsz')
        seg_end = p_vaddr + p_memsz
        if p_vaddr < 0x0000010000000000 or seg_end >= 0x0000800000000000:
            raise SystemExit(f'{path}: LOAD outside accepted isolated high user range')
        if p_offset + p_filesz > len(data):
            raise SystemExit(f'{path}: LOAD file range outside file')
        if p_align not in (0, 0x1000):
            raise SystemExit(f'{path}: unexpected p_align {p_align}')
        if p_align and (p_vaddr % p_align) != (p_offset % p_align):
            raise SystemExit(f'{path}: LOAD vaddr/offset alignment mismatch')
        if (p_flags & PF_X) and p_vaddr <= e_entry < seg_end:
            entry_in_executable_load = True
    if loads == 0:
        raise SystemExit(f'{path}: no PT_LOAD segment')
    if not entry_in_executable_load:
        raise SystemExit(f'{path}: entry point is not inside an executable PT_LOAD segment')
    if b'\x0f\x05' in data:
        raise SystemExit(f'{path}: contains syscall instruction; Rabbitbone Stage 3 ABI must use int 0x80')
    if b'\xcd\x80' not in data:
        raise SystemExit(f'{path}: no int 0x80 syscall entry found')
    print(f'{path}: ok entry=0x{e_entry:x} load_segments={loads} bytes={len(data)}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('elf', nargs='+')
    ns = ap.parse_args()
    for p in ns.elf:
        check(Path(p))

if __name__ == '__main__':
    main()
