#!/usr/bin/env bash
set -euo pipefail
usage() {
  echo "usage: $0 [--sha256 HEX] /path/to/extracted/rust-1.94.1-x86_64-unknown-linux-gnu [make-target...]" >&2
  echo "       or place SHA256SUMS in the toolchain root and signed/verified externally before use" >&2
  exit 2
}
EXPECTED_SHA="${RABBITBONE_RUSTC_SHA256:-}"
if [[ $# -ge 2 && "$1" == "--sha256" ]]; then
  EXPECTED_SHA="$2"
  shift 2
fi
if [[ $# -lt 1 ]]; then
  usage
fi
ROOT="$1"
shift
if [[ $# -eq 0 ]]; then
  set -- image
fi
RUSTC="$ROOT/rustc/bin/rustc"
SYSROOT="$ROOT/rust-std-x86_64-unknown-linux-gnu"
if [[ ! -x "$RUSTC" || ! -d "$SYSROOT" || ! -d "$ROOT/rustc/lib" ]]; then
  echo "invalid uploaded Rust toolchain layout: $ROOT" >&2
  exit 1
fi
if [[ -n "$EXPECTED_SHA" ]]; then
  if [[ ! "$EXPECTED_SHA" =~ ^[0-9a-fA-F]{64}$ ]]; then
    echo "invalid rustc SHA-256 digest: expected 64 hex characters" >&2
    exit 2
  fi
  actual="$(sha256sum "$RUSTC" | awk '{print $1}')"
  if [[ "$actual" != "$EXPECTED_SHA" ]]; then
    echo "rustc checksum mismatch: expected $EXPECTED_SHA got $actual" >&2
    exit 1
  fi
elif [[ -f "$ROOT/SHA256SUMS" ]]; then
  if ! awk '
    /^[[:xdigit:]]{64}[[:space:]][ *]?/ {
      name = $0
      sub(/^[[:xdigit:]]{64}[[:space:]][ *]?/, "", name)
      if (name ~ /^\// || name ~ /(^|\/)\.\.($|\/)/) exit 1
      next
    }
    { exit 1 }
  ' "$ROOT/SHA256SUMS"; then
    echo "refusing malformed SHA256SUMS or entries with absolute/parent-relative paths" >&2
    exit 1
  fi
  (cd "$ROOT" && sha256sum -c SHA256SUMS)
else
  echo "refusing to execute uploaded Rust toolchain without checksum verification" >&2
  echo "provide --sha256 for rustc or a SHA256SUMS file in the toolchain root" >&2
  exit 1
fi
export LD_LIBRARY_PATH="$ROOT/rustc/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec make RUSTC="$RUSTC" RUST_SYSROOT="$SYSROOT" "$@"
