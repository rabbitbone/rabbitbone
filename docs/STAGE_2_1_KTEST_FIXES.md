# AuroraOS Stage 2.1

Stage 2.1 fixes kernel self-test failures reported from VMware Stage 2.

## Fixed

- VFS read-only mount semantics now reject mutating operations before checking whether the filesystem implements the operation.
- ATA PIO read path now uses LBA28 for low disk addresses, which is the reliable VMware legacy IDE path for the boot image.
- ATA PIO keeps LBA48 as fallback for addresses beyond the LBA28 limit when the device reports support.
- ELF64 header validation now treats a zero-program-header executable header as a valid header. Full ELF loading still rejects images without PT_LOAD segments.

## Expected ktest result

```text
KTEST_STATUS: PASS
```
