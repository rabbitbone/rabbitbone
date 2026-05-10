#!/usr/bin/env python3
from pathlib import Path
checks = {
    Path('kernel/rust/lib.rs'): ['#![no_std]', 'pub mod syscall_dispatch', 'pub mod vfs_route', 'pub mod usercopy', 'pub mod path_policy'],
    Path('kernel/rust/syscall_dispatch.rs'): ['enum SyscallNo', 'fn validate_args', 'valid_handle', 'aurora_rust_syscall_dispatch', 'aurora_rust_syscall_selftest'],
    Path('kernel/rust/vfs_route.rs'): ['Option<', 'aurora_rust_vfs_route', 'aurora_rust_vfs_route_selftest', 'normalize'],
    Path('kernel/rust/usercopy.rs'): ['USER_LIMIT', 'checked_add', 'aurora_rust_user_range_check', 'aurora_rust_user_copy_step', 'aurora_rust_usercopy_selftest'],
    Path('kernel/rust/path_policy.rs'): ['validate_path', 'NotAbsolute', 'ComponentTooLong', 'aurora_rust_path_policy_check', 'aurora_rust_path_policy_selftest'],
    Path('kernel/proc/process.c'): ['aurora_rust_user_range_check', 'aurora_rust_user_copy_step'],
    Path('kernel/sys/syscall.c'): ['aurora_rust_syscall_dispatch', 'aurora_rust_path_policy_check'],
    Path('kernel/vfs/vfs.c'): ['aurora_rust_vfs_route'],
    Path('Makefile'): ['K_RUST_SRCS', 'kernel/rust/usercopy.rs', 'kernel/rust/path_policy.rs', '$(RUSTC) $(RUSTFLAGS)', 'kernel/rust/lib.rs'],
}
errors = []
for path, needles in checks.items():
    if not path.exists():
        errors.append(f'missing {path}')
        continue
    text = path.read_text()
    for needle in needles:
        if needle not in text:
            errors.append(f'{path}: missing {needle!r}')
if errors:
    for e in errors:
        print(e)
    raise SystemExit(1)
print('rust safety module integration checks passed')
