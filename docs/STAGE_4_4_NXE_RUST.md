# AuroraOS Stage 4.4: NXE user stack fix and safety-boundary hardening

## Runtime fix

VMware showed all C user programs faulting at `entry + 3`, exactly on the first `call main` instruction. The fault address was just below `USER_STACK_TOP`, which means the CPU faulted while `call` tried to push a return address onto the user stack.

The user stack pages were intentionally mapped with `VMM_NX`. Stage2 enabled EFER.LME but did not enable EFER.NXE. On x86_64, if NXE is not enabled, bit 63 in a PTE is reserved. Any access through a PTE with bit 63 set can raise a page fault with the reserved-bit condition. Assembly-only `/bin/regtrash` did not touch the stack before `int 0x80`, so it survived while C binaries crashed.

Stage2 now enables both:

- EFER.LME, bit 8
- EFER.NXE, bit 11

`ktest` now checks `cpu_nxe_enabled()` before exercising ring3 C binaries.

## VFS hardening

VFS mount selection was rewritten around an explicit `vfs_route_t` result. Call sites now receive either a valid route or a status code, instead of manually juggling nullable mount pointers and relative-path buffers.

The backend filesystem modules remain C ABI modules in this stage.

## Rust safety modules

This environment does not contain `rustc`, so the bootable image remains linked from C/C++/asm objects. Stage 4.4 adds no_std Rust source modules for the two safety boundaries requested:

- `kernel/rust/syscall_dispatch.rs`
- `kernel/rust/vfs_route.rs`

They are checked by `make test` for the expected no_std/type-safe structure. Linking them into the kernel will be enabled once a Rust freestanding toolchain is available in the build environment.
