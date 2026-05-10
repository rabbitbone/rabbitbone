# AuroraOS 0.0.1.10

AuroraOS 0.0.1.10 is a Stage 9 hardening/security release. It keeps the 0.0.1.9 `exec/execv` process-image baseline and fixes overflow, corrupted-image and scheduler-lifecycle bugs found during review.

## Fixed bugs

- ramfs rejects huge offsets and write-size overflows instead of growing forever or wrapping capacity.
- ATA PIO LBA48 reads encode 256-sector requests correctly as `0x0100`, not as an accidental 65536-sector command.
- Rust VFS route returns `/` for exact mount roots such as `/disk0` and `/dev`.
- EXT4 mount/read paths now validate block-size shifts, first data block, inode size, group descriptor size, extent depth, extent node capacity and checked arithmetic.
- tarfs rejects zero checksums and truncated payloads.
- block read range checks are overflow-safe.
- ELF PT_LOAD page-flag merging rejects writable+executable overlaps instead of producing RWX user pages.
- Kernel identity map marks the low direct-map area above the bootstrap/kernel window as NX while full text/rodata split remains future work.
- Scheduler runqueue terminal slots are reusable after jobs are pushed into history.
- VMM mapping functions reject duplicate mappings instead of silently replacing present entries.
- E820 accounting uses checked/saturating arithmetic.
- bootinfo validation checks E820 array range and count bounds.
- MBR Linux partition selection validates the partition range against the block device.
- EXT4 VFS mount frees its context if VFS mount registration fails.
- ramfs and VFS file creation reject nonzero sizes with null data pointers.
- Root VFS listing propagates backend errors.
- syscall read/write/console/argv pointer arithmetic is checked before usercopy.
- readdir clears the output record on EOF.
- krealloc shrink now splits and frees the tail block.
- fork address-space clone releases a page if metadata tracking fails after mapping.
- Rust panic/bounds failures route to the kernel panic path instead of spinning silently.

## Regression tests

New host and kernel self-tests cover:

- huge ramfs seek/write rejection;
- exact mount-root VFS routing;
- overflow-safe block range checks;
- zero-checksum and truncated tar images;
- corrupt EXT4 superblock/group/extent inputs;
- scheduler reuse after more than 32 completed jobs;
- duplicate VMM mapping rejection.

## ABI

- Version text: `0.0.1.10`
- Syscall ABI: `0x0000010a`

## Notes

`/dev/random` is still a deterministic test PRNG seeded from timer state. It is not a cryptographic random source.

Freestanding stack-protector/canary support is not enabled yet; that remains tied to a later TLS/canary runtime layer.
