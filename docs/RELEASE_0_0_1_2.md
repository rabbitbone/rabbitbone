# AuroraOS 0.0.1.2

AuroraOS 0.0.1.2 extends the 0.0.1.1 FD/process/scheduler baseline with the first preemption contract layer and argv-aware user process spawning through the Rust syscall boundary.

## Completed

- Version contract bumped to `0.0.1.2` with syscall ABI `0x00000102`.
- Added `AURORA_SYS_SPAWNV` / `AU_SYS_SPAWNV` for path plus argv vector process creation from ring3.
- Added `AURORA_SYS_PREEMPTINFO` / `AU_SYS_PREEMPTINFO` for explicit PIT/timeslice/preemption telemetry.
- Extended Rust syscall dispatcher with typed decoding and validation for `spawnv` and `preemptinfo`.
- Added kernel-side argv vector copying with per-string usercopy hardening and argc bounds.
- Added PIT-driven scheduler tick accounting:
  - global timer ticks observed by scheduler,
  - user/kernel tick split,
  - current PID and current slice ticks,
  - fixed default quantum,
  - expiration counter and last expiration RIP.
- Wired IRQ0 dispatch into `scheduler_tick()` after `pit_irq()`.
- Extended `sched_stats_t` and userland `au_schedinfo_t` with preemption counters.
- Added userland `/bin/preemptcheck` to validate preemption telemetry and null-pointer rejection.
- Extended `/bin/spawncheck` to verify `spawnv` rejection while another user process is active and invalid argument handling.
- Extended shell with `preempt`, retaining `spawn PATH [ARGS]`, `qspawn`, `runq`, `sched`, `fdprobe`.
- Embedded `/bin/preemptcheck` into the kernel userland image.
- Extended `ktest` coverage for:
  - `/bin/preemptcheck`,
  - Rust-dispatched `spawnv` with argv,
  - `preemptinfo`,
  - scheduler tick accounting,
  - syscall number/name contract.
- Updated build helper for uploaded Rust distributions by exporting the Rust shared-library directory through `LD_LIBRARY_PATH`.

## Current behavior

This release records and exposes preemption/timeslice information, but it does not yet context-switch ring3 processes asynchronously. User processes are still executed through the current single-active-process model. The new telemetry and ABI are the contract base for the next step: saving/restoring runnable user contexts and returning from IRQ0 into the selected process.

## Verified

Commands run in this environment:

```sh
make clean
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain userentrycheck rustsafetycheck cpufeaturecheck userlibcontractcheck processcontractcheck versioncontractcheck rustsymbolscheck kernellayoutcheck layoutcheck
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain bootcheck
```

Successful outputs included:

- `all tests passed`
- `user entry register preservation checks passed`
- `rust safety module integration checks passed`
- `cpu feature setup checks passed`
- `userlib error contract checks passed`
- `process syscall/registry/fd contract checks passed`
- `version contract checks passed`
- `rust linked symbol checks passed`
- `kernel layout checks passed: kernel=0x10000..0x83000 bytes=471040 bss=136352 early_stack_margin=110592 max_symbol=col@0x82498`
- `stage2 layout checks passed`
- `boot source address checks passed`
- `created build/aurora.img size=64MiB partition_lba=2048 ext4_seed_blocks=8192`

## Not complete

- No full IRQ-driven context switch yet.
- No fork/COW yet.
- EXT4 journal/write path remains future work.
- SMP/APIC remains future work.
- No VMware runtime boot was executed here because no VMware/QEMU binary is available in this environment.
