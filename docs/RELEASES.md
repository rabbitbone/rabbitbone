# AuroraOS release notes

This file keeps the release history short enough to be useful. Older one-off stage notes were folded into this summary.


## 0.0.1.40

Stage19.17 syscall validator recovery and ABI boundary hardening.

- Bumped the kernel version and syscall ABI to `0.0.1.40` / `0x00000128`.
- Fixed the Rust syscall decoder self-test after the `sync`/`fsync`/`statvfs` ABI expansion by checking `AURORA_SYS_MAX` instead of the old `sync` syscall number as the unsupported boundary.
- Added explicit Rust validator contracts for `sync`, `fsync`, and `statvfs`, keeping C and Rust syscall validation aligned.
- Extended the Rust ABI generator to export `AURORA_SYS_MAX`, preventing future decoder boundary tests from drifting when syscall IDs are added.
- Reduced the diagnostic log ring from 40 to 32 lines to keep the freestanding kernel below the early-stack safety boundary.

## 0.0.1.39

Stage19.16 EXT4 app-storage syscall contract update.

- Bumped the kernel version and syscall ABI to `0.0.1.39` / `0x00000127`.
- Added VFS-level `sync`, descriptor-backed `fsync`, and `statvfs` plumbing for persistent application storage.
- Added EXT4 `statvfs` capacity reporting with persistent/journaled/repairable mount flags, free block/inode counters, block size, max name length, mount path, and filesystem name.
- Added userland wrappers `au_sync`, `au_fsync`, and `au_statvfs`, and wired the Rust syscall dispatcher/name table for the new stable filesystem ABI.
- Extended runtime contracts for EXT4 capacity reporting, writeback flush boundaries, and syscall validation for the new FS calls.
- Reduced the in-kernel log ring from 48 to 40 lines to preserve the early boot stack layout after adding the storage ABI.

## 0.0.1.38

Stage19.15 EXT4 repair-lite and fault-injection contracts.

- Bumped the kernel version and syscall ABI to `0.0.1.38` / `0x00000126`.
- Added `ext4_repair_metadata()` for fsck-lite recovery of free block/inode counters, metadata checksums, and Aurora htree indexes.
- Added htree index consistency checking and rebuild logic that scans directory records, drops a corrupt index block, rebuilds a sorted persistent index, and preserves lookups.
- Added repair telemetry to fsck reports, EXT4 performance counters, and the `ext4` shell command.
- Added runtime fault-injection contracts for corrupted htree metadata and corrupted free counters, followed by repair and post-repair validation.

## 0.0.1.37

Stage19.14 EXT4 unwritten-extent preallocation and conversion.

- Bumped the kernel version and syscall ABI to `0.0.1.37` / `0x00000125`.
- Added EXT4 unwritten extent support in inline, depth-1, and depth-2 extent trees.
- Added `ext4_preallocate_file_path()` for zero-readable preallocation without eager data writes.
- Added safe conversion of an unwritten block to initialized data on first write, including split/merge of neighboring extent records.
- Kept security semantics for partial writes by zero-filling converted blocks in the data cache before overlaying user payload, avoiding stale disk-data exposure.
- Extended fsck/extent inspection and shell perf counters with unwritten extent/block and conversion telemetry.
- Added runtime coverage for preallocate-read-zero, write conversion, no stale data leakage, truncate, unlink, and metadata consistency.

## 0.0.1.34

Stage 19.11 EXT4 cache-coherency and runtime correctness recovery.

- Bumped the kernel version and syscall ABI to `0.0.1.34` / `0x00000122`.
- Fixed the stage19.10 runtime failures where dirty metadata-cache blocks could survive after a metadata block was freed and then reused as file data, causing sparse and indexed extent readback to return stale metadata instead of file payload.
- Added metadata-cache invalidation on direct data writes and block free, while preserving async metadata write-combining for performance.
- Fixed htree index offsets for entries inserted by splitting an occupied directory record; the index now points to the newly-created record instead of the previous record.
- Strengthened htree validation so fsck-like checks verify that every htree entry resolves to the exact referenced directory record, inode, file type, and name hash.
- Added mount-time free-counter reconciliation so a second mount cannot overwrite correct cached journal-reserved free counts with stale superblock counts read through the raw boot path.
- Extended EXT4 perf reporting with metadata-cache invalidation counters.

## 0.0.1.30

Stage 19.7 EXT4 extent-demotion correctness update.

- Bumped the kernel version and syscall ABI to `0.0.1.30` / `0x0000011e`.
- Fixed indexed-extent demotion after truncate so dirty leaf buffers are persisted before leaf metadata is collapsed into inline extents.
- Generalized demotion to collapse any depth-1 extent tree whose remaining extents fit in the inode inline root, including trees that previously had multiple split leaves.
- Ensured truncate calls the demotion pass after tail block removal so sparse multi-leaf files shrink back to inline form and release leaf metadata blocks.
- Preserved metadata counters and allocation bitmaps across truncate, unlink, and fsck-like EXT4 validation after mutation stress.

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
