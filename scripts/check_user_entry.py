#!/usr/bin/env python3
import pathlib
import re
import sys

path = pathlib.Path(sys.argv[1])
text = path.read_text()
required = [
    "arch_user_saved_rsp",
    "arch_user_saved_rbp",
    "arch_user_saved_rbx",
    "arch_user_saved_r12",
    "arch_user_saved_r13",
    "arch_user_saved_r14",
    "arch_user_saved_r15",
]
missing = [s for s in required if s not in text]
if missing:
    raise SystemExit(f"missing saved register symbols: {', '.join(missing)}")
for reg in ["rbp", "rbx", "r12", "r13", "r14", "r15"]:
    save = re.search(rf"mov\s+\[rip \+ arch_user_saved_{reg}\],\s*{reg}", text)
    restore = re.search(rf"mov\s+{reg},\s*\[rip \+ arch_user_saved_{reg}\]", text)
    if not save or not restore:
        raise SystemExit(f"arch_user_enter does not preserve {reg}")
print("user entry register preservation checks passed")
