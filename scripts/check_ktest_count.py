#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
KTEST_ROOT = ROOT / "kernel" / "core" / "ktest"
MIN_CHECKS = 445
MIN_SUITES = 11


def strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = re.sub(r"//.*", "", src)
    return src


def main() -> int:
    files = sorted(KTEST_ROOT.glob("*.inc")) + sorted((KTEST_ROOT / "ext4_disk").glob("*.inc"))
    checks = 0
    skips = 0
    suites = 0
    per_file = []
    for path in files:
        data = strip_comments(path.read_text(encoding="utf-8"))
        c = len(re.findall(r"\bcheck\s*\(", data))
        s = len(re.findall(r"\bskip\s*\(", data))
        q = len(re.findall(r"\bsuite_begin\s*\(", data))
        if c or s or q:
            per_file.append((path.relative_to(ROOT).as_posix(), c, s, q))
        checks += c
        skips += s
        suites += q
    if checks < MIN_CHECKS:
        raise SystemExit(f"ktest-count: expected at least {MIN_CHECKS} check() sites, found {checks}")
    if suites < MIN_SUITES:
        raise SystemExit(f"ktest-count: expected at least {MIN_SUITES} suites, found {suites}")
    if not any(name.endswith("tarfs_tests.inc") and c >= 15 for name, c, _s, _q in per_file):
        raise SystemExit("ktest-count: tarfs suite lost final archive policy coverage")
    if not any(name.endswith("hardening_regression_tests.inc") and c >= 38 and q >= 1 for name, c, _s, q in per_file):
        raise SystemExit("ktest-count: hardening regression suite lost boundary/path/log coverage")
    print(f"ktest-count: checks={checks} skip_sites={skips} suites={suites}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
