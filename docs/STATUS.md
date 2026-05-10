# AuroraOS status

AuroraOS is currently at `0.0.1.13`.

The system boots as a VMware Legacy BIOS x86_64 guest, passes through a two-stage BIOS loader, enters long mode, and starts an independent kernel. The kernel has its own memory manager, heap, interrupt setup, VFS, device files, a read-only EXT4 path, a syscall layer, and a small ring3 userland.

## Working now

- BIOS stage1/stage2 boot path with early COM1 diagnostics.
- x86_64 GDT, IDT, IRQ, paging, VMM, heap, panic and logging code.
- Writable ramfs at `/`, devfs at `/dev`, tarfs support, and read-only EXT4 mounted from the first Linux MBR partition.
- ATA PIO, MBR parsing, and a block layer for the VMware IDE disk path.
- ELF64 user program loading, ring3 entry, and `int 0x80` syscalls.
- Process records, file descriptors, async `spawn`, blocking `wait` and `sleep`, deep-copy `fork`, and `exec`/`execv`.
- Single-core preemptive scheduling through PIT IRQ0.
- Rust safety-boundary modules linked into the kernel for syscall dispatch, usercopy, VFS routing, and path policy checks.
- Host tests and in-kernel `ktest`.

## Not done yet

- SMP, APIC, PCI, AHCI, NVMe, USB, ACPI, and GUI.
- Writable EXT4 and journaling.
- Copy-on-write `fork`.
- Environment-vector ABI.
- Close-on-exec descriptor flag.
- Interpreter and shebang support.
- Full kernel text/rodata/data page-permission split.
- Real entropy for `/dev/random`.

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
