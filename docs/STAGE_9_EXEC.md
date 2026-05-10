# Stage 9 exec and process image replacement

## Status

Stage 9 is complete for AuroraOS 0.0.1.9 as a process-image replacement baseline. A live ring3 process can request a new ELF image, the kernel prepares the replacement address space off to the side, and only commits it once loading and user-stack construction have succeeded.

## Completed

- Added shared ABI syscall numbers for `exec` and `execv`.
- Added Rust syscall decoding, validation, name-table coverage and dispatch for `exec` and `execv`.
- Added kernel backend validation for path and argv vectors crossing the user/kernel boundary.
- Added a replacement-image prepare/commit path in the process subsystem.
- Preserved PID, parent PID and fd-handle snapshot across successful exec.
- Rebuilt argv on the new user stack and reset the interrupt frame to the new ELF entry point.
- Kept failed exec non-destructive: the old program receives an error and continues running.
- Added userlib wrappers `au_exec()` and `au_execv()`.
- Added `/bin/execcheck` and integrated it into the full ktest userland suite.
- Updated host-side process/syscall/version contract checks.

## Verification

- Host/build contract checks pass.
- `ktest` includes the Stage 9 exec path through `/bin/execcheck`.
- `build/aurora.img` is produced by the pinned Rust-toolchain build path.

## Known limits

- No environment-vector ABI yet.
- No close-on-exec descriptor flag yet.
- No interpreter/shebang path yet.
- No copy-on-write fork yet.
