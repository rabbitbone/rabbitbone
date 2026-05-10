# AuroraOS Stage 5 Process Registry

Stage 5 adds a real kernel process registry around the existing isolated ring3 execution path. It is not a preemptive scheduler yet. Processes still execute synchronously, but every user exec now has stable PID metadata, lifecycle state, timing, address-space generation and lookup contracts.

## Added

- Heap-backed completed process registry with 64 records.
- Stable PID allocation beginning at 1000.
- Address-space generation tracking independent of reused PML4 physical pages.
- Kernel process lookup API:
  - `process_current_pid`
  - `process_current_info`
  - `process_lookup`
  - `process_table_count`
  - `process_dump_table`
  - `process_table_selftest`
- Syscalls:
  - `AURORA_SYS_GETPID = 15`
  - `AURORA_SYS_PROCINFO = 16`
- Rust syscall dispatcher now decodes and validates `getpid` and `procinfo`.
- User library wrappers:
  - `au_getpid`
  - `au_procinfo`
- New ring3 test program:
  - `/bin/procstat`
- Shell command:
  - `procs`
- Static contract checker:
  - `scripts/check_process_contracts.py`

## ktest additions

- `/bin/procstat` validates current process metadata from user mode.
- Kernel `process/registry/contracts` suite validates completed-process lookup, monotonic registry count, invalid PID rejection and metadata integrity.
- Coverage postconditions still validate process cleanup, kernel CR3 restoration and filesystem state after userland tests.

## Deliberately not done yet

- No preemptive context switching yet.
- No concurrent user processes yet.
- No user-mode `spawn` syscall yet, because the current process executor is single-active-process by design. This stage makes the registry and syscall metadata safe before introducing concurrency.
