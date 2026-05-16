#!/usr/bin/env python3
from pathlib import Path
import sys

errors = []
boot = Path("boot/uefi/bootx64.c").read_text()
entry = Path("kernel/arch/x86_64/entry.S").read_text()

if "typedef void (RABBITBONE_SYSV_ABI *kernel_entry_t)(const rabbitbone_bootinfo_t *);" not in boot:
    errors.append("boot/uefi/bootx64.c: kernel entry pointer must be declared with SysV ABI; UEFI code is Win64 ABI")
if "typedef void (*kernel_entry_t)(const rabbitbone_bootinfo_t *);" in boot:
    errors.append("boot/uefi/bootx64.c: plain kernel_entry_t would pass bootinfo in RCX instead of RDI")
if "bootinfo_candidate_has_magic:" not in entry:
    errors.append("kernel/arch/x86_64/entry.S: missing RDI/RCX bootinfo ABI fallback")
if "mov r12, rcx" not in entry:
    errors.append("kernel/arch/x86_64/entry.S: fallback must preserve RCX bootinfo into r12")
if "0x52424f4e" not in entry.lower():
    errors.append("kernel/arch/x86_64/entry.S: fallback must verify RABBITBONE_BOOT_MAGIC before using RCX")
if "'R','A','B','B','I','T','B','O','N','E'" not in boot:
    errors.append("boot/uefi/bootx64.c: loader must read from /RABBITBONE, matching live ISO layout")
stale_dir_marker = "'" + "','".join(["A", "U", "R", "O", "R", "A"]) + "'"
if stale_dir_marker in boot:
    errors.append("boot/uefi/bootx64.c: stale pre-Rabbitbone loader path would break UEFI boot")

if "RABBITBONE_UEFI_KERNEL_MAX_BYTES" not in boot:
    errors.append("boot/uefi/bootx64.c: UEFI loader must cap KERNEL.BIN to the low-memory kernel window")
if "get_memory_map_dynamic" not in boot or "EFI_BUFFER_TOO_SMALL" not in boot:
    errors.append("boot/uefi/bootx64.c: UEFI loader must grow GetMemoryMap buffer instead of assuming a fixed descriptor count")
if "EFI_FILE_DIRECTORY" not in boot or "info->Attribute & EFI_FILE_DIRECTORY" not in boot:
    errors.append("boot/uefi/bootx64.c: UEFI loader must reject directories when opening kernel/root files")

if "EFI_ACPI_20_TABLE_GUID" not in boot or "locate_acpi_rsdp" not in boot or "RABBITBONE_BOOT_ACPI_RSDP(bootinfo)" not in boot:
    errors.append("boot/uefi/bootx64.c: UEFI loader must pass ACPI RSDP through bootinfo for APIC/SMP discovery")

if errors:
    for e in errors:
        print(e, file=sys.stderr)
    sys.exit(1)
print("uefi boot ABI contract checks passed")
