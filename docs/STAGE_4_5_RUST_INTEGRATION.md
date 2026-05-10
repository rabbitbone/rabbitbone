# AuroraOS Stage 4.5 Rust integration

Stage 4.5 turns Rust from checked source files into linked kernel code.

## Rust build path

The kernel now links `kernel/rust/lib.rs` into `kernel.elf` as a freestanding Rust object:

- `#![no_std]`
- `panic=abort`
- `relocation-model=static`
- `code-model=large`
- `no-redzone=yes`
- no allocator
- C ABI only at the boundary

With the uploaded Rust archive extracted beside the tree, build like this:

```sh
make clean
make test \
  RUSTC=/path/to/rust/rustc/bin/rustc \
  RUST_SYSROOT=/path/to/rust/rust-std-x86_64-unknown-linux-gnu
make image \
  RUSTC=/path/to/rust/rustc/bin/rustc \
  RUST_SYSROOT=/path/to/rust/rust-std-x86_64-unknown-linux-gnu
```

A normal system Rust install also works if it has the `x86_64-unknown-linux-gnu` standard library available:

```sh
make clean
make test
make image
```

## Rust-owned kernel boundaries

### Syscall dispatch

`kernel/rust/syscall_dispatch.rs` is now the primary syscall dispatcher.

C still owns the backend syscall implementations because they call existing kernel subsystems, but the hostile user/kernel syscall number boundary is decoded in Rust:

- typed `SyscallNo`
- explicit `match` by syscall number
- size/null prevalidation for hostile user buffers
- unknown syscalls return `VFS_ERR_UNSUPPORTED`
- self-test exported as `aurora_rust_syscall_selftest()`

C entry `syscall_dispatch(...)` now builds a C ABI `aurora_rust_sysargs_t` and calls `aurora_rust_syscall_dispatch(...)`.

### VFS route selection

`kernel/rust/vfs_route.rs` now owns path normalization and longest-prefix mount routing.

C VFS still owns mount storage and backend FS operations, but `resolve_route(...)` now passes a compact `MountView[16]` to Rust and receives:

- normalized path
- selected mount index
- relative path under the mount
- explicit status code

Self-test is exported as `aurora_rust_vfs_route_selftest()` and is part of `ktest`.

## Stage 4.3 fault fix status

The Stage 4.3 user stack fault was addressed in Stage 4.4 by enabling `EFER.NXE` in stage2. Stage 4.5 keeps that fix and adds a boot-source regression check.

## ktest additions

`ktest` now includes:

- Rust VFS route self-test
- Rust syscall decoder/validator self-test
- Rust-dispatched syscall filesystem test

Expected final line:

```text
KTEST_STATUS: PASS
```
