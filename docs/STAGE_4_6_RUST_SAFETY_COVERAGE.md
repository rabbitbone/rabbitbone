# AuroraOS Stage 4.6 Rust safety boundary and ktest expansion

Stage 4.6 keeps the existing C/C++/asm kernel architecture and moves only the high-risk boundary logic into Rust.

## Rust modules now linked into the kernel

- `kernel/rust/syscall_dispatch.rs`
  - typed syscall number decode
  - handle number validation
  - hostile pointer null/length checks before C backend dispatch
  - payload size caps
- `kernel/rust/vfs_route.rs`
  - absolute path routing
  - path normalization
  - longest-prefix mount selection
- `kernel/rust/path_policy.rs`
  - absolute path requirement
  - disallowed byte filtering
  - component length enforcement
- `kernel/rust/usercopy.rs`
  - user address overflow checks
  - low-address guard
  - high user limit guard
  - per-page copy step arithmetic

The Rust layer is still `#![no_std]`, freestanding, panic-abort, no allocator, and linked into `kernel.elf` as `build/kernel/rust/lib.o`.

## What deliberately stayed in C/C++

- ATA PIO, block layer, MBR and EXT4 backend stayed C because they are already directly exercised by `ktest` and host tests.
- heap/VMM/scheduler stayed C for now because rewriting them would be a high-risk architecture change.
- syscall backend implementations stayed C, but the dangerous dispatch/validation boundary now enters through Rust.

## ktest expansion

`ktest` now includes:

- Rust usercopy selftest
- Rust path policy selftest
- Rust syscall validator negative tests
- VFS rejection of relative and disallowed paths
- ring3 `/bin/badpath` for user-originated path policy failures
- ring3 `/bin/statcheck` for stat/mkdir/ticks syscall coverage
- postcondition coverage suite validating:
  - all syscall IDs have stable Rust names
  - all Rust safety modules remain callable
  - no user process remains active after userland tests
  - CR3 is restored to kernel PML4
  - block registry and root ramfs remain healthy

This is not line-coverage instrumentation yet, but it is now a test contract over the critical kernel/user and VFS routing surface.
