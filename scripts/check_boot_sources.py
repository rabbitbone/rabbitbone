#!/usr/bin/env python3
import re
import sys
from pathlib import Path

BAD_IMMEDIATE_RE = re.compile(
    r"^\s*mov\s+(si|di|esi|edi|rsi|rdi|eax|rax)\s*,\s*(?!offset\b)(msg_[A-Za-z0-9_]*|dap\b|bootinfo\b|pml4\b)",
    re.IGNORECASE,
)

errors = []
for arg in sys.argv[1:]:
    path = Path(arg)
    text = path.read_text()
    if path.name == "stage2.S" and "1 << 11" not in text:
        errors.append(f"{path}: stage2 must enable EFER.NXE before any NX PTEs can be used")
    for no, line in enumerate(text.splitlines(), 1):
        code = line.split("#", 1)[0].strip()
        if not code:
            continue
        if BAD_IMMEDIATE_RE.match(code):
            errors.append(f"{path}:{no}: use 'offset <symbol>' for immediate symbol addresses in GAS Intel syntax: {line.strip()}")

if errors:
    for err in errors:
        print(err, file=sys.stderr)
    sys.exit(1)
print("boot source address checks passed")
