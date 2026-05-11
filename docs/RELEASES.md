# AuroraOS release notes

This file keeps the release history short enough to be useful. Older one-off stage notes were folded into this summary.

## 0.0.1.28

Stage 19.5 EXT4 extent-leaf split runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.28` / `0x0000011c`.
- Added real split support for depth-1 EXT4 indexed extent leaves when a leaf reaches capacity.
- Indexed extent roots can now reference multiple external leaf blocks, with sorted root indexes and read/write routing across leaf boundaries.
- Added truncate/unlink coverage for multi-leaf indexed extent files, including metadata leaf block reclamation.
- Extended EXT4 metadata validation to scan allocated inodes, inspect extent trees, and verify referenced extent data/metadata blocks are actually allocated in the block bitmap.
- Added CLI EXT4 diagnostics for checked inode counts and extent tree shape/data/metadata block accounting.
- Extended `ktest` with a 96-sparse-extent runtime stress file that forces leaf split, verifies sparse readback, validates zero-filled holes, truncates across leaves, and checks allocation counters return to baseline after unlink.

## 0.0.1.27

Stage 19.4 EXT4 indexed-extent runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.27` / `0x0000011b`.
- Added indexed EXT4 extent promotion: overflowed inline extent trees move into a depth-1 root with an external leaf block.
- Kept sparse EXT4 growth semantics across indexed extents: gap writes keep holes unallocated while reads return zero-filled data.
- Added safe freeing/truncation/unlink for indexed extent data and metadata leaf blocks.
- Added public inode extent introspection helpers for runtime contracts.
- Extended `ktest` coverage for five-discontiguous-block sparse files, indexed extent readback, hole reads, truncate, unlink, and metadata consistency.

## 0.0.1.26

Stage 19.3 EXT4 extent-write runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.26` / `0x0000011a`.
- Added inline EXT4 extent trees for newly-created regular files and made VFS create/write/truncate/unlink operate on those inodes.
- Added sparse EXT4 growth semantics: gap writes and expanding truncate keep holes unallocated while reads return zero-filled data.
- Added EXT4 metadata validation that cross-checks group descriptors, allocation bitmaps, and superblock counters.
- Extended `ktest` coverage for extent-backed files, sparse writes, sparse truncation, metadata consistency before/after mutation tests, and CLI `ext4` fsck reporting.

## 0.0.1.15

Stage 10 exec and descriptor-control update.

- Bumped the kernel version and syscall ABI to `0.0.1.15` / `0x0000010f`.
- Added `fdctl` and close-on-exec descriptor flags.
- Added `execve` with argv and environment-vector handoff.
- Added `/bin/execfdcheck`, `/bin/execvecheck`, and `/bin/exectarget` userland regression programs.
- Extended ktest coverage for inherited descriptors, close-on-exec behavior, and environment passing across exec.

## 0.0.1.14

VMM regression update.

- Bumped the kernel version and syscall ABI to `0.0.1.14` / `0x0000010e`.
- Fixed page-table creation so intermediate paging tables receive only valid table-level flags, while leaf entries keep leaf-level permissions.
- Refreshed the README to match the current release line.

## 0.0.1.13

Hardening update over the `0.0.1.12` baseline.

- Bumped the kernel version and syscall ABI to `0.0.1.13` / `0x0000010d`.
- Added safer numeric parsing for shell commands that accept PIDs or run counts.
- Hardened process initialization, wait handling, address-space generation, and deadlock reporting.
- Hid raw process address-space pointers from user-visible process info.
- Added size limits for syscall read/write requests.
- Tightened VFS mount IDs, root unmount behavior, ramfs inode allocation, and ramfs growth checks.
- Added small driver and timer guardrails around PIT, serial, ATA, and block I/O paths.

## 0.0.1.12

Rust boundary reproducibility fix over the Stage 9 hardening baseline.

- Removed dependency on hashed internal `core::panicking::panic_bounds_check::*` symbols.
- Reworked Rust VFS route and path-policy code so checked indexing is not emitted into the freestanding kernel object.
- Added `scripts/check_rust_no_panic_bounds.py`.
- Kept Rust source compatible with Rust 2021 syntax.

## 0.0.1.11

Second Stage 9 hardening pass.

- Restored Rust 2021-compatible exported-symbol attributes.
- Tightened EXT4 descriptor and inode handling.
- Made tar parsing stricter.
- Added allocator overflow and double-free checks.
- Prevented VMM permission promotion through existing page-table entries.
- Locked shared log, keyboard, and devfs PRNG state.
- Tightened scheduler wait semantics.
- Validated ELF entry placement.
- Added IST stacks for critical exceptions.

## 0.0.1.10

Stage 9 hardening and security pass.

- Added overflow checks across ramfs, tarfs, EXT4, MBR, block I/O, syscall buffers, VMM mapping, and heap operations.
- Fixed ATA PIO LBA48 handling for 256-sector requests.
- Rejected duplicate VMM mappings and overlapping user ELF W+X pages.
- Fixed scheduler terminal-slot reuse after process history publication.
- Routed Rust panic and bounds failures into kernel panic diagnostics.

## 0.0.1.9

Stage 9 process image replacement.

- Added `exec` and `execv` syscall IDs.
- Added Rust-side syscall validation and dispatch for `exec` and `execv`.
- Added a safe prepare-then-commit replacement path for process images.
- Added `/bin/execcheck`.
- Kept PID identity across successful exec.
- Made failed exec non-destructive.

## 0.0.1.8

Stage 8 scheduler return-path hardening.

- Fixed heap coalescing for non-contiguous heap extension blocks.
- Made async process context publication resilient to a lost `current_async_slot`.
- Hardened scheduler, preemption, process wait, and post-ktest idle paths.

## 0.0.1.3 to 0.0.1.6

Stage 8 preemptive scheduling and runtime stabilization.

- Added persistent READY, RUNNING, SLEEPING, and WAITING process states.
- Captured and restored ring3 interrupt frames.
- Wired PIT IRQ0 into single-core preemption.
- Added async `spawn`, blocking `wait`, `sleep`, `yield`, and deep-copy `fork`.
- Fixed ring3 `SYS_EXIT`, shell idle interrupt handling, and wait lifecycle issues.

## 0.0.1.1 to 0.0.1.2

Process, fd, and argv groundwork.

- Replaced stage-number versioning with `major.minor.patch.fix`.
- Added fd-oriented syscalls such as `dup`, `tell`, `fstat`, `fdinfo`, and `readdir`.
- Added argv-aware process spawning through the Rust syscall boundary.

## Earlier stage work

- Repaired BIOS boot memory layout issues around E820 and bootstrap page tables.
- Added stage1/stage2 COM1 diagnostics.
- Added linker checks for boot-sector and stage2 size limits.
- Built the VFS, ramfs, devfs, tarfs, read-only EXT4 path, ATA PIO, MBR parsing, and installer-generated seed filesystem.
- Added the first ring3 userland path with GDT/TSS setup, user selectors, syscall gate, ELF loading, and user stack mapping.
- Linked Rust boundary modules into the kernel for high-risk parsing and syscall edges.
