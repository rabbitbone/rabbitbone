#!/usr/bin/env python3
import sys
from pathlib import Path


def main() -> None:
    if len(sys.argv) != 2:
        print('usage: check_boot_sector.py STAGE1_BIN', file=sys.stderr)
        raise SystemExit(2)
    p = Path(sys.argv[1])
    data = p.read_bytes()
    if len(data) != 512:
        raise SystemExit(f'{p}: boot sector must be exactly 512 bytes, got {len(data)}')
    if data[510:512] != b'\x55\xaa':
        raise SystemExit(f'{p}: missing 0x55aa boot signature')


if __name__ == '__main__':
    main()
