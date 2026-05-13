# AuroraOS status

AuroraOS is currently at `0.0.2.13`.

The system boots as a VMware Legacy BIOS x86_64 guest, passes through a two-stage BIOS loader, enters long mode, and starts an independent kernel. The kernel has its own memory manager, heap, interrupt setup, VFS, device files, a bounded writable EXT4 path with inline/depth-1/depth-2 extents, split extent leaf writes, truncate-time extent-tree compaction/demotion, persistent htree directory indexes, async-coalesced ordered metadata journaling/recovery with data-before-metadata sync, direct-data writeback caching with cache coherency, unwritten extent preallocation/conversion, orphan cleanup, metadata checksums, fsck repair-lite for htree/free-counter/dirent corruption with cache-coherent raw-media repair, VFS/userland sync-fsync-statvfs-install_commit-preallocate app-storage calls, cwd-relative filesystem operations, PCI enumeration, AHCI SATA discovery/read-write block support, an AHCI-first ATA-PIO-fallback block registry, a syscall layer with heap-backed descriptor tables, kernel shebang interpreter resolution, copy-on-write fork, per-process user heaps with brk/sbrk, VMA-backed demand-paged anonymous, file-backed private, and fork-inherited anonymous shared mmap/munmap/mprotect with VMA range tracking, and a small ring3 userland with `/sbin/init` and `/bin/aursh`.

## 0.0.2.13 status

Signals, process groups, and terminal job-control groundwork are implemented. The kernel now tracks per-process signal actions, masks, pending sets, process groups, sessions, and a terminal foreground process group. Ring3 signal handlers use a kernel-built user stack frame plus `sigreturn`, default terminate/stop/ignore behavior is modeled for the supported signal set, `kill`/`raise`/`sigaction`/`sigprocmask`/`sigpending` are exposed through the syscall ABI, and process registry snapshots include pgrp/sid/signal flags. `/bin/procctl` and ktest cover handlers, blocked delivery, pending state, child termination by signal, setpgid/getpgid, and tcgetpgrp/tcsetpgrp groundwork.

## 0.0.2.12 status

- Added ACPI RSDP/RSDT/XSDT discovery with MADT and HPET table parsing.
- Added LAPIC/IOAPIC topology diagnostics while keeping PIC/PIT as the interrupt fallback.
- Added HPET free-running counter initialization, invariant-TSC calibration when HPET is available, and timer diagnostics.
- Added SMP bootstrap groundwork: bounded CPU topology, BSP identification, and AP-startup staging metadata without enabling multi-core scheduling yet.
- Added shell/kctl diagnostics: `acpi`, `apic`, `hpet`, `timer`, and `smp`.

## Working now

- BIOS stage1/stage2 boot path with early COM1 diagnostics.
- x86_64 GDT, IDT, IRQ, paging, VMM with kernel-image W^X/NX page protections, heap, panic and logging code.
- Writable ramfs at `/`, devfs at `/dev`, tarfs support, and partition-bounded writable EXT4 mounted from the first Linux MBR partition.
- PCI config-space enumeration, AHCI SATA probing/read-write support, ATA PIO fallback, MBR parsing, and a driver-tagged block registry used by EXT4 mount selection.
- ELF64 user program loading, kernel `#!` interpreter dispatch for scripts, ring3 entry, `int 0x80` syscalls, `/sbin/init`, and `/bin/aursh`.
- Process records, file descriptors, async `spawn`, blocking `wait` and `sleep`, copy-on-write `fork`, per-process `brk`/`sbrk` heaps, VMA-backed demand-paged anonymous, file-backed private, and anonymous `MAP_SHARED` `mmap`/`munmap`/`mprotect`, and `exec`/`execv`/`execve`, including shebang scripts with bounded recursion.
- Single-core preemptive scheduling through PIT IRQ0, with ACPI-discovered APIC topology and HPET/TSC clocksource diagnostics staged for later APIC timer routing.
- Rust safety-boundary modules linked into the kernel for syscall dispatch, usercopy, VFS routing, and path policy checks, with generated ABI constants shared from the C header.
- Host tests and in-kernel `ktest`.

## Not done yet

- Full Linux-compatible EXT4 journal descriptor/commit block format, extent tree depth above 2, htree hash variants beyond the Aurora-compatible index block, full delayed allocation/writeback-daemon policy, app/package rollback database, deeper fsck repair coverage, and metadata checksum coverage matching every upstream EXT4 feature flag.
- AP startup/multi-core scheduling, IOAPIC IRQ routing, APIC timer, MSI/MSI-X, PCIe ECAM, NVMe, USB, and GUI.
- Real entropy and a future cryptographic `/dev/random`; current `/dev/prng` and `/dev/urandom_insecure` are explicitly non-cryptographic.

## Runtime checks

After booting in VMware, run:

```text
ktest
```

A passing run ends with:

```text
KTEST_STATUS: PASS
```

Useful manual checks:

```text
mounts
ls /
cat /etc/motd
ls /disk0
cat /disk0/hello.txt
run /bin/hello
run /bin/fscheck /disk0/hello.txt
run /bin/execcheck
sched
preempt
pci
disks
blk
```

- `mmap` VMAs are demand-paged: anonymous and file-backed private pages are materialized on user page fault, while `munmap`, `mprotect`, fork/COW, exec, and exit keep VMA/PTE/refcount state consistent.
