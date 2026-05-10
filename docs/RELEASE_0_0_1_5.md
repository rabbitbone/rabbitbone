# AuroraOS 0.0.1.5

AuroraOS 0.0.1.5 is a Stage 8 runtime hardening release focused on the VMware ktest failures seen after 0.0.1.4.

## Fixed

- `SYS_EXIT` is now intercepted at the `int80` trap boundary for ring3 processes and completed as a non-returning scheduler transition. User mode can no longer fall through into a `hlt` loop after a returned `main()`.
- User CRT and `au_exit()` no longer use `hlt` as a fallback. If an exit path ever returns unexpectedly, they retry `SYS_EXIT` and use `pause`, avoiding CPU-off behaviour.
- Blocking `wait` now preserves the syscall number when rewinding RIP to reissue `int80`. 0.0.1.4 rewound RIP after `rax` had already been overwritten with the provisional return value, so a resumed waiter could call `version` instead of `wait`.
- The TSS ring0 stack reservation was tightened from 64 KiB to 48 KiB to keep the low-memory kernel image clear of the early boot stack while retaining a large interrupt stack for the current single-core design.

## Contracts

- Version text: `0.0.1.5`
- Syscall ABI: `0x00000105`
- Stage: Stage 8 single-core preemptive scheduler/process-control baseline, hardened for exit/wait runtime correctness.

## Verification

Expected clean commands:

```sh
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image
```
