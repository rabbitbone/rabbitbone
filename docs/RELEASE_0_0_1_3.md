# AuroraOS 0.0.1.3

AuroraOS 0.0.1.3 closes Stage 8 as a single-core preemptive scheduler/process-control baseline. The release moves beyond telemetry-only accounting and keeps persistent ring3 process contexts that can be preempted by IRQ0, resumed through `iretq`, blocked on wait/sleep, and cloned through `fork`.

## Kernel and scheduler

- Added persistent async process slots with READY, RUNNING, SLEEPING and WAITING states.
- Added full interrupt-frame capture and restore for ring3 process contexts.
- Added `arch_user_resume()` for returning to saved user frames after scheduler selection.
- Wired PIT quantum expiry to process preemption through `scheduler_tick()`.
- Added wakeup handling for sleepers and waiters.
- Added async run-until-idle execution for kernel-launched user checks.

## Process-control syscalls

- `spawn` and `spawnv` now create runnable child processes when called from the async scheduler.
- `wait` blocks the caller until the target process reaches a terminal state.
- `sleep` blocks the caller until the requested tick deadline.
- `yield` requests immediate rescheduling.
- Added Rust-dispatched `fork` syscall as syscall number 29.
- Syscall ABI version is now `0x00000103`.

## State preservation

- Added per-process fd-handle snapshots for context switches.
- Deep-copy `fork` clones user memory mappings and fd-handle state.
- Process metadata is available through `procinfo`, `lastproc` and scheduler diagnostics after terminal transitions.

## Userland and tests

- Added `/bin/procctl` for async `spawn/spawnv/wait` coverage.
- Added `/bin/forkcheck` for parent/child fork/wait validation.
- Extended `/bin/spawncheck` to validate the asynchronous process path.
- Extended `ktest`, process contract scripts and version checks for the new ABI and Stage 8 programs.

## Build verification

```sh
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image
```

Both commands complete in the development environment. The generated raw disk image remains `build/aurora.img`.

## Limits

This is still a VMware Legacy BIOS single-core baseline. `fork` is deep-copy rather than copy-on-write, EXT4 write/journal support is not implemented, and SMP/APIC/PCI/AHCI/NVMe/GUI are later stages.
