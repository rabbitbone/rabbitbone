# AuroraOS 0.0.1.4

AuroraOS 0.0.1.4 is a Stage 8 hardening release. It keeps the 0.0.1.3 single-core preemptive process-control model and fixes the issues found during VMware ktest runs and code review.

## Fixed

- Kernel heap growth now reserves a real contiguous run before creating a multi-page heap block. It no longer converts non-contiguous pages into one large free block.
- Kernel heap pages are allocated below the current 1 GiB identity-map/direct-pointer limit until the kernel grows a full HHDM/direct map.
- Physical memory allocator exposes contiguous-run allocation and matching free helpers.
- `timer_sleep_ticks()` enables interrupts before `hlt`, preventing a dead halt when a prior test path left IF cleared.
- `process_exec()` now revalidates terminal root process state before returning, which makes the synchronous wrapper robust against scheduler-loop exit edge cases.
- Userspace-visible `procinfo` and `schedinfo` layouts are centralized in `include/aurora/abi.h`; kernel and userlib now alias the same ABI structs.
- Added ABI static asserts for shared process, scheduler and preemption structs.
- Added `rust-toolchain.toml` and documented Rust as a mandatory build dependency. The existing uploaded-toolchain build script remains the reproducible path for environments without system Rust.

## ABI

- Version text: `0.0.1.4`
- Syscall ABI: `0x00000104`

## Stage status

Stage 8 remains closed as single-core preemptive scheduler/process-control baseline. The next large work items are still SMP/APIC, COW fork, `execve`, writable EXT4/journal and later GUI.
