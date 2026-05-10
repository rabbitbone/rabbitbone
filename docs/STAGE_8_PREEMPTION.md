# Stage 8 preemption and process-control baseline

## Status

Stage 8 is complete for AuroraOS 0.0.1.8 as a single-core preemptive scheduler and process-control baseline. The kernel now keeps persistent runnable user contexts, switches ring3 interrupt frames on PIT quantum expiry, supports asynchronous child creation, blocks waiters and sleepers, and exposes the state through stable syscall/userlib contracts.

## Completed in 0.0.1.8

- Replaced the single-active-process executor with a persistent async process slot table.
- Added READY, RUNNING, SLEEPING and WAITING live states alongside EXITED, FAULTED and LOADERR terminal states.
- Added full user interrupt-frame save/restore and `arch_user_resume()` for IRQ/syscall return into a selected process context.
- Wired PIT IRQ0 through `scheduler_tick()` and preempts ring3 execution when the configured quantum expires.
- Added round-robin ready-slot selection and wakeup paths for sleeping and waiting processes.
- Converted `spawn` and `spawnv` to asynchronous runnable process creation while preserving validation through Rust.
- Converted `wait` to a blocking process primitive in the async scheduler.
- Converted `sleep` and `yield` to scheduler requests instead of accounting-only calls.
- Added `fork` syscall through Rust validation and implemented it as a deep-copy address-space clone with duplicated fd-handle snapshot state.
- Added per-process fd-handle snapshots so open descriptors, offsets and directory handles survive context switches.
- Added userland wrappers and checks for `fork`, async `spawn/spawnv`, blocking `wait`, and preemption counters.
- Added `/bin/procctl` and `/bin/forkcheck`; extended `/bin/spawncheck`; embedded all Stage 8 user programs in the boot image.
- Bumped the ABI to `0x00000108` and version text to `0.0.1.8`.

## Verification

- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test` passes.
- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image` builds `build/aurora.img`.
- Boot source, stage2 layout, kernel layout, Rust symbol, CPU feature, user entry, userlib, process/fd, version and host ktest checks pass.

## Known limits after Stage 8

- Scheduling is still single-core and uses one global run queue/process slot table.
- `fork` is a deep-copy clone, not copy-on-write.
- Stage 9 adds `exec/execv`; Stage 8 itself only created new images through `spawn/spawnv` or `fork`.
- EXT4 remains read-only in kernel.
- SMP, APIC timer, PCI/AHCI/NVMe and GUI are outside this stage.
