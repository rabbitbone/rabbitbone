# AuroraOS 0.0.1.1

Kernel release `0.0.1.1` keeps the base version on `0.0.1` and uses the fourth component as the fix/build number.

Changes:

- Replaced stage-number kernel versioning with `major.minor.patch.fix`.
- Added `dup`, `tell`, `fstat`, `fdinfo`, and `readdir` syscalls.
- Allowed directory handles through `open`; `read` and `write` reject directory handles.
- Added `/bin/fdcheck` to validate descriptor semantics from ring3.
- Extended Rust syscall decoder and validator for descriptor syscalls.
- Extended `ktest` coverage for descriptor metadata, directory iteration and userland fd contracts.
