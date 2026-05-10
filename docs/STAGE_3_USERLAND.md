# AuroraOS Stage 3: ring3 userland path

Stage 3 adds the first real user-mode execution path on top of the Stage 2 kernel platform.

## Added

- Kernel GDT reload with kernel/user code/data selectors and a 64-bit TSS.
- IDT vector `0x80` exposed to CPL3 as the Aurora syscall gate.
- Kernel/user transition through `iretq`.
- TSS-backed ring3 to ring0 interrupt stack switch.
- User process loader for ELF64 `ET_EXEC` binaries.
- User virtual mappings for `PT_LOAD` segments and user stack pages.
- Argument vector construction on the user stack.
- Controlled return from `SYS_EXIT` back to the kernel without tearing down the VM.
- User fault containment: user exceptions are converted to process failure instead of kernel panic.
- Minimal user ABI and user library in `userlib/include/aurora_sys.h` and `user/lib/aurora.c`.
- User programs:
  - `/bin/hello`
  - `/bin/fscheck`
  - `/bin/writetest`
- Embedded user binary generator: `scripts/bin2c.py`.
- Static userland ELF verifier: `scripts/check_userland.py`.
- Shell commands:
  - `run PATH [ARGS]`
  - `lastproc`
  - `userbins`
- `ktest` now runs ring3 programs and verifies syscall-backed userland I/O.

## Runtime checks

Inside VMware:

```text
ktest
run /bin/hello
run /bin/writetest
run /bin/fscheck /disk0/hello.txt
lastproc
```

Expected final test line:

```text
KTEST_STATUS: PASS
```

## Current limitations

- User programs share the kernel page table; mappings are tracked and released, but this is not yet per-process address-space isolation.
- The syscall ABI is `int 0x80`; `syscall/sysret` is not enabled yet.
- Process execution is synchronous; there is no preemptive userspace scheduler yet.
- File handles are still a global kernel syscall table, not a per-process descriptor table.
- EXT4 remains read-only.
