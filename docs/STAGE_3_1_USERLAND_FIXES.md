# AuroraOS Stage 3.1 - Ring3 ktest fixes

Stage 3.1 fixes the first VMware-reported Stage 3 userland failures.

## Root causes

1. User ELF images were linked at `0x400000`, inside the kernel's 1 GiB bootstrap identity map. The VMM refuses to replace a 2 MiB identity huge page with 4 KiB user pages, so process loading could not create the first user mapping.
2. New user PTEs had `VMM_USER`, but existing upper-level page-table entries inherited from the identity map did not. On x86_64, every level in the walk must allow user access, so ring3 faulted even when the leaf PTE was user-accessible.
3. `gdt_selftest()` compared descriptors against pre-accessed values. The CPU may set the accessed bit, and the TSS descriptor becomes busy after `ltr`, so the test produced a false failure.
4. `arch_user_enter()` used `r12` as scratch without preserving the SysV callee-saved register contract.

## Changes

- User executables now link at `0x40000000`, outside the 1 GiB identity map and below the Stage 3 user stack top.
- `vmm_map_4k()` now propagates `VMM_USER`/`VMM_WRITE` into existing paging ancestors when creating mappings.
- GDT descriptors are initialized with the accessed bit already set.
- `gdt_selftest()` accepts both available and busy TSS descriptor states.
- `arch_user_enter()` uses only caller-saved scratch registers.
- `ktest` now prints detailed process diagnostics for ring3 failures.
- `scripts/check_userland.py` rejects user ELFs that overlap the identity-mapped low 1 GiB region.

## Expected result

After booting in VMware, running `ktest` should reach:

```text
KTEST_STATUS: PASS
```
