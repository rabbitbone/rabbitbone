# AuroraOS Stage 6 Process Control

Stage 6 keeps the existing C/C++/asm monolithic kernel and linked Rust safety-boundary modules. It adds a central version/ABI contract, removes stale stage literals from kernel output, moves the frame bitmap out of kernel BSS, expands the boot kernel sector budget, and adds synchronous process-control contracts.

## Added

- Shared version header: `include/aurora/version.h`.
- Shared syscall ABI header: `include/aurora/abi.h`.
- Kernel/user syscall ID definitions now use the shared ABI header.
- `AURORA_SYSCALL_ABI_VERSION` is the single source for the version syscall and `ktest` expectation.
- Process-control syscalls:
  - `spawn`
  - `wait`
- Kernel process API:
  - `process_spawn`
  - `process_wait`
- Shell commands:
  - `spawn PATH [ARGS]`
  - `wait PID`
- User program:
  - `/bin/spawncheck`
- `ktest` now covers:
  - shared version contract
  - Rust syscall names for all syscall IDs
  - kernel-side spawn/wait
  - Rust-dispatched wait syscall
  - user-mode rejection of nested spawn/wait while the current executor is single-active-process
  - process registry retention after spawn/wait

## Memory layout change

The physical frame bitmap moved from static kernel BSS to fixed low memory at `0x100000`. This removes 128 KiB from BSS and restores a large early-stack margin.

## Boot budget

The reserved kernel boot area is now 768 sectors. Stage2 still reads the kernel in 32-sector chunks. The partition remains at LBA 2048.
