# AuroraOS status

AuroraOS is currently at `0.0.2.0`.

The system boots as a VMware Legacy BIOS x86_64 guest, passes through a two-stage BIOS loader, enters long mode, and starts an independent kernel. The kernel has its own memory manager, heap, interrupt setup, VFS, device files, a bounded writable EXT4 path with inline/depth-1/depth-2 extents, split extent leaf writes, truncate-time extent-tree compaction/demotion, persistent htree directory indexes, async-coalesced ordered metadata journaling/recovery with data-before-metadata sync, direct-data writeback caching with cache coherency, unwritten extent preallocation/conversion, orphan cleanup, metadata checksums, fsck repair-lite for htree/free-counter/dirent corruption with cache-coherent raw-media repair, VFS/userland sync-fsync-statvfs-install_commit-preallocate app-storage calls, cwd-relative filesystem operations, a syscall layer, and a small ring3 userland with `/sbin/init` and `/bin/aursh`.

## Working now

- BIOS stage1/stage2 boot path with early COM1 diagnostics.
- x86_64 GDT, IDT, IRQ, paging, VMM, heap, panic and logging code.
- Writable ramfs at `/`, devfs at `/dev`, tarfs support, and partition-bounded writable EXT4 mounted from the first Linux MBR partition.
- ATA PIO, MBR parsing, and a block layer for the VMware IDE disk path.
- ELF64 user program loading, ring3 entry, `int 0x80` syscalls, `/sbin/init`, and `/bin/aursh`.
- Process records, file descriptors, async `spawn`, blocking `wait` and `sleep`, deep-copy `fork`, and `exec`/`execv`/`execve`.
- Single-core preemptive scheduling through PIT IRQ0.
- Rust safety-boundary modules linked into the kernel for syscall dispatch, usercopy, VFS routing, and path policy checks, with generated ABI constants shared from the C header.
- Host tests and in-kernel `ktest`.

## Not done yet

- Full Linux-compatible EXT4 journal descriptor/commit block format, extent tree depth above 2, htree hash variants beyond the Aurora-compatible index block, full delayed allocation/writeback-daemon policy, app/package rollback database, deeper fsck repair coverage, and metadata checksum coverage matching every upstream EXT4 feature flag.
- SMP, APIC, PCI, AHCI, NVMe, USB, ACPI, and GUI.
- Copy-on-write `fork`.
- Interpreter and shebang support.
- Full kernel text/rodata/data page-permission split.
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
```
