#!/usr/bin/env python3
import argparse
import math
import os
import stat
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple

from rabbitbone_version import load_version

SECTOR = 2048
FAT_SECTOR = 512
FAT_CLUSTER_SECTORS = 8
FAT_ROOT_ENTRIES = 512
FAT_RESERVED_SECTORS = 1
FAT_COUNT = 2
FAT_MEDIA = 0xF8


def ceil_div(value: int, divisor: int) -> int:
    return (value + divisor - 1) // divisor


def pad(data: bytes, multiple: int) -> bytes:
    return data + b"\0" * ((multiple - len(data) % multiple) % multiple)


def le16(v: int) -> bytes:
    return int(v).to_bytes(2, "little")


def be16(v: int) -> bytes:
    return int(v).to_bytes(2, "big")


def le32(v: int) -> bytes:
    return int(v).to_bytes(4, "little")


def be32(v: int) -> bytes:
    return int(v).to_bytes(4, "big")


def both16(v: int) -> bytes:
    return le16(v) + be16(v)


def both32(v: int) -> bytes:
    return le32(v) + be32(v)


FAT_NAME_ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`^")
FAT_LABEL_ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@'`^ ")
ISO_VOLUME_ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
FAT_MAX_CLUSTER = 0xFFEF
MAX_INPUT_BYTES = 128 * 1024 * 1024
KERNEL_LOAD_BASE = 0x10000
KERNEL_LOW_LIMIT = 0x9F000
KERNEL_MAX_BYTES = KERNEL_LOW_LIMIT - KERNEL_LOAD_BASE


def default_iso_volume_id() -> str:
    return load_version().iso_volume_id


def default_fat_serial() -> int:
    return load_version().fat_serial


def upper_83(name: str) -> bytes:
    base, dot, ext = name.partition(".")
    base = base.upper()
    ext = ext.upper() if dot else ""
    if not base or len(base) > 8 or len(ext) > 3:
        raise ValueError(f"not an 8.3 name: {name}")
    if any(c not in FAT_NAME_ALLOWED for c in base + ext):
        raise ValueError(f"unsupported FAT name: {name}")
    return base.encode("ascii").ljust(8, b" ") + ext.encode("ascii").ljust(3, b" ")


def fat_long_name_supported(name: str) -> bool:
    try:
        name.encode("ascii")
    except UnicodeEncodeError:
        return False
    if not name or len(name) > 255 or name[0] == "." or name[-1] in (" ", "."):
        return False
    return all((c in FAT_NAME_ALLOWED) or c == "." for c in name.upper())


def fat_lfn_checksum(short_name: bytes) -> int:
    if len(short_name) != 11:
        raise ValueError("short name must be exactly 11 bytes")
    value = 0
    for b in short_name:
        value = (((value & 1) << 7) + (value >> 1) + b) & 0xFF
    return value


def fat_lfn_entries(long_name: str, short_name: bytes) -> List[bytes]:
    if not fat_long_name_supported(long_name):
        raise ValueError(f"unsupported FAT long name: {long_name!r}")
    # The builder stores deterministic uppercase names. That is enough for UEFI
    # SimpleFS lookup while avoiding locale/case-folding ambiguity.
    units = [ord(c) for c in long_name] + [0]
    while len(units) % 13:
        units.append(0xFFFF)
    checksum = fat_lfn_checksum(short_name)
    entries: List[bytes] = []
    chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
    for idx in range(len(chunks) - 1, -1, -1):
        seq = idx + 1
        if idx == len(chunks) - 1:
            seq |= 0x40
        e = bytearray(32)
        e[0] = seq
        e[11] = 0x0F
        e[12] = 0
        e[13] = checksum
        e[26:28] = b"\0\0"
        pos = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
        for off, unit in zip(pos, chunks[idx]):
            e[off:off + 2] = le16(unit)
        entries.append(bytes(e))
    return entries


def fat_sanitized_short_parts(name: str) -> Tuple[str, str]:
    base, dot, ext = name.partition(".")
    base = "".join(c if c in FAT_NAME_ALLOWED else "_" for c in base.upper())
    ext = "".join(c if c in FAT_NAME_ALLOWED else "_" for c in (ext.upper() if dot else ""))
    base = base or "RB"
    return base, ext


def fat_short_alias(name: str, used: set[bytes]) -> bytes:
    try:
        direct = upper_83(name)
        if direct not in used:
            return direct
    except ValueError:
        pass
    base, ext = fat_sanitized_short_parts(name)
    ext = ext[:3]
    for n in range(1, 1000000):
        suffix = "~" + str(n)
        prefix_len = max(1, 8 - len(suffix))
        alias = (base[:prefix_len] + suffix).encode("ascii").ljust(8, b" ") + ext.encode("ascii").ljust(3, b" ")
        if alias not in used:
            return alias
    raise RuntimeError(f"could not allocate FAT 8.3 alias for {name!r}")


def validate_iso_volume_id(volume_id: str) -> str:
    if not volume_id:
        raise ValueError("volume id must be non-empty")
    try:
        encoded = volume_id.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ValueError("volume id must be ASCII") from exc
    if len(encoded) > 32:
        raise ValueError("volume id must be at most 32 ASCII bytes")
    upper = volume_id.upper()
    bad = [c for c in upper if c not in ISO_VOLUME_ALLOWED]
    if bad:
        raise ValueError(f"volume id contains unsupported ISO-9660 character: {bad[0]!r}")
    return upper


def fat_volume_label(volume_label: str) -> bytes:
    # ISO volume identifiers can be up to 32 bytes, while FAT volume labels are
    # a raw 11-byte directory field, not a normal 8.3 filename. Keep builds
    # deterministic and permissive by sanitizing the configured ISO id into a
    # valid FAT label instead of rejecting long release-derived identifiers.
    sanitized = []
    for c in volume_label.upper().replace("-", "_"):
        sanitized.append(c if c in FAT_LABEL_ALLOWED else "_")
    label = "".join(sanitized).strip()[:11]
    if not label:
        label = "RABBITBONE"
    return label.encode("ascii").ljust(11, b" ")


def fat_datetime() -> Tuple[int, int]:
    # 2026-05-14 00:00:00, deterministic build metadata.
    date = ((2026 - 1980) << 9) | (5 << 5) | 14
    time = 0
    return time, date


class FatBuilder:
    def __init__(self) -> None:
        self.files: Dict[str, bytes] = {}
        self.dirs = {"/", "/EFI", "/EFI/BOOT", "/RABBITBONE"}

    def add_file(self, path: str, data: bytes) -> None:
        if not path.startswith("/"):
            raise ValueError(f"FAT path must be absolute: {path!r}")
        parts = path.strip("/").split("/")
        if not parts or any(part in ("", ".", "..") for part in parts):
            raise ValueError(f"FAT path must be normalized: {path!r}")
        normalized_parts = []
        for part in parts:
            upper = part.upper()
            if not fat_long_name_supported(upper):
                raise ValueError(f"unsupported FAT path component: {part!r}")
            normalized_parts.append(upper)
        normalized = "/" + "/".join(normalized_parts)
        if normalized in self.files or normalized in self.dirs:
            raise ValueError(f"duplicate FAT path: {normalized}")
        parent_parts = normalized_parts[:-1]
        cur = ""
        for part in parent_parts:
            cur += "/" + part
            if cur in self.files:
                raise ValueError(f"FAT parent path conflicts with file: {cur}")
            self.dirs.add(cur)
        self.files[normalized] = data

    def build(self, volume_label: str, fat_serial: int) -> bytes:
        label = fat_volume_label(volume_label)

        def parent_of_static(path: str) -> str:
            parts = path.strip("/").split("/")
            if len(parts) <= 1:
                return "/"
            return "/" + "/".join(parts[:-1])

        def leaf_static(path: str) -> str:
            return path.strip("/").split("/")[-1]

        dir_entries: Dict[str, List[bytes]] = {d: [] for d in self.dirs}
        dir_cluster: Dict[str, int] = {}
        file_clusters: Dict[str, Tuple[int, int]] = {}
        cluster_payloads: List[bytes] = []

        child_paths: Dict[str, List[str]] = {d: [] for d in self.dirs}
        for d in sorted(self.dirs):
            if d != "/":
                child_paths[parent_of_static(d)].append(d)
        for path in sorted(self.files):
            child_paths.setdefault(parent_of_static(path), []).append(path)
        short_names: Dict[str, bytes] = {}
        for parent, children in child_paths.items():
            used: set[bytes] = set()
            for child in sorted(children):
                short_names[child] = fat_short_alias(leaf_static(child), used)
                used.add(short_names[child])

        def alloc_cluster_chain(data: bytes) -> Tuple[int, int]:
            if not data:
                return 0, 0
            cluster_size = FAT_CLUSTER_SECTORS * FAT_SECTOR
            first = 2 + len(cluster_payloads)
            chunks = [data[i:i + cluster_size] for i in range(0, len(data), cluster_size)]
            for chunk in chunks:
                cluster_payloads.append(chunk.ljust(cluster_size, b"\0"))
            return first, len(chunks)

        for d in sorted(self.dirs):
            if d != "/":
                dir_cluster[d], _ = alloc_cluster_chain(b"")
                # Replace zero-length allocation with an explicit empty directory cluster.
                if dir_cluster[d] == 0:
                    dir_cluster[d] = 2 + len(cluster_payloads)
                    cluster_payloads.append(b"\0" * (FAT_CLUSTER_SECTORS * FAT_SECTOR))

        for path, data in sorted(self.files.items()):
            file_clusters[path] = alloc_cluster_chain(data)

        def make_short_entry(short_name: bytes, attr: int, first_cluster: int, size: int) -> bytes:
            if len(short_name) != 11:
                raise ValueError("short FAT name must be exactly 11 bytes")
            tm, dt = fat_datetime()
            e = bytearray(32)
            e[0:11] = short_name
            e[11] = attr
            e[14:16] = le16(tm)
            e[16:18] = le16(dt)
            e[18:20] = le16(dt)
            e[22:24] = le16(tm)
            e[24:26] = le16(dt)
            e[26:28] = le16(first_cluster)
            e[28:32] = le32(size)
            return bytes(e)

        def make_entry(name: str, attr: int, first_cluster: int, size: int) -> bytes:
            if name == ".":
                return make_short_entry(b".          ", attr, first_cluster, size)
            if name == "..":
                return make_short_entry(b"..         ", attr, first_cluster, size)
            return make_short_entry(upper_83(name), attr, first_cluster, size)

        def make_named_entries(name: str, short_name: bytes, attr: int, first_cluster: int, size: int) -> List[bytes]:
            try:
                direct = upper_83(name)
            except ValueError:
                direct = b""
            out: List[bytes] = []
            if direct != short_name:
                out.extend(fat_lfn_entries(name, short_name))
            out.append(make_short_entry(short_name, attr, first_cluster, size))
            return out

        def parent_of(path: str) -> str:
            parts = path.strip("/").split("/")
            if len(parts) <= 1:
                return "/"
            return "/" + "/".join(parts[:-1])

        def leaf(path: str) -> str:
            return path.strip("/").split("/")[-1]

        for d in sorted(self.dirs):
            if d == "/":
                continue
            dir_entries[parent_of(d)].extend(make_named_entries(leaf(d), short_names[d], 0x10, dir_cluster[d], 0))

        for path, data in sorted(self.files.items()):
            first, _ = file_clusters[path]
            dir_entries[parent_of(path)].extend(make_named_entries(leaf(path), short_names[path], 0x20, first, len(data)))

        # Fill directory cluster payloads after all entries are known.
        cluster_size = FAT_CLUSTER_SECTORS * FAT_SECTOR
        for d, first in dir_cluster.items():
            entries = []
            entries.append(make_entry(".", 0x10, first, 0))
            parent = parent_of(d)
            entries.append(make_entry("..", 0x10, 0 if parent == "/" else dir_cluster[parent], 0))
            entries.extend(dir_entries[d])
            payload = b"".join(entries)
            if len(payload) > cluster_size:
                raise RuntimeError(f"FAT directory {d} is too large: {len(payload)} bytes")
            cluster_payloads[first - 2] = payload.ljust(cluster_size, b"\0")

        data_clusters = len(cluster_payloads)
        fat_entries = data_clusters + 2
        fat_sectors = ceil_div(fat_entries * 2, FAT_SECTOR)
        root_dir_sectors = ceil_div(FAT_ROOT_ENTRIES * 32, FAT_SECTOR)
        data_start = FAT_RESERVED_SECTORS + FAT_COUNT * fat_sectors + root_dir_sectors
        total_sectors = data_start + data_clusters * FAT_CLUSTER_SECTORS
        if fat_entries > FAT_MAX_CLUSTER:
            raise RuntimeError(f"FAT image needs too many clusters: {fat_entries}")
        if fat_sectors > 0xFFFF:
            raise RuntimeError(f"FAT table is too large: {fat_sectors} sectors")

        fat = bytearray(fat_sectors * FAT_SECTOR)
        fat[0:2] = le16(0xFFF8)
        fat[2:4] = le16(0xFFFF)
        used = [False] * data_clusters
        # Directories are single-cluster chains.
        for first in dir_cluster.values():
            used[first - 2] = True
            fat[first * 2:first * 2 + 2] = le16(0xFFFF)
        for _path, (first, count) in file_clusters.items():
            for idx in range(count):
                cl = first + idx
                used[cl - 2] = True
                nxt = 0xFFFF if idx + 1 == count else cl + 1
                fat[cl * 2:cl * 2 + 2] = le16(nxt)
        for idx, u in enumerate(used):
            if not u:
                fat[(idx + 2) * 2:(idx + 2) * 2 + 2] = le16(0x0000)

        volume_entry = bytearray(make_entry("RBBONE", 0x08, 0, 0))
        volume_entry[0:11] = label
        root_entries = [bytes(volume_entry)]
        root_entries.extend(dir_entries["/"])
        root_payload = b"".join(root_entries)
        if len(root_payload) > root_dir_sectors * FAT_SECTOR:
            raise RuntimeError(f"FAT root directory is too large: {len(root_payload)} bytes")
        root_dir = root_payload.ljust(root_dir_sectors * FAT_SECTOR, b"\0")

        boot = bytearray(FAT_SECTOR)
        boot[0:3] = b"\xEB\x3C\x90"
        oem_name = b"RBBONE15"
        if len(oem_name) != 8:
            raise RuntimeError("FAT OEM name must be exactly 8 bytes")
        boot[3:11] = oem_name
        boot[11:13] = le16(FAT_SECTOR)
        boot[13] = FAT_CLUSTER_SECTORS
        boot[14:16] = le16(FAT_RESERVED_SECTORS)
        boot[16] = FAT_COUNT
        boot[17:19] = le16(FAT_ROOT_ENTRIES)
        if total_sectors < 0x10000:
            boot[19:21] = le16(total_sectors)
        else:
            boot[19:21] = le16(0)
            boot[32:36] = le32(total_sectors)
        boot[21] = FAT_MEDIA
        boot[22:24] = le16(fat_sectors)
        boot[24:26] = le16(63)
        boot[26:28] = le16(255)
        boot[28:32] = le32(0)
        boot[36] = 0x80
        boot[38] = 0x29
        boot[39:43] = le32(fat_serial)
        boot[43:54] = label
        boot[54:62] = b"FAT16   "
        boot[510:512] = b"\x55\xAA"
        if len(boot) != FAT_SECTOR:
            raise RuntimeError(f"FAT boot sector size changed to {len(boot)} bytes")

        image = bytes(boot)
        image += bytes(fat) * FAT_COUNT
        image += root_dir
        image += b"".join(cluster_payloads)
        image = pad(image, FAT_SECTOR)
        if len(image) != total_sectors * FAT_SECTOR:
            raise RuntimeError("internal FAT size mismatch")
        return image


def iso_dir_record(extent: int, size: int, flags: int, name: bytes) -> bytes:
    dt = bytes([126, 5, 14, 0, 0, 0, 0])
    body = bytearray()
    body += b"\0"  # extended attribute length
    body += both32(extent)
    body += both32(size)
    body += dt
    body += bytes([flags, 0, 0])
    body += both16(1)
    body += bytes([len(name)]) + name
    rec_len = 1 + len(body)
    if rec_len % 2:
        body += b"\0"
        rec_len += 1
    return bytes([rec_len]) + bytes(body)


def build_iso(fat_image: bytes, volume_id: str) -> bytes:
    fat_image = pad(fat_image, SECTOR)
    boot_image_sectors = len(fat_image) // SECTOR
    pvd_lba = 16
    boot_record_lba = 17
    terminator_lba = 18
    path_table_lba = 19
    root_dir_lba = 20
    boot_catalog_lba = 21
    boot_image_lba = 22
    total_sectors = boot_image_lba + boot_image_sectors

    root_record = iso_dir_record(root_dir_lba, SECTOR, 0x02, b"\x00")

    pvd = bytearray(SECTOR)
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"RABBITBONEOS".ljust(32)
    pvd[40:72] = volume_id.upper().encode("ascii")[:32].ljust(32)
    pvd[80:88] = both32(total_sectors)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    path_table = bytes([1, 0]) + le32(root_dir_lba) + le16(1)
    path_table = pad(path_table, SECTOR)
    pvd[132:140] = both32(len(path_table.rstrip(b"\0")))
    pvd[140:144] = le32(path_table_lba)
    pvd[148:152] = be32(path_table_lba)
    pvd[156:156 + len(root_record)] = root_record
    pvd[813:830] = b"20260514000000000"
    pvd[830] = 0
    pvd[881] = 1

    boot_record = bytearray(SECTOR)
    boot_record[0] = 0
    boot_record[1:6] = b"CD001"
    boot_record[6] = 1
    boot_record[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\0")
    boot_record[71:75] = le32(boot_catalog_lba)

    terminator = bytearray(SECTOR)
    terminator[0] = 255
    terminator[1:6] = b"CD001"
    terminator[6] = 1

    root_dir = bytearray(SECTOR)
    entries = [iso_dir_record(root_dir_lba, SECTOR, 0x02, b"\x00"), iso_dir_record(root_dir_lba, SECTOR, 0x02, b"\x01")]
    off = 0
    for e in entries:
        root_dir[off:off + len(e)] = e
        off += len(e)

    validation = bytearray(32)
    validation[0] = 1
    validation[1] = 0xEF
    validation[4:28] = b"RABBITBONEOS UEFI".ljust(24, b"\0")
    validation[30] = 0x55
    validation[31] = 0xAA
    checksum = sum(int.from_bytes(validation[i:i + 2], "little") for i in range(0, 32, 2)) & 0xFFFF
    validation[28:30] = le16((-checksum) & 0xFFFF)

    initial = bytearray(32)
    initial[0] = 0x88
    initial[1] = 0x00
    initial[2:4] = le16(0)
    initial[4] = 0
    initial[6:8] = le16(1)
    initial[8:12] = le32(boot_image_lba)
    boot_catalog = bytes(validation) + bytes(initial)
    boot_catalog = boot_catalog.ljust(SECTOR, b"\0")

    sectors = [b"\0" * SECTOR for _ in range(16)]
    sectors += [bytes(pvd), bytes(boot_record), bytes(terminator), path_table, bytes(root_dir), boot_catalog, fat_image]
    return b"".join(sectors)


def read_regular_file(path: Path, label: str) -> bytes:
    try:
        st = path.stat(follow_symlinks=False)
    except OSError as exc:
        raise SystemExit(f"cannot stat {label} {path}: {exc}") from exc
    if stat.S_ISLNK(st.st_mode):
        raise SystemExit(f"refusing to read symlink {label}: {path}")
    if not stat.S_ISREG(st.st_mode):
        raise SystemExit(f"refusing to read non-regular {label}: {path}")
    if st.st_size <= 0:
        raise SystemExit(f"{label} must be non-empty: {path}")
    if st.st_size > MAX_INPUT_BYTES:
        raise SystemExit(f"{label} is too large: {path} has {st.st_size} bytes, limit is {MAX_INPUT_BYTES}")
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise SystemExit(f"cannot read {label} {path}: {exc}") from exc
    if len(data) != st.st_size:
        raise SystemExit(f"{label} changed while being read: {path}")
    return data


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp_name = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=str(path.parent or Path(".")))
    try:
        with os.fdopen(fd, "wb") as tmp:
            tmp.write(data)
            tmp.flush()
            os.fsync(tmp.fileno())
        os.replace(tmp_name, path)
        try:
            dir_fd = os.open(path.parent or Path("."), os.O_RDONLY)
        except OSError:
            dir_fd = -1
        if dir_fd >= 0:
            try:
                os.fsync(dir_fd)
            finally:
                os.close(dir_fd)
    finally:
        try:
            os.unlink(tmp_name)
        except FileNotFoundError:
            pass


def main() -> int:
    ap = argparse.ArgumentParser(description="Build Rabbitbone UEFI live ISO with an embedded FAT ESP")
    ap.add_argument("--out", required=True)
    ap.add_argument("--efi", required=True)
    ap.add_argument("--kernel", required=True)
    ap.add_argument("--root", required=True)
    ap.add_argument("--volume-id", default=default_iso_volume_id())
    ap.add_argument("--fat-serial", default=f"0x{default_fat_serial():08X}", help="FAT volume serial, defaults to version.h-derived value")
    args = ap.parse_args()

    efi = read_regular_file(Path(args.efi), "EFI image")
    kernel = read_regular_file(Path(args.kernel), "kernel image")
    if len(kernel) > KERNEL_MAX_BYTES:
        raise SystemExit(
            f"kernel image is too large for current low-memory boot contract: "
            f"{len(kernel)} bytes, limit is {KERNEL_MAX_BYTES} bytes "
            f"for 0x{KERNEL_LOAD_BASE:x}..0x{KERNEL_LOW_LIMIT:x}"
        )
    root = read_regular_file(Path(args.root), "root image")
    try:
        volume_id = validate_iso_volume_id(args.volume_id)
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc

    fat = FatBuilder()
    fat.add_file("/EFI/BOOT/BOOTX64.EFI", efi)
    fat.add_file("/RABBITBONE/KERNEL.BIN", kernel)
    fat.add_file("/RABBITBONE/ROOT.IMG", root)
    try:
        fat_serial = int(str(args.fat_serial), 0)
    except ValueError as exc:
        raise SystemExit(f"invalid FAT serial {args.fat_serial!r}") from exc
    if fat_serial < 0 or fat_serial > 0xFFFFFFFF:
        raise SystemExit(f"FAT serial out of u32 range: {args.fat_serial!r}")
    fat_image = fat.build(volume_id, fat_serial)
    iso = build_iso(fat_image, volume_id)
    out = Path(args.out)
    atomic_write(out, iso)
    print(f"created {out} size={len(iso)} bytes esp={len(fat_image)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
