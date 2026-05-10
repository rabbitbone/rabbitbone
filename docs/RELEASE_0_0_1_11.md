# AuroraOS 0.0.1.11

AuroraOS 0.0.1.11 is a second Stage 9 hardening release. It keeps the verified `exec/execv` baseline from 0.0.1.9 and the parser/overflow security work from 0.0.1.10, then fixes the remaining review items that were still open or only partially addressed.

## Rust build compatibility

- Reverted Rust exported-symbol attributes to edition-2021 compatible syntax:
  - `#[no_mangle]`
  - `#[export_name = "..."]`
- Removed edition-2024-only `#[unsafe(...)]` attributes from no_std Rust boundary modules.
- Restored normal `extern "C"` block syntax for the current pinned toolchain path.
- Added a freestanding custom target spec at `kernel/rust/x86_64-aurora-kernel.json` for the next build-std migration step. It is not the default build target yet because the current reproducible script builds with the pinned uploaded toolchain object path.

## Filesystem and parser hardening

- EXT4 group descriptor reads now use the actual on-disk descriptor size and zero-fill the in-memory descriptor before reading.
- EXT4 VFS now explicitly accepts only regular files and directories; unsupported inode types return `VFS_ERR_UNSUPPORTED`.
- EXT4 VFS `stat` now endian-converts `i_mode` consistently.
- Tar octal fields now reject non-octal garbage instead of silently skipping it.
- Tar entries now accept only regular files and directories.
- Tar entry names containing `..` are rejected before normalization.

## Memory and paging hardening

- `kmalloc` and heap extension arithmetic now reject oversized/overflowing allocations.
- `memory_free_contiguous_pages` now detects double-free attempts and panics instead of corrupting the allocator bitmap.
- Existing page-table entries are no longer permission-promoted by `next_table` when a subtree already exists.
- User copy and syscall pointer arithmetic received additional overflow checks.

## Kernel diagnostics and concurrency hardening

- `printf` signed formatting now handles `INT64_MIN` without signed overflow.
- `kvsnprintf` clamps huge logical lengths to `INT_MAX`.
- Kernel log writes are guarded by an IRQ-save spinlock.
- Keyboard IRQ/reader queue access is guarded by an IRQ-save spinlock.
- `/dev/random` PRNG state updates are locked. The device remains documented as deterministic non-crypto PRNG.
- `/dev/kmsg` writes now enter the kernel log instead of being silently discarded.

## Scheduler and process hardening

- Kernel task PID allocation now avoids zero and active PID collisions.
- Scheduler init no longer destroys live jobs on accidental repeat initialization.
- Scheduler wait for a job no longer drains unrelated queued jobs.
- Scheduler counters use saturating increments.
- Sleep wake tick calculation is saturating.
- ELF validation rejects executable images without program headers.
- Process image loading rejects an entry point outside executable PT_LOAD segments.

## CPU exception hardening

- TSS now reserves IST stacks for critical exceptions.
- IDT routes NMI, double fault, page fault and machine check through dedicated IST entries.

## Regression coverage

- EXT4 32-byte group descriptor read semantics.
- Tar strict octal parsing, unsupported typeflag rejection, and parent-reference rejection.
- `kmalloc` huge-size overflow rejection.
- `printf` `INT64_MIN` formatting.
- Previously added coverage for ramfs huge offsets, block overflow, VFS exact mount roots, scheduler slot reuse and corrupted EXT4/tarfs images remains active.

## Version

- Version text: `0.0.1.11`
- Syscall ABI: `0x0000010b`
