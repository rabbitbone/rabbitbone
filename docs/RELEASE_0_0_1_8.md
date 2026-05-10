# AuroraOS 0.0.1.8

AuroraOS 0.0.1.8 is a Stage 8 scheduler return-path hardening release for the VMware ktest failures that remained after idle halt and heap hardening fixes.

## Fixes

- Kernel heap coalescing now merges only physically adjacent blocks. Previous heap extension blocks were linked in allocation order and could be non-contiguous; freeing neighboring list nodes could create an invalid synthetic large block and corrupt later kernel allocations.
- Async process context publication is now resilient to a lost `current_async_slot`: the scheduler resolves the current slot by PID before saving, preempting, faulting or exiting.
- `SYS_EXIT` under the async scheduler now always publishes terminal state through the root slot path when `current_proc` is active.
- Added defensive logging for impossible lost-slot terminal publication.

## ABI

- Version text: `0.0.1.8`
- Syscall ABI: `0x00000108`

## Expected VMware impact

The CLI no longer freezes after `ktest` from 0.0.1.6. This release targets the remaining `KTEST_STATUS: FAIL` cases where `/bin/fscheck`, `/bin/spawncheck`, `/bin/procctl`, `/bin/forkcheck`, and `/bin/schedcheck` completed useful user work but were reported as `status=fault` with `faulted=0`, which is consistent with corrupted heap/process-slot metadata or a lost async slot rather than a hardware exception.


## 0.0.1.8 fix

- `arch_user_resume()` is now declared as a returning trampoline, not `noreturn`. The function resumes ring3, but scheduler control returns to the caller through `arch_user_return_from_interrupt()` after syscall/IRQ handoff. Marking it `noreturn` let the C compiler place cleanup immediately after the call instead of continuing the run loop, so blocking wait, yield/sleep, spawned children and preempted `/bin/fscheck` could be reported as `PROC_ERR_FAULT` with `faulted=0`.
- `process_run_until_idle()` now explicitly continues the scheduling loop after a user-context return.
