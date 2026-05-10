#!/usr/bin/env python3
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit(f"usage: {sys.argv[0]} build/aurora.img")
img = Path(sys.argv[1]).read_bytes()
if len(img) < 4096:
    raise SystemExit("image too small")
print(f"image_bytes={len(img)}")
print(f"mbr_signature={img[510:512].hex()}")
part = img[446:462]
start = int.from_bytes(part[8:12], "little")
sectors = int.from_bytes(part[12:16], "little")
print(f"partition0_boot=0x{part[0]:02x} type=0x{part[4]:02x} start_lba={start} sectors={sectors}")
print("boot_layout:")
print("  stage1: lba=0 sectors=1")
print("  stage2: lba=1 sectors=64")
print("  kernel: lba=65 sectors=1016")
print(f"  ext4_seed: lba={start}")
print("stage1_strings:")
for marker in [b"Aurora stage1: BIOS", b"Aurora stage1: stage2", b"Aurora stage1: INT13"]:
    print(f"  {marker.decode(errors='ignore')}: offset={img.find(marker)}")
print("stage2_strings:")
for marker in [b"Aurora stage2: entered", b"Aurora stage2: loading kernel", b"Aurora stage2: entering long mode", b"Aurora stage2: long mode active", b"Aurora stage2: jumping kernel"]:
    print(f"  {marker.decode(errors='ignore')}: offset={img.find(marker)}")

print("kernel_strings:")
for marker in [b"Aurora kernel: entry reached", b"Aurora kernel: calling kernel_main"]:
    print(f"  {marker.decode(errors='ignore')}: offset={img.find(marker)}")
