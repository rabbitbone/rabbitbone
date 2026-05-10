#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 1 ]]; then
  echo "usage: $0 /path/to/extracted/rust-1.94.1-x86_64-unknown-linux-gnu [make-target...]" >&2
  exit 2
fi
ROOT="$1"
shift
if [[ $# -eq 0 ]]; then
  set -- image
fi
export LD_LIBRARY_PATH="$ROOT/rustc/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec make RUSTC="$ROOT/rustc/bin/rustc" RUST_SYSROOT="$ROOT/rust-std-x86_64-unknown-linux-gnu" "$@"
