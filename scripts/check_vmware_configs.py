#!/usr/bin/env python3
from __future__ import annotations

import pathlib
import re
import sys

VALID_PREFIXES = (
    "ide", "sata", "scsi", "nvme", "serial", "floppy", "usb", "sound",
    "ethernet", "display", "guest", "mem", "num", "firmware", "uefi",
    "bios", "config", "virtualHW", "extendedConfigFile", "svga",
    ".encoding", "displayName", "tools", "isolation", "mks",
)

DEVICE_RE = re.compile(r'^(?P<prefix>[A-Za-z]+)\d+(?::\d+)?\.')
ASSIGN_RE = re.compile(r'^\s*([^#][^=]+?)\s*=\s*"(.*)"\s*$')


def parse(path: pathlib.Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = ASSIGN_RE.match(line)
        if not m:
            raise SystemExit(f"{path}:{lineno}: invalid VMX assignment: {raw!r}")
        key = m.group(1).strip()
        value = m.group(2)
        if key in values:
            raise SystemExit(f"{path}:{lineno}: duplicate VMX key {key!r}")
        if not any(key.startswith(prefix) for prefix in VALID_PREFIXES):
            raise SystemExit(f"{path}:{lineno}: unexpected VMX key prefix in {key!r}")
        values[key] = value
        dm = DEVICE_RE.match(key)
        if dm:
            prefix = dm.group("prefix")
            if prefix not in {"ide", "sata", "scsi", "nvme", "serial", "floppy", "usb", "sound", "ethernet", "svga"}:
                raise SystemExit(f"{path}:{lineno}: unknown VMware device prefix {prefix!r}; expected ide/sata/scsi/nvme/etc")
    return values


def check_uefi_live(path: pathlib.Path) -> None:
    values = parse(path)
    if values.get("firmware") != "efi":
        raise SystemExit(f"{path}: firmware must be efi")
    if values.get("uefi.secureBoot.enabled") != "FALSE":
        raise SystemExit(f"{path}: Secure Boot must be disabled")

    cd_keys = [k for k, v in values.items() if k.endswith(".deviceType") and v == "cdrom-image"]
    if not cd_keys:
        raise SystemExit(f"{path}: no cdrom-image device found")
    for key in cd_keys:
        dev = key[: -len(".deviceType")]
        filename = values.get(f"{dev}.fileName", "")
        if not filename:
            raise SystemExit(f"{path}: {dev} has no fileName")
        if filename.replace("\\", "/") == "build/rabbitbone-live.iso":
            raise SystemExit(f"{path}: {dev}.fileName is relative to vmware/, use ../build/rabbitbone-live.iso")
        if "rabbitbone-live.iso" in filename and not filename.replace("\\", "/").endswith("../build/rabbitbone-live.iso"):
            raise SystemExit(f"{path}: expected {dev}.fileName to be ../build/rabbitbone-live.iso, got {filename!r}")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        raise SystemExit("usage: check_vmware_configs.py <vmx-or-example> [...]")
    for name in argv[1:]:
        path = pathlib.Path(name)
        check_uefi_live(path)
    print("vmware config check: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
