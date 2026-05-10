#!/usr/bin/env python3
import argparse
p = argparse.ArgumentParser()
p.add_argument('--stage2-sectors', type=int, required=True)
p.add_argument('--kernel-lba', type=int, required=True)
p.add_argument('--kernel-sectors', type=int, required=True)
a = p.parse_args()
print(f'.equ STAGE2_SECTORS, {a.stage2_sectors}')
print(f'.equ KERNEL_LBA, {a.kernel_lba}')
print(f'.equ KERNEL_SECTORS, {a.kernel_sectors}')
