# AuroraOS 0.0.1.6

AuroraOS 0.0.1.6 is a Stage 8 runtime hardening release focused on the VMware post-ktest idle/freeze report and the remaining async wait lifecycle failures.

## Fixed

- The shell idle loop now explicitly enables interrupts before `hlt`, so returning to the CLI after `ktest` cannot leave the guest halted with IF cleared.
- Blocking `wait` no longer rewinds and replays the `int 0x80` instruction. Waiters now store the userspace result pointer in their process slot and are completed by the scheduler when the child reaches a terminal state.
- `wake_waiters()` now copies `process_info_t` directly into the blocked process address space and sets the saved syscall return registers before making the waiter runnable.
- Removed the remaining userland `hlt` fallback from `/bin/regtrash`; failed exit fallback now uses `pause`, not CPU halt.

## Version

- Version text: `0.0.1.6`
- Syscall ABI: `0x00000106`

## Validation

Built with the uploaded pinned Rust toolchain:

```sh
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 test
scripts/build_with_uploaded_rust.sh /mnt/data/rust_toolchain -j2 image
```

Host contract checks and image generation pass. Runtime VMware `ktest` should now return to a responsive CLI instead of entering a dead halted idle state after the prompt.
