# AuroraOS 0.0.1.9

AuroraOS 0.0.1.9 is the Stage 9 exec/process-image release. It builds on the verified Stage 8 single-core preemptive scheduler and adds a real userspace image replacement path without changing PID identity.

## Added

- Added `exec` and `execv` syscall IDs through the shared ABI header.
- Added Rust-dispatched validation and syscall names for `exec` and `execv`.
- Added C syscall backends `aurora_sys_exec()` and `aurora_sys_execv()`.
- Added userlib wrappers `au_exec()` and `au_execv()`.
- Added `/bin/execcheck` to verify failed exec preservation and successful `execv` replacement.
- Added scheduler/process support for preparing a replacement address space before committing it.
- `exec/execv` preserve the current PID and parent metadata, replace the user image, rebuild the user stack with argv, and retain fd-handle snapshot state across the image switch.
- Failed `exec/execv` returns an error to the old image without destroying the running process.
- Successful `exec/execv` commits on the syscall return path and resumes at the new ELF entry instead of returning to the old program.

## Hardening

- Pending exec replacements are discarded with mapping and user-space destruction on cleanup paths.
- Added host contract checks for the new syscall ABI range and for keeping the user resume trampoline as returning code.
- Kernel early-stack reservation was adjusted to keep the larger Stage 9 image inside the checked low-memory boot layout.

## ABI

- Version text: `0.0.1.9`
- Syscall ABI: `0x00000109`
- New syscall IDs: `exec=30`, `execv=31`, `AURORA_SYS_MAX=32`

## Verification

- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test` passes.
- `scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image` builds `build/aurora.img`.
- The Stage 9 ktest path runs `/bin/execcheck`, which first verifies that a failed exec keeps the original image alive, then `execv`s into `/bin/fscheck`; the final process result must report the same PID slot completed as `/bin/fscheck` with exit code 0.

## Remaining after Stage 9

- `fork` is still deep-copy, not copy-on-write.
- No `execve` environment vector yet.
- No shebang/interpreter support.
- Scheduler remains single-core.
- EXT4 remains read-only in kernel.
- SMP, APIC timer, PCI/AHCI/NVMe and GUI remain future stages.
