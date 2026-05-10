# AuroraOS stage status

## Stage 1.2 status

Current stage is a bootable amd64 VMware-oriented kernel baseline with a repaired BIOS boot path through the long-mode transition. Stage 1.1 fixed bad GAS Intel-syntax symbol addresses in the INT13 path. Stage 1.2 fixes a real triple fault caused by BIOS E820 overwriting the bootstrap PML4.

### Completed in this iteration

- Fixed stage2 E820/PML4 overlap: E820 buffer moved from `0x9000` to `0x5000`, while bootstrap page tables remain at `0x9000..0xBfff`.
- Fixed stage2 bootinfo ABI version to `1`, matching the kernel-side validator.
- Added post-long-mode serial checkpoints: `long mode active` and `jumping kernel`.
- Added raw COM1 breadcrumbs at `kernel_start` before BSS clearing and before `kernel_main`.
- Added `scripts/check_stage2_layout.py` and wired it into `make test` to prevent boot memory-map overlaps.

- Fixed stage1/stage2 symbol-address loads: every DAP/string pointer now uses `offset` instead of accidental memory loads.
- Added early COM1 diagnostics in stage1 and stage2, before kernel serial initialization.
- Added linker assertions so stage1 cannot overlap the MBR partition table and stage2 cannot exceed its 32 KiB real-mode load window.
- Added boot source regression check to catch the exact `mov si, symbol` class of GAS Intel syntax bug.
- BIOS stage1 + stage2 boot kernel from fixed boot sectors and enter amd64 long mode.
- Kernel now replaces loader page tables with its own VMM-managed identity map.
- Physical frame allocator remains E820-backed.
- Kernel heap added with first-fit splitting/coalescing allocator, calloc/realloc/free, stats and self-test.
- VFS layer added with mount table, path normalization, stat/read/write/list/mkdir/create/unlink API.
- Writable boot ramfs mounted at `/`.
- devfs mounted at `/dev` with `null`, `zero`, `random`, `kmsg`.
- EXT4 adapter added to mount first Linux MBR partition at `/disk0` when present.
- Installer now writes a minimal EXT4-compatible seed filesystem into the MBR partition.
- EXT4 driver can resolve paths, find directory entries and read files through the VFS adapter.
- Kernel syscall dispatcher added for version, console write, open, close, read, stat, list, log and exit.
- ELF64 parser/loader added: validates amd64 ELF64 and loads PT_LOAD segments into kernel memory without execution.
- CLI expanded with `heap`, `vmm`, `mounts`, `ls`, `stat`, `cat`, `write`, `mkdir`, `touch`, `rm`, `syscall`, `elf`, `ktest`.
- Host tests expanded for path utilities and heap allocator.

### Verified here

- `make` completes.
- `make test` completes, including boot source address checks and stage2 layout overlap checks.
- The generated raw disk image contains an MBR Linux partition.
- The installer-created EXT4 seed filesystem was verified with the project EXT4 reader from a host-side check.

### Not complete yet

- No VMware runtime boot test was possible in this environment; no VMware or QEMU binary is available here.
- EXT4 remains read-only in kernel; installer generates the seed FS, kernel does not write EXT4.
- No ring3 execution yet.
- ELF loader prepares images but does not map them into a process address space or transfer control.
- No scheduler/process table yet.
- No PCI/AHCI/NVMe/USB/ACPI/APIC/SMP.
- GUI is intentionally not started.

## Next hard stage

- Add process model, address spaces, syscall CPU entry path and user-mode transition.
- Add ELF execution from `/disk0` or ramfs.
- Move CLI into first userland program once ring3 works.

## Stage 8 status

AuroraOS 0.0.1.8 closes Stage 8 as a single-core preemptive scheduler/process-control baseline. The scheduler now owns persistent user contexts, PIT IRQ0 can preempt ring3 tasks on quantum expiry, and process syscalls can create, block, wake, clone and reap children under the async run loop.

### Completed in this iteration

- ABI bumped to `0x00000108` and version text to `0.0.1.8`.
- Added persistent async process slots with READY, RUNNING, SLEEPING and WAITING states.
- Added interrupt-frame based user context save/restore and `arch_user_resume()`.
- Wired IRQ0 quantum expiry into ring3 preemption and round-robin ready selection.
- Converted `spawn` and `spawnv` to asynchronous runnable process creation inside the scheduler.
- Converted `wait` to a blocking scheduler primitive.
- Converted `sleep` and `yield` into scheduler requests.
- Added Rust-dispatched `fork` syscall with deep-copy address-space cloning.
- Added per-process fd-handle snapshots across context switches and fork.
- Added `/bin/procctl` and `/bin/forkcheck`; extended `/bin/spawncheck`.
- Increased kernel payload reservation to 1016 sectors and moved the early kernel stack top to keep layout margins valid.

### Verified here

- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test` completes.
- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image` completes.
- Boot source, stage2 layout, kernel layout, Rust symbol, CPU feature, user entry, userlib, process/fd, version and host ktest checks pass.

### Remaining after Stage 8

- No VMware/QEMU runtime boot test was possible in this environment.
- Scheduler remains single-core.
- `fork` is deep-copy, not copy-on-write.
- EXT4 is still read-only from the kernel side; write/journal support remains a later stage.
- No PCI/AHCI/NVMe/APIC/SMP/GUI yet.


## Stage 9 status

AuroraOS 0.0.1.12 keeps the real `exec/execv` process-image replacement baseline on top of the verified Stage 8 preemptive scheduler. Successful exec keeps the current PID, replaces the user address space, rebuilds argv on the new stack and resumes at the new ELF entry. Failed exec is non-destructive and returns an error to the original image.

### Completed in this iteration

- ABI bumped to `0x0000010c` and version text to `0.0.1.12`.
- Added shared syscall IDs `exec=30` and `execv=31`.
- Added Rust syscall validation/dispatch and syscall-name contract coverage.
- Added C syscall backend and userlib wrappers.
- Added prepared replacement image commit path in the process subsystem.
- Added `/bin/execcheck` and ktest coverage for failed exec preservation and successful execv replacement into `/bin/fscheck`.

### Remaining after Stage 9

- No environment-vector ABI yet.
- No close-on-exec descriptor flag yet.
- No interpreter/shebang support.
- `fork` remains deep-copy rather than copy-on-write.
- EXT4 write/journal, SMP/APIC, PCI/AHCI/NVMe and GUI remain later stages.

## Stage 9 hardening status

AuroraOS 0.0.1.12 is a hardening pass over the Stage 9 baseline. It closes overflow and corrupted-input bugs in ramfs, tarfs, EXT4, MBR, block I/O, syscall user-buffer arithmetic, VMM mapping, heap shrink/coalescing and scheduler terminal-slot reuse.

### Completed in this iteration

- Added checked ramfs maximum-size and capacity-growth handling.
- Added overflow-safe block, MBR and E820 arithmetic.
- Hardened tarfs and EXT4 parsers against malformed on-disk structures.
- Rejected duplicate VMM mappings and overlapping user ELF W+X pages.
- Reused scheduler terminal slots after history publication.
- Routed Rust panic/bounds failures to kernel panic diagnostics.
- Added regression tests for the above bug classes.

### Remaining after Stage 9 hardening

- Full kernel text/rodata/data page-permission split remains future work.
- `/dev/random` is documented as a deterministic non-crypto test PRNG until a real entropy pool exists.
- Stack protector/canary support remains future work pending TLS/canary runtime setup.

## Stage 9.2 hardening follow-up - 0.0.1.12

AuroraOS 0.0.1.12 closes the remaining hardening review items after the 0.0.1.10 security pass. It restores Rust edition-2021 compatibility for exported symbols, hardens EXT4 descriptor and inode handling, makes tar parsing strict, adds allocator overflow and double-free detection, prevents VMM permission promotion through existing page-table entries, locks log/keyboard/devfs PRNG shared state, tightens scheduler wait semantics, validates ELF entry placement, and adds IST stacks for critical exceptions.

KTest and host tests cover the new regressions together with the earlier Stage 9 hardening checks.
