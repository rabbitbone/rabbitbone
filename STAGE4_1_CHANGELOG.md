# AuroraOS Stage 4.1

Stage 4.1 fixes the first isolated-userland loader regression found by VMware ktest.

## Fixed

- ELF PT_LOAD pages are now loaded through a two-phase permission model:
  - map as temporary writable user pages while the kernel copies bytes and zeroes BSS;
  - apply final per-page permissions after the image is fully loaded.
- Read-only and executable ELF pages no longer cause `PROC_ERR_RANGE` during kernel-side loading.
- Overlapping PT_LOAD segments sharing a page are handled without wiping previously loaded bytes.
- Page permissions are merged conservatively for overlapping segments:
  - write is enabled if any contributor needs write;
  - execute is enabled if any contributor needs execute;
  - NX is preserved only when all contributors are non-executable.
- `vmm_space_protect_4k()` was added for leaf PTE permission changes.
- VMM self-test now verifies permission downgrades from writable to non-writable NX user pages.
- Kernel identity mapping was expanded to 4 GiB for VMware Stage 4.1 so kernel copy paths can safely touch physical pages allocated below the current tracked-frame ceiling.

## Expected VMware test

Run:

```text
ktest
```

Expected final line:

```text
KTEST_STATUS: PASS
```
