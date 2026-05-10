# AuroraOS Stage 1.2 Boot Path Fix

Status: completed as a focused VMware triple-fault repair.

## Fixed

The VM reached:

```text
Aurora stage2: entering long mode
```

and then VMware reported a triple fault. The root cause was an overlap in `boot/stage2.S`:

- `collect_e820` wrote BIOS E820 entries to physical `0x9000`.
- The stage2 bootstrap page tables also placed `pml4` at physical `0x9000`.
- The BIOS memory map therefore corrupted the PML4 before `cr3` was loaded.
- Enabling paging with the corrupted PML4 caused a fault before the kernel had an IDT, escalating to triple fault.

## Changes

- Moved the E820 buffer to `0x5000..0x5600`.
- Kept bootstrap page tables at `0x9000`, `0xA000`, `0xB000`.
- Fixed the stage2 bootinfo ABI version to `1`, matching `aurora_bootinfo.h`.
- Added serial output after the CPU is in long mode:

```text
Aurora stage2: long mode active
Aurora stage2: jumping kernel
```

- Added `scripts/check_stage2_layout.py` to prevent E820/page-table overlap regressions.
- Added the layout check to `make test`.

## Expected serial sequence

```text
Aurora stage1: BIOS boot sector entered
Aurora stage1: stage2 loaded, jumping
Aurora stage2: entered
Aurora stage2: collecting E820
Aurora stage2: loading kernel
Aurora stage2: kernel loaded
Aurora stage2: entering long mode
Aurora stage2: long mode active
Aurora stage2: jumping kernel
Aurora kernel: entry reached
Aurora kernel: calling kernel_main
```

After that the C kernel should start normal console/log initialization. If the serial log reaches `jumping kernel` but the kernel is silent, the next fault boundary is in `kernel_start` or `kernel_main`.
