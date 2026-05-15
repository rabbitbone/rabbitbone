# Contributing to Rabbitbone

Thanks for taking an interest in Rabbitbone. This is a small experimental operating system, so focused changes with clear validation are the easiest to review.

## Before You Start

- Check existing issues and discussions before opening duplicate work.
- Keep changes scoped to one topic.
- Prefer small pull requests over broad rewrites.
- Do not commit generated build output, VM runtime state, logs, or local tool caches.

## Development Setup

Install the host tools listed in the README, then run:

```sh
make clean
make
make test
```

For image work, also run:

```sh
make image
```

## Pull Requests

Good pull requests include:

- A short explanation of the behavior changed.
- The commands you ran to validate the change.
- Notes about any boot, filesystem, ABI, or userland compatibility impact.
- Screenshots or logs only when they clarify a runtime behavior.

## Release Version Changes

Version bumps must keep these files in sync:

- `include/rabbitbone/version.h`
- `README.md`
- `docs/STATUS.md`
- `docs/RELEASES.md`

Validate the release markers with:

```sh
python3 scripts/check_release_version.py
```

## Coding Notes

- Follow the surrounding C, C++, Rust, and assembly style.
- Keep kernel and userland ABI changes explicit.
- Add or update host tests and `ktest` coverage when behavior changes.
- Prefer deterministic diagnostics over ad hoc logging.
