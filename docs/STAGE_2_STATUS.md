# AuroraOS Stage 2 status

Stage 2 focuses on kernel platform hardening after the VMware long-mode boot path was confirmed stable.

## Completed in this increment

- Expanded the kernel self-test runner into a full-system `ktest` pass.
- Added subsystem coverage for:
  - libc string/memory helpers
  - bitmap allocator helper
  - ring buffer library
  - CRC32 library
  - bootinfo validation
  - physical page allocator
  - VMM map/translate/unmap path
  - kernel heap allocator
  - VFS root and mounted directories
  - writable ramfs
  - devfs devices: null, zero, random
  - tarfs open/mount/read/list/unmount
  - ATA block device presence
  - MBR parsing
  - raw EXT4 mount/read/path lookup
  - VFS EXT4 adapter at `/disk0`
  - syscall dispatcher create/open/read/write/seek/unlink/ticks
  - cooperative kernel task table
  - PIT timer advancement
  - ELF64 header validator
  - kernel log level/name path
- Added reusable kernel libraries:
  - `kernel/lib/ringbuf.c`
  - `kernel/lib/crc32.c`
- Added cooperative kernel task manager:
  - `kernel/sched/task.c`
  - shell commands: `ps`, `schedtest`
- Extended syscall ABI to Stage 2 IDs:
  - write, seek, create, mkdir, unlink, ticks
- Added memory and VMM self-tests.
- Added `tarfs_destroy` for clean temporary mounts.

## Runtime validation

From the Aurora CLI, run:

```text
ktest
```

A healthy VMware run should end with:

```text
KTEST_STATUS: PASS
```

## Host validation

```sh
make clean
make
make test
scripts/dump_boot_layout.py build/aurora.img
```

## Known limitations still present

- No ring3 process execution yet.
- ELF64 loader still validates/loads but does not transfer control to user mode.
- EXT4 is still read-only.
- The scheduler is a cooperative kernel task table, not preemptive process scheduling.
- No ACPI/APIC/PCI/AHCI/NVMe/SMP yet.
- UI is intentionally untouched.
