# Rabbitbone release notes

This file keeps the release history short enough to be useful. Older one-off stage notes were folded into this summary.


## 0.0.3.0

- UEFI NXE handoff fix: the kernel now enables `EFER.NXE` before installing NX-marked page tables, matching the legacy BIOS stage2 long-mode setup and preventing early UEFI triple faults during `vmm_init`.
- UEFI framebuffer console fix: the loader now passes GOP framebuffer metadata through bootinfo and the kernel maps it after `vmm_init`, so VMware's graphical console shows kernel/user shell output instead of remaining on the loader banner.
- UEFI framebuffer glyph fix: lowercase ASCII glyphs with an empty top row are now rendered correctly instead of being replaced with `?`.
### 0.0.2.15 fix notes

- Fixed the VMware UEFI live configuration: the ISO CD-ROM now uses the real `ide1:0` device prefix instead of the invalid `dice0` typo.
- Corrected the VMware ISO path to `../build/rabbitbone-live.iso`, because `.vmx` relative paths are resolved from the `vmware/` directory.
- Added `scripts/check_vmware_configs.py` to keep UEFI live VMX examples from regressing.


UEFI live ISO boot path over `0.0.2.14`.

- Replaced the default Legacy BIOS raw-disk build with a UEFI live ISO target, `build/rabbitbone-live.iso`.
- Added a freestanding `BOOTX64.EFI` loader that reads `RABBITBONE/KERNEL.BIN` and `RABBITBONE/ROOT.IMG` from the ISO EFI system partition, imports the UEFI memory map into bootinfo v2, exits boot services, and jumps to the Rabbitbone kernel at the existing low-memory entry point.
- Added a self-contained Python ISO/FAT image generator, so the default live ISO does not depend on GRUB, Limine, xorriso, or mtools.
- Added `rabbitbone-install --root-only` to build the live root disk image without BIOS stage sectors or a kernel payload.
- Added a boot-module RAM disk block driver; the kernel now registers the UEFI-loaded root image as `ramdisk0`, probes its MBR Linux partition, and mounts the seeded EXT4 filesystem at `/disk0`.
- Extended boot diagnostics with UEFI/live-ISO source flags and boot module reporting.
- Kept the old BIOS disk path as an explicit `make legacy-image` regression target instead of the release artifact.
- Bumped the kernel version and syscall ABI to `0.0.3.0` / `0x00000300`.


## 0.0.2.14

Disk-backed userland and boot contract cleanup over `0.0.2.13`.

- Removed `user_bins.o` from the default kernel release link; embedded userland is now gated behind `RABBITBONE_EMBED_USERLAND=1` as a debug/fallback build option.
- Extended the image installer to seed `/bin`, `/sbin`, and `/etc` into the EXT4 partition and install all user ELF payloads there.
- Changed the kernel boot path to mount the first valid EXT4 partition at `/disk0` and start `/disk0/sbin/init`.
- Added compatibility symlinks from the boot ramfs `/bin` and `/sbin/init` to `/disk0` so existing absolute-path tests keep exercising the disk-backed files.
- Updated PATH defaults to `/disk0/bin:/disk0/sbin:/bin:/sbin` in the seed filesystem, boot ramfs profile, and shell fallback.
- Upgraded bootinfo to v2 with boot modules, root-device LBA/size, kernel source, flags, and command-line fields plus `boot` diagnostics.
- Bumped the kernel version and syscall ABI to `0.0.2.14` / `0x0000020e`.

## 0.0.2.12

ACPI + APIC + HPET/TSC timer + SMP bootstrap groundwork over `0.0.2.11`.

- Added ACPI discovery through legacy BIOS memory scanning for RSDP plus RSDT/XSDT SDT validation.
- Parsed MADT for local APIC CPUs, IOAPICs, interrupt-source overrides, and LAPIC address overrides.
- Parsed HPET and initialized a free-running HPET clocksource when advertised.
- Added timer diagnostics with PIT fallback, HPET monotonic time, and invariant-TSC calibration when HPET is present.
- Added APIC diagnostics and SMP bootstrap groundwork without enabling AP startup or multi-core scheduling yet.
- Added `acpi`, `apic`, `hpet`, `timer`, and `smp` shell/kctl commands and ktest coverage.
- Bumped the kernel version and syscall ABI to `0.0.2.12` / `0x0000020c`.

## 0.0.2.11

PCI + AHCI + modern block-device layer update over `0.0.2.10`.

- Added a legacy CF8/CFC PCI config-space enumerator with bus/device/function scanning, vendor/device IDs, class/subclass/prog_if, header type, IRQ line/pin, BAR base/size decoding, and bounded capability-list discovery.
- Added `pci`/`lspci` diagnostics through kctl, the user shell, and the emergency kernel shell.
- Added an AHCI SATA block driver path that locates PCI class `01:06:01` controllers, maps ABAR MMIO, enumerates implemented SATA ports, identifies ATA disks, and exposes sector read/write through the block layer.
- Extended the block registry with driver identity, write/flush/DMA flags, optional flush op, `blockN` diagnostics, and AHCI-first / ATA-PIO-fallback discovery.
- Changed EXT4 mount policy to probe all registered block devices for a valid MBR Linux partition and mount the first valid EXT4 at `/disk0`, instead of stopping on the first IDE-shaped candidate.
- Added boot diagnostics for selected block driver/device/partition and ktest coverage for PCI enumeration, driver-tagged block devices, and selected Linux-partition probing.
- Bumped the kernel version and syscall ABI to `0.0.2.11` / `0x0000020b`.


## 0.0.2.10

- Added anonymous `MAP_SHARED` VMAs for `mmap` with fork inheritance.
- Backed shared anonymous mappings with refcounted kernel shared-anon objects and demand page materialization.
- Kept `MAP_PRIVATE` COW behavior unchanged and rejected unsupported file-backed `MAP_SHARED`.
- Added `/bin/mmapsharedcheck` and ktest coverage for materialized/unmaterialized shared mappings, child `munmap`, fork+exec release, and invalid flag combinations.
- Bumped the kernel version and syscall ABI to `0.0.2.10` / `0x0000020a`.

## 0.0.2.9

VMA-backed demand paging for mmap update over `0.0.2.8`.

- Bumped the kernel version and syscall ABI to `0.0.2.9` / `0x00000209`.
- Changed anonymous and file-backed private `mmap` to install VMA metadata only; no user pages or physical frames are allocated by `mmap` itself.
- Added recoverable user page-fault handling for non-present mmap VMA pages, materializing zero-filled anonymous pages or private VFS-backed pages from the stored file reference.
- Made `munmap` and `mprotect` operate on partially materialized ranges, including VMA splitting, unmapped-page skips, PTE updates for present pages, and future-protection changes for not-yet-faulted pages.
- Preserved fork/COW, exec, exit, refcount teardown, fd-close lifetime, and file-backed private behavior under demand paging.
- Extended `/bin/mmapcheck` and `/bin/mmapfilecheck` to verify that mappings do not consume physical pages until first touch.

## 0.0.2.8

File-backed private mmap over VFS handles update over `0.0.2.7`.

- Bumped the kernel version and syscall ABI to `0.0.2.8` / `0x00000208`.
- Extended `mmap` to the standard six-argument user ABI: address, length, protection, flags, VFS handle, and file offset.
- Kept anonymous mappings strict: `MAP_ANON | MAP_PRIVATE` requires `fd=-1` and `offset=0`.
- Added file-backed `MAP_PRIVATE` mappings over readable VFS file handles, with page-aligned offsets, VFS type/permission checks, zero-fill past EOF, and W^X protection enforcement.
- File-backed mappings are private eager copies: closing the fd or later writing the backing file does not modify already mapped pages, and writes through the mapping never write back to VFS.
- Preserved VMA/range tracking for file-backed mappings, including file offset metadata across VMA splitting for `munmap` and `mprotect`.
- Kept file-backed mmap pages on the same refcount/COW path as anonymous mappings across `fork`, child exit, exec replacement, and process teardown.
- Added `/bin/mmapfilecheck` and ktest coverage for VFS-handle backed mappings, offset mapping, zero-fill past EOF, fd-close lifetime, private write isolation, partial unmap faults, mprotect faults, fork COW, fork+exec release, and invalid fd/offset/flag/directory/write-only-handle rejection.

## 0.0.2.7

Anonymous mmap/munmap/mprotect and VMA/range tracking update over `0.0.2.6`.

- Bumped the kernel version and syscall ABI to `0.0.2.7` / `0x00000207`.
- Added anonymous private `mmap`, `munmap`, and `mprotect` syscalls through the C ABI, Rust dispatcher/name table/validator, and userland wrappers.
- Added per-process VMA/range tracking for image, stack, heap, and anonymous mmap ranges.
- Added a bounded anonymous mmap arena with page-aligned allocation, fixed mappings, overlap rejection, W^X enforcement, and writable NX private pages.
- Implemented subrange `munmap` with VMA splitting and mapping release through the shared page refcount path.
- Implemented `mprotect` with VMA splitting and PTE flag updates; read-only write faults stay non-COW faults, while forked writable pages keep correct COW behavior.
- Made anonymous mmap pages participate in COW `fork` and release correctly across child exit, exec replacement, and process teardown.
- Added `/bin/mmapcheck` and ktest coverage for anonymous mapping, fixed mapping rejection/placement, partial unmap faults, mprotect faults and restoration, W^X rejection, mmap COW fork isolation, and fork+exec release.

## 0.0.2.6

User heap, brk/sbrk, and userlib malloc update over `0.0.2.5`.

- Bumped the kernel version and syscall ABI to `0.0.2.6` / `0x00000206`.
- Added per-process heap bounds (`heap_base`, `heap_break`, `heap_limit`) derived from the loaded user ELF image and kept below the user stack window.
- Added `brk` and `sbrk` syscalls through the C ABI, Rust dispatcher/name table/validator, and userland wrappers.
- Implemented lazy 4 KiB user heap mapping on heap growth and clean unmap/decref on shrink, exit, and exec replacement.
- Preserved heap mappings through COW `fork`: writable heap pages become COW like other writable user pages, and child/parent writes are isolated through the recoverable user page-fault path.
- Added userlib `malloc`, `free`, `calloc`, and `realloc` on top of `sbrk`, with block splitting, coalescing, overflow checks, and 16-byte alignment.
- Added `/bin/heapcheck` and ktest coverage for direct `brk`/`sbrk`, allocator reuse, calloc zeroing, realloc preservation, heap COW fork isolation, and fork+exec heap-page release.
- Enabled userland function/data section GC so the allocator does not bloat every embedded test ELF that does not use it.

## 0.0.2.5

Copy-on-write fork update over `0.0.2.4`.

- Bumped the kernel version and syscall ABI to `0.0.2.5` / `0x00000205`.
- Added physical page refcounting with `memory_ref_page`, `memory_unref_page`, and `memory_page_refcount`; user address-space teardown now decrements shared backing pages instead of freeing them blindly.
- Changed `fork()` from eager page copying to COW: read-only and executable pages are shared immediately, writable pages are made read-only plus `VMM_COW` in both parent and child, and the underlying physical page refcount is incremented.
- Added user page-fault COW handling for present+write+user faults: single-reference COW pages regain `VMM_WRITE`, shared COW pages get a private copied page, and non-COW write faults still terminate the process.
- Releases mappings on process exit and exec replacement so forked children and fork+exec paths decref/free shared pages promptly.
- Expanded `/bin/forkcheck` and ktest coverage for parent/child COW isolation, distinct address-space generations, fork+exec release, child-exit page validity, and rejecting writes to read-only text as non-COW faults.
- Removed the stale unbuilt `user/bin/execfail.c` probe instead of keeping dead source outside `USER_C_PROGS`.

## 0.0.2.4

Interpreter and shebang execution support update over `0.0.2.3`.

- Bumped the kernel version and syscall ABI to `0.0.2.4` / `0x00000204`.
- Added kernel-level `#!` resolution in the process loader, shared by `spawn`, `spawnv`, `exec`, `execv`, and `execve`, before user image mapping.
- Rewrites script execution to the interpreter argv contract: interpreter path, optional shebang argument, script path, then original caller arguments after `argv[0]`.
- Enforces a bounded 127-byte shebang line, absolute interpreter paths, path-policy normalization, read access for the script, normal ELF validation and execute permission for the final interpreter, inherited envp handling, existing argv/env limits, and a two-hop recursion limit.
- Added ktest coverage using existing `/bin/exectarget` and `/bin/execcheck` probes for no-argument shebangs, optional interpreter args, envp preservation, syscall `spawnv`, ring3 `execv`, relative/empty interpreters, and recursive loops.
- Kept the low-memory kernel image within the BIOS-safe load window by reusing existing userland probes and moving syscall handle/pipe tables from static `.bss` to heap-backed initialization.
- Relaxed `/bin/rbsh` preflight checks so shell command execution and `spawn` can hand shebang files to the kernel instead of rejecting non-ELF script heads.

## 0.0.2.3

Kernel W^X and full image page-protection hardening update over `0.0.2.2`.

- Bumped the kernel version and syscall ABI to `0.0.2.3` / `0x00000203`.
- Removed the first-2 MiB limit from fine-grained kernel identity mapping: every 2 MiB identity-map window intersecting `__kernel_start..__kernel_end` is now split into 4 KiB mappings.
- Enforced page-level kernel image permissions: `.text` is global read/execute, `.rodata` is global read-only NX, and `.data`/`.bss` are global writable NX; non-kernel identity mappings remain writable NX.
- Added `vmm_kernel_protection_selftest()` to validate kernel section flags and reject `VMM_USER` mappings/protections over the kernel virtual image range.
- Extended the `vmm` shell diagnostics with `kernel_text_ro`, `kernel_rodata_nx`, and `kernel_data_nx` status fields.


## 0.0.2.2

Terminal and shell polish update over `0.0.2.1`.

- Bumped the kernel version and syscall ABI to `0.0.2.2` / `0x00000202`.
- Added terminal-control syscalls for scrolling, cursor movement, line clearing, full-screen clearing, and cursor visibility.
- Extended the Rust syscall decoder/name table and userland wrappers for the new terminal ABI.
- Split `/bin/rbsh` line editing into its own checked-in fragment and routed shell input through the new terminal controls.
- Improved VGA/console/TTY cursor and screen handling and expanded `termcheck` coverage for the new calls.
- Added Linguist hints so checked-in C `.inc` fragments are counted as C instead of inflating GitHub's C++ language share.

## 0.0.2.1

Patch update over `0.0.2.0`.

- Bumped the kernel version and syscall ABI to `0.0.2.1` / `0x00000201`.
- Split `/bin/rbsh` into smaller checked-in implementation fragments and taught the build to track those shell fragments as dependencies.
- Made root directory listing merge the real root filesystem entries with mounted top-level entries without emitting duplicate names.
- Kept the `0.0.2.x` storage/runtime surface, `/sbin/init`, `/bin/rbsh`, EXT4 durability work, and validation checks from `0.0.2.0` intact.

## 0.0.2.0

First full `0.0.2.x` release line, compared with `0.0.1.40`.

`0.0.1.49` was the final preparatory slice for this release; its cwd-relative filesystem work is included here.

- Bumped the kernel version and syscall ABI to `0.0.2.0` / `0x00000200`.
- Promoted the storage/runtime work landed after `0.0.1.40` into the `0.0.2.x` line: cwd-relative filesystem operations, userland wrappers, EXT4/VFS/ramfs path plumbing, and runtime bad-path/descriptor coverage.
- Kept the app-storage surface from the `0.0.1.41` through `0.0.1.49` preparation window: `sync`, descriptor-backed `fsync`, `statvfs`, atomic `install_commit`, and `preallocate`.
- Preserved the EXT4 durability work from the same window: ordered metadata journaling/recovery, data-before-metadata writeback, orphan cleanup, cache-coherent repair scans, metadata checksum validation, htree/free-counter/dirent repair-lite, extent-backed directories, and unwritten extent conversion.
- Added the packaged `/sbin/init` and `/bin/rbsh` userland shell path to the default image alongside the existing diagnostic programs.
- Split several large kernel, userland, test, and installer modules into smaller checked-in implementation fragments while keeping the public build targets and runtime behavior intact.
- Extended split-source integrity, userland, boot-sector, stage2-layout, Rust-symbol, and release-version checks so the release can be validated from source.

## 0.0.1.49

Preparatory update for the upcoming `0.0.2.0` line.

This is not the full `0.0.2.0` release. It keeps the project on the `0.0.1.x` release line while landing the filesystem, syscall, and runtime groundwork needed for the next minor line.

- Bumped the kernel version and syscall ABI to `0.0.1.49` / `0x00000131`.
- Added cwd-relative filesystem syscall groundwork and userland wrappers for the future `0.0.2.0` storage surface.
- Extended VFS, ramfs, EXT4, and process state plumbing for cwd-aware path resolution without promoting the project to the `0.0.2.0` release line yet.
- Expanded runtime coverage for cwd-relative file operations, descriptor behavior, bad-path handling, and EXT4 metadata stability.
- Kept this release marked as a `0.0.1.x` preparatory patch so the eventual `0.0.2.0` tag can remain the first complete release of that line.

## 0.0.1.46

Stage19.23 EXT4 VFS/userland preallocation ABI.

- Bumped the kernel version and syscall ABI to `0.0.1.46` / `0x0000012e`.
- Promoted EXT4 unwritten-extent preallocation from the raw EXT4 helper into the VFS, syscall dispatcher, and userland wrapper surface as `preallocate(path, size)`.
- Added runtime coverage that preallocated EXT4 files expose zero-readable unwritten blocks, convert correctly on write, truncate/unlink cleanly, and work from ring3 userland through `/disk0`.
- Kept existing app-storage guarantees for sync, fsync, statvfs, install_commit, htree repair, extent trees, and ordered writeback intact.

## 0.0.1.45

Stage19.22 EXT4 extent-backed directories and dirent-repair target fix.

- Bumped the kernel version and syscall ABI to `0.0.1.45` / `0x0000012d`.
- Changed newly created EXT4 directories to use inline extent roots for their data blocks, matching the regular-file allocator path and the repair/fsck raw-media tests.
- Keeps legacy direct-block seed directories readable while making app-created directories compatible with extent validation, htree repair, dirent normalization, truncate/unlink cleanup, and future multi-block directory growth.

## 0.0.1.44

Stage19.21 EXT4 cache-coherent repair and dirty-data preservation.

- Bumped the kernel version and syscall ABI to `0.0.1.44` / `0x0000012c`.
- Fixed the stage19.20 regression where a secondary raw EXT4 mount invalidated dirty data-cache slots belonging to the live VFS mount, causing newly created file payloads and sparse extent samples to disappear before sync.
- Kept fsck-lite raw-media visibility by flushing dirty cache state and invalidating only clean cached blocks for the same partition before validate/repair scans.
- Preserved the stage19.19 dirent repair normalization fix while keeping htree repair, indexed extent readback, install_commit, and ordered writeback coherent.

## 0.0.1.43

Stage19.20 EXT4 fsck cache coherency and app-storage stability.

- Bumped the kernel version and syscall ABI to `0.0.1.43` / `0x0000012b`.
- Made EXT4 validate/repair paths flush and invalidate shared read caches before scanning metadata, so fsck-lite sees raw-media corruption even when the mounted filesystem still has a clean cached copy of the same directory block.
- Fixed directory unlink of the first entry in a block to create a canonical free dirent instead of leaving stale `name_len`/`file_type` fields behind.
- Kept the existing ordered writeback, install_commit, htree, extent, and syscall paths intact while closing the stage19.19 runtime failure.

## 0.0.1.42

Stage19.19 EXT4 dirent repair normalization.

- Bumped the kernel version and syscall ABI to `0.0.1.42` / `0x0000012a`.
- Reworked EXT4 repair-lite for directory entries to compact live records and normalize malformed free-slot `rec_len`, `name_len`, and `file_type` chains.
- Reduced the in-kernel log ring from 32 to 24 lines to keep the freestanding kernel under the early boot stack safety boundary after the repair code growth.
- Fixed the stage19.18 runtime failure in `EXT4 repair-lite normalizes corrupted dirent rec_len/free slot metadata` without changing the already-green extent, writeback, htree, install-commit, and syscall paths.

## 0.0.1.41

Stage19.18 EXT4 atomic install/update and dirent repair.

- Bumped the kernel version and syscall ABI to `0.0.1.41` / `0x00000129`.
- Added VFS and syscall-level `install_commit(staged, final)`, which syncs the target filesystem, atomically renames a staged path into place, then syncs again for application/package install boundaries.
- Added rename-overwrite semantics for ramfs and EXT4 regular files, preserving replacement payloads and removing the overwritten inode through the orphan/free path.
- Added EXT4 same-type replacement handling for empty directories, including `..` update when moving a directory across parents.
- Added EXT4 dirent `rec_len`/free-slot validation and repair-lite normalization for corrupted directory record metadata.
- Extended runtime contracts for file rename-overwrite, staged app install commit, syscall/userland install commit, corrupted dirent repair, and final metadata validation.
- Moved two 4 KiB ktest scratch buffers from static BSS to heap allocation to keep the freestanding kernel below the early-stack safety boundary.

## 0.0.1.40

Stage19.17 syscall validator recovery and ABI boundary hardening.

- Bumped the kernel version and syscall ABI to `0.0.1.40` / `0x00000128`.
- Fixed the Rust syscall decoder self-test after the `sync`/`fsync`/`statvfs` ABI expansion by checking `RABBITBONE_SYS_MAX` instead of the old `sync` syscall number as the unsupported boundary.
- Added explicit Rust validator contracts for `sync`, `fsync`, and `statvfs`, keeping C and Rust syscall validation aligned.
- Extended the Rust ABI generator to export `RABBITBONE_SYS_MAX`, preventing future decoder boundary tests from drifting when syscall IDs are added.
- Reduced the diagnostic log ring from 40 to 32 lines to keep the freestanding kernel below the early-stack safety boundary.

## 0.0.1.39

Stage19.16 EXT4 app-storage syscall contract update.

- Bumped the kernel version and syscall ABI to `0.0.1.39` / `0x00000127`.
- Added VFS-level `sync`, descriptor-backed `fsync`, and `statvfs` plumbing for persistent application storage.
- Added EXT4 `statvfs` capacity reporting with persistent/journaled/repairable mount flags, free block/inode counters, block size, max name length, mount path, and filesystem name.
- Added userland wrappers `au_sync`, `au_fsync`, and `au_statvfs`, and wired the Rust syscall dispatcher/name table for the new stable filesystem ABI.
- Extended runtime contracts for EXT4 capacity reporting, writeback flush boundaries, and syscall validation for the new FS calls.
- Reduced the in-kernel log ring from 48 to 40 lines to preserve the early boot stack layout after adding the storage ABI.

## 0.0.1.38

Stage19.15 EXT4 repair-lite and fault-injection contracts.

- Bumped the kernel version and syscall ABI to `0.0.1.38` / `0x00000126`.
- Added `ext4_repair_metadata()` for fsck-lite recovery of free block/inode counters, metadata checksums, and Rabbitbone htree indexes.
- Added htree index consistency checking and rebuild logic that scans directory records, drops a corrupt index block, rebuilds a sorted persistent index, and preserves lookups.
- Added repair telemetry to fsck reports, EXT4 performance counters, and the `ext4` shell command.
- Added runtime fault-injection contracts for corrupted htree metadata and corrupted free counters, followed by repair and post-repair validation.

## 0.0.1.37

Stage19.14 EXT4 unwritten-extent preallocation and conversion.

- Bumped the kernel version and syscall ABI to `0.0.1.37` / `0x00000125`.
- Added EXT4 unwritten extent support in inline, depth-1, and depth-2 extent trees.
- Added `ext4_preallocate_file_path()` for zero-readable preallocation without eager data writes.
- Added safe conversion of an unwritten block to initialized data on first write, including split/merge of neighboring extent records.
- Kept security semantics for partial writes by zero-filling converted blocks in the data cache before overlaying user payload, avoiding stale disk-data exposure.
- Extended fsck/extent inspection and shell perf counters with unwritten extent/block and conversion telemetry.
- Added runtime coverage for preallocate-read-zero, write conversion, no stale data leakage, truncate, unlink, and metadata consistency.

## 0.0.1.34

Stage 19.11 EXT4 cache-coherency and runtime correctness recovery.

- Bumped the kernel version and syscall ABI to `0.0.1.34` / `0x00000122`.
- Fixed the stage19.10 runtime failures where dirty metadata-cache blocks could survive after a metadata block was freed and then reused as file data, causing sparse and indexed extent readback to return stale metadata instead of file payload.
- Added metadata-cache invalidation on direct data writes and block free, while preserving async metadata write-combining for performance.
- Fixed htree index offsets for entries inserted by splitting an occupied directory record; the index now points to the newly-created record instead of the previous record.
- Strengthened htree validation so fsck-like checks verify that every htree entry resolves to the exact referenced directory record, inode, file type, and name hash.
- Added mount-time free-counter reconciliation so a second mount cannot overwrite correct cached journal-reserved free counts with stale superblock counts read through the raw boot path.
- Extended EXT4 perf reporting with metadata-cache invalidation counters.

## 0.0.1.30

Stage 19.7 EXT4 extent-demotion correctness update.

- Bumped the kernel version and syscall ABI to `0.0.1.30` / `0x0000011e`.
- Fixed indexed-extent demotion after truncate so dirty leaf buffers are persisted before leaf metadata is collapsed into inline extents.
- Generalized demotion to collapse any depth-1 extent tree whose remaining extents fit in the inode inline root, including trees that previously had multiple split leaves.
- Ensured truncate calls the demotion pass after tail block removal so sparse multi-leaf files shrink back to inline form and release leaf metadata blocks.
- Preserved metadata counters and allocation bitmaps across truncate, unlink, and fsck-like EXT4 validation after mutation stress.

## 0.0.1.28

Stage 19.5 EXT4 extent-leaf split runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.28` / `0x0000011c`.
- Added real split support for depth-1 EXT4 indexed extent leaves when a leaf reaches capacity.
- Indexed extent roots can now reference multiple external leaf blocks, with sorted root indexes and read/write routing across leaf boundaries.
- Added truncate/unlink coverage for multi-leaf indexed extent files, including metadata leaf block reclamation.
- Extended EXT4 metadata validation to scan allocated inodes, inspect extent trees, and verify referenced extent data/metadata blocks are actually allocated in the block bitmap.
- Added CLI EXT4 diagnostics for checked inode counts and extent tree shape/data/metadata block accounting.
- Extended `ktest` with a 96-sparse-extent runtime stress file that forces leaf split, verifies sparse readback, validates zero-filled holes, truncates across leaves, and checks allocation counters return to baseline after unlink.

## 0.0.1.27

Stage 19.4 EXT4 indexed-extent runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.27` / `0x0000011b`.
- Added indexed EXT4 extent promotion: overflowed inline extent trees move into a depth-1 root with an external leaf block.
- Kept sparse EXT4 growth semantics across indexed extents: gap writes keep holes unallocated while reads return zero-filled data.
- Added safe freeing/truncation/unlink for indexed extent data and metadata leaf blocks.
- Added public inode extent introspection helpers for runtime contracts.
- Extended `ktest` coverage for five-discontiguous-block sparse files, indexed extent readback, hole reads, truncate, unlink, and metadata consistency.

## 0.0.1.26

Stage 19.3 EXT4 extent-write runtime update.

- Bumped the kernel version and syscall ABI to `0.0.1.26` / `0x0000011a`.
- Added inline EXT4 extent trees for newly-created regular files and made VFS create/write/truncate/unlink operate on those inodes.
- Added sparse EXT4 growth semantics: gap writes and expanding truncate keep holes unallocated while reads return zero-filled data.
- Added EXT4 metadata validation that cross-checks group descriptors, allocation bitmaps, and superblock counters.
- Extended `ktest` coverage for extent-backed files, sparse writes, sparse truncation, metadata consistency before/after mutation tests, and CLI `ext4` fsck reporting.

## 0.0.1.15

Stage 10 exec and descriptor-control update.

- Bumped the kernel version and syscall ABI to `0.0.1.15` / `0x0000010f`.
- Added `fdctl` and close-on-exec descriptor flags.
- Added `execve` with argv and environment-vector handoff.
- Added `/bin/execfdcheck`, `/bin/execvecheck`, and `/bin/exectarget` userland regression programs.
- Extended ktest coverage for inherited descriptors, close-on-exec behavior, and environment passing across exec.

## 0.0.1.14

VMM regression update.

- Bumped the kernel version and syscall ABI to `0.0.1.14` / `0x0000010e`.
- Fixed page-table creation so intermediate paging tables receive only valid table-level flags, while leaf entries keep leaf-level permissions.
- Refreshed the README to match the current release line.

## 0.0.1.13

Hardening update over the `0.0.1.12` baseline.

- Bumped the kernel version and syscall ABI to `0.0.1.13` / `0x0000010d`.
- Added safer numeric parsing for shell commands that accept PIDs or run counts.
- Hardened process initialization, wait handling, address-space generation, and deadlock reporting.
- Hid raw process address-space pointers from user-visible process info.
- Added size limits for syscall read/write requests.
- Tightened VFS mount IDs, root unmount behavior, ramfs inode allocation, and ramfs growth checks.
- Added small driver and timer guardrails around PIT, serial, ATA, and block I/O paths.

## 0.0.1.12

Rust boundary reproducibility fix over the Stage 9 hardening baseline.

- Removed dependency on hashed internal `core::panicking::panic_bounds_check::*` symbols.
- Reworked Rust VFS route and path-policy code so checked indexing is not emitted into the freestanding kernel object.
- Added `scripts/check_rust_no_panic_bounds.py`.
- Kept Rust source compatible with Rust 2021 syntax.

## 0.0.1.11

Second Stage 9 hardening pass.

- Restored Rust 2021-compatible exported-symbol attributes.
- Tightened EXT4 descriptor and inode handling.
- Made tar parsing stricter.
- Added allocator overflow and double-free checks.
- Prevented VMM permission promotion through existing page-table entries.
- Locked shared log, keyboard, and devfs PRNG state.
- Tightened scheduler wait semantics.
- Validated ELF entry placement.
- Added IST stacks for critical exceptions.

## 0.0.1.10

Stage 9 hardening and security pass.

- Added overflow checks across ramfs, tarfs, EXT4, MBR, block I/O, syscall buffers, VMM mapping, and heap operations.
- Fixed ATA PIO LBA48 handling for 256-sector requests.
- Rejected duplicate VMM mappings and overlapping user ELF W+X pages.
- Fixed scheduler terminal-slot reuse after process history publication.
- Routed Rust panic and bounds failures into kernel panic diagnostics.

## 0.0.1.9

Stage 9 process image replacement.

- Added `exec` and `execv` syscall IDs.
- Added Rust-side syscall validation and dispatch for `exec` and `execv`.
- Added a safe prepare-then-commit replacement path for process images.
- Added `/bin/execcheck`.
- Kept PID identity across successful exec.
- Made failed exec non-destructive.

## 0.0.1.8

Stage 8 scheduler return-path hardening.

- Fixed heap coalescing for non-contiguous heap extension blocks.
- Made async process context publication resilient to a lost `current_async_slot`.
- Hardened scheduler, preemption, process wait, and post-ktest idle paths.

## 0.0.1.3 to 0.0.1.6

Stage 8 preemptive scheduling and runtime stabilization.

- Added persistent READY, RUNNING, SLEEPING, and WAITING process states.
- Captured and restored ring3 interrupt frames.
- Wired PIT IRQ0 into single-core preemption.
- Added async `spawn`, blocking `wait`, `sleep`, `yield`, and deep-copy `fork`.
- Fixed ring3 `SYS_EXIT`, shell idle interrupt handling, and wait lifecycle issues.

## 0.0.1.1 to 0.0.1.2

Process, fd, and argv groundwork.

- Replaced stage-number versioning with `major.minor.patch.fix`.
- Added fd-oriented syscalls such as `dup`, `tell`, `fstat`, `fdinfo`, and `readdir`.
- Added argv-aware process spawning through the Rust syscall boundary.

## Earlier stage work

- Repaired BIOS boot memory layout issues around E820 and bootstrap page tables.
- Added stage1/stage2 COM1 diagnostics.
- Added linker checks for boot-sector and stage2 size limits.
- Built the VFS, ramfs, devfs, tarfs, read-only EXT4 path, ATA PIO, MBR parsing, and installer-generated seed filesystem.
- Added the first ring3 userland path with GDT/TSS setup, user selectors, syscall gate, ELF loading, and user stack mapping.
- Linked Rust boundary modules into the kernel for high-risk parsing and syscall edges.
