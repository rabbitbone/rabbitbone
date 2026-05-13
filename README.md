# AuroraOS

![Version](https://img.shields.io/badge/version-0.0.2.12-2f6fed)
![Target](https://img.shields.io/badge/target-x86_64%20VMware%20BIOS-222222)
![Kernel](https://img.shields.io/badge/kernel-independent-6b46c1)
![Written with](https://img.shields.io/badge/written%20with-GPT--5.5-0f766e)

> A small x86_64 operating system written from the ground up with GPT-5.5.

AuroraOS is an experimental amd64 operating system for VMware Legacy BIOS machines. It is not a Linux distribution, not a Unix clone, and not a bootloader demo. The repository contains the boot path, kernel, filesystem layer, syscall surface, small userland programs, host-side tests, and the tooling needed to build a raw disk image.

The current release line is `0.0.2.12`. Compared with `0.0.1.40`, AuroraOS now can boot through a BIOS stage1/stage2 loader, enter long mode, run a protected W^X/NX kernel image, maintain physical-page refcounts for copy-on-write fork, expose per-process user heaps through brk/sbrk and userlib malloc, provide VMA-backed demand-paged anonymous, file-backed private, and fork-inherited anonymous shared mmap/munmap/mprotect with VMA range tracking, discover ACPI tables, record APIC/IOAPIC topology, initialize HPET/TSC timer diagnostics, prepare SMP bootstrap topology, enumerate PCI devices, probe AHCI SATA disks with ATA-PIO fallback through a driver-tagged block registry, mount in-memory and disk filesystems, write EXT4 regular files through inline, depth-1, and depth-2 extent trees, maintain persistent directory htree indexes, journal metadata with async-coalesced ordered single-block recovery, buffer data writes with ordered data-before-metadata sync, preallocate zero-readable unwritten extents through EXT4, VFS, syscalls, and userland, recover orphaned unlinks, verify metadata checksums, repair corrupted htree metadata/free counters/dirent records even after raw-media cache divergence, create new directories as extent-backed inodes, expose sync/fsync/statvfs/install_commit/preallocate for application storage, handle cwd-relative filesystem operations, run the packaged `/sbin/init` and `/bin/aursh` userland shell path, and validate metadata after mutation stress. It can enter ring3, execute small ELF64 user programs, preempt them on a single CPU, expose terminal control syscalls for the userland shell, and replace a process image through `exec`, `execv`, and `execve`, including kernel-dispatched shebang scripts.

## What is inside

| Area | Status |
| --- | --- |
| Boot | BIOS stage1 and stage2 loader with E820 discovery and COM1 diagnostics |
| Kernel | x86_64 GDT, IDT, IRQ, paging, heap, VMM with kernel-image W^X/NX page protections, panic/logging, shell |
| Filesystems | VFS, writable ramfs, devfs, tarfs, bounded writable EXT4 adapter with inline, indexed, split-leaf, depth-2 extent trees, persistent htree directory indexes, heap-backed async metadata cache, buffered file-data writeback with coherency invalidation, unwritten extent preallocation/conversion exposed through VFS/userland, ordered metadata journal/recovery, orphan cleanup, metadata checksums, and fsck repair-lite for htree/free-counter/dirent corruption |
| Storage | ACPI discovery, APIC/HPET diagnostics, PCI enumeration, AHCI SATA, ATA PIO fallback, driver-tagged block layer, MBR parsing, installer-built EXT4 seed partition |
| User mode | ELF64 loader, ring3 transition, `int 0x80` syscalls, small test programs |
| Processes | process registry, heap-backed fd model, async spawn, blocking wait/sleep, fork, exec, user heap/brk/sbrk/malloc, demand-paged anonymous, file-backed private, and fork-inherited anonymous shared mmap/munmap/mprotect |
| Scheduling | single-core preemptive runqueue driven by PIT IRQ0 |
| Rust boundary | Rust modules linked into the kernel for syscall, usercopy, VFS route, and path policy checks |
| Testing | host tests plus in-kernel `ktest` coverage for boot, memory, VFS, filesystems, syscalls, scheduling, and userland |

## Built with GPT-5.5

AuroraOS was fully written by GPT-5.5. The project is intentionally kept as plain systems code: C, C++, assembly, and freestanding Rust, with small scripts around the build. The goal is to make the result readable and inspectable, not to hide the work behind generated scaffolding.

## Requirements

You need these tools on the host:

- `clang`
- `ld.lld`
- `llvm-objcopy`
- `make`
- a C++17 compiler
- Python 3
- Rust `1.94.1`

The repository includes [rust-toolchain.toml](rust-toolchain.toml), so Rust-aware environments should pick the expected toolchain automatically.

## Build

```sh
make clean
make
make test
```

The main build products are:

```text
build/aurora.img
build/kernel.elf
build/kernel.bin
build/stage1.bin
build/stage2.bin
build/tools/installer/aurora-install
```

If your environment does not have a system Rust install, point the helper script at an extracted Rust toolchain:

```sh
make clean
scripts/build_with_uploaded_rust.sh /path/to/rust test
scripts/build_with_uploaded_rust.sh /path/to/rust image
```

## Run in VMware

Create a new x86_64 VM with:

- Legacy BIOS firmware
- one IDE disk
- 512 MiB RAM or more
- one serial port connected to a file such as `aurora-com1.log`

Attach `build/aurora.img` as a raw disk image. The sample VMware configuration lives at [vmware/aurora-legacy.vmx.example](vmware/aurora-legacy.vmx.example).

The image layout is deliberately simple:

```text
sector 0       boot sector and MBR
sectors 1..64  stage2 loader
sectors 65..1080 kernel payload reservation
LBA 2048       Linux MBR partition with an installer-generated EXT4 seed filesystem
```

Early serial output should look like this before the kernel logger takes over:

```text
Aurora stage1: BIOS boot sector entered
Aurora stage1: stage2 loaded, jumping
Aurora stage2: entered
Aurora stage2: collecting E820
Aurora stage2: loading kernel
Aurora stage2: kernel loaded
Aurora stage2: entering long mode
```

## First commands

After the kernel CLI appears, these commands give a quick tour:

```text
help
mem
heap
vmm
mounts
ls /
cat /etc/motd
ls /disk0
cat /disk0/hello.txt
ktest
```

Useful process and scheduler checks:

```text
ps
procs
lastproc
sched
preempt
schedtest
runq
```

Small ring3 programs can be run directly:

```text
run /bin/hello
run /bin/fscheck /disk0/hello.txt
run /bin/writetest
run /bin/preemptcheck
run /bin/fdcheck
run /bin/procctl
run /bin/forkcheck
run /bin/execcheck
run /bin/execvecheck
run /bin/execfdcheck
spawn /bin/fscheck /disk0/hello.txt
qspawn /bin/hello
```

## Tests

Run host-side checks with:

```sh
make test
```

Inside AuroraOS, run:

```text
ktest
```

A passing in-kernel run ends with:

```text
KTEST_STATUS: PASS
```

`ktest` covers the kernel libraries, heap, VMM, VFS, ramfs, devfs, tarfs, block layer, MBR reader, EXT4 reader/writer including multi-leaf extent stress, syscall dispatcher, PIT timer, ELF validation, process paths, user-mode transition, and logging path.

## Docs

- [Current status](docs/STATUS.md)
- [Release notes](docs/RELEASES.md)

## License

AuroraOS is released under the [MIT License](LICENSE).

## Source layout

```text
boot/                  real-mode and long-mode loader
include/               public Aurora ABI/version headers
kernel/arch/x86_64/    CPU, GDT, IDT, IRQ, paging, entry assembly
kernel/core/           kernel main, shell, logging, panic, ktest
kernel/drivers/        VGA, serial, PIT, PIC, keyboard, ACPI, APIC, HPET, SMP staging, PCI, AHCI, ATA PIO, MBR, block layer
kernel/exec/           ELF64 image loader
kernel/fs/             EXT4 reader/writer
kernel/mm/             virtual memory and kernel heap
kernel/proc/           process table and process image management
kernel/rust/           Rust safety-boundary modules
kernel/sched/          task and scheduler code
kernel/sys/            syscall backend
kernel/vfs/            VFS, ramfs, devfs, tarfs, EXT4 adapter
scripts/               build and binary-check scripts
tests/                 host-side tests
tools/installer/       raw disk installer and EXT4 seed filesystem generator
user/                  tiny ring3 programs and CRT entry
userlib/               userland syscall wrappers and ABI headers
vmware/                VMware helper files and example config
```

## Current limits

AuroraOS is still an experimental OS. The current kernel is single-core, EXT4 write support is limited to the implemented regular-file and directory paths, extent-tree splitting is covered through the tested split-leaf/depth-2 cases only, and persistent htree directory indexes use the Aurora-compatible subset rather than the full set of Linux EXT4 hash variants. There is no AP startup/multi-core scheduling, IOAPIC IRQ routing, APIC timer, MSI/MSI-X, PCIe ECAM, NVMe, USB, GUI, full Linux-compatible journal descriptor/commit format, extent depth above 2, or complete upstream EXT4 feature coverage yet. `/dev/prng` and `/dev/urandom_insecure` are deterministic non-cryptographic PRNG devices until real entropy plumbing exists; no `/dev/random` crypto guarantee is exposed.

Those limits are intentional for now. The project is being built in small, testable stages, with the ABI version, kernel banner, and user-visible version kept in [include/aurora/version.h](include/aurora/version.h).
