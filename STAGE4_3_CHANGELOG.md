# AuroraOS Stage 4.3

Stage 4.3 fixes the VMware failures seen after Stage 4.2:

- `vmm map/translate/unmap 4K page` failed because `vmm_space_translate()` treated all page-table flags with a bitwise AND. That is correct for `USER` and `WRITE`, but wrong for `NX`, which is effective if any paging level sets it. The translator now computes effective flags explicitly.
- The page fault at VGA address `...b8f00` was caused by early kernel stack pressure. The Stage 4.2 kernel ended at `0x80000`, while the temporary boot stack previously left too little safe headroom for nested EXT4/VFS test calls. The early stack top is now `0x9e000`, below the EBDA boundary, and the TSS ring0 stack is 64 KiB.
- `check_kernel_layout.py` now enforces a minimum early-stack margin so future growth cannot silently recreate this corruption class.
- VGA output now clamps cursor state defensively so corrupted cursor globals cannot turn a diagnostic print into a page fault.

Expected runtime check:

```text
ktest
KTEST_STATUS: PASS
```
