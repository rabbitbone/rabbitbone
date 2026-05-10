# AuroraOS 0.0.1.12

AuroraOS 0.0.1.12 is a Rust boundary reproducibility fix over the Stage 9 hardening baseline.

## Fixed

- Removed the dependency on hashed internal `core::panicking::panic_bounds_check::*` symbols.
- Rewrote Rust VFS route and path-policy boundary code so checked indexing is not emitted in the freestanding kernel object.
- Removed the old version-specific `export_name` panic-bounds workaround.
- Kept Rust source compatible with normal Rust 2021 syntax, including older stable toolchains that do not accept edition-2024 unsafe attributes.

## Regression checks

- Added `scripts/check_rust_no_panic_bounds.py`.
- The Makefile now fails `make test` if `kernel/rust/lib.o` has an unresolved dependency on Rust `core::panicking::*` internals.

## Version

- Version text: `0.0.1.12`
- Syscall ABI: `0x0000010c`
