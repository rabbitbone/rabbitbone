#ifndef RABBITBONE_ACPI_H
#define RABBITBONE_ACPI_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define ACPI_MAX_CPUS 16u
#define ACPI_MAX_IOAPICS 4u
#define ACPI_MAX_ISO 16u

typedef struct acpi_cpu_info {
    u8 acpi_id;
    u8 apic_id;
    u32 flags;
    bool enabled;
} acpi_cpu_info_t;

typedef struct acpi_ioapic_info {
    u8 id;
    u32 address;
    u32 gsi_base;
} acpi_ioapic_info_t;

typedef struct acpi_iso_info {
    u8 bus;
    u8 source;
    u32 gsi;
    u16 flags;
} acpi_iso_info_t;

typedef struct acpi_info {
    bool found;
    bool xsdt;
    bool madt_found;
    bool hpet_found;
    u64 rsdp;
    u64 root_table;
    u32 table_count;
    u32 lapic_address;
    u32 cpu_count;
    u32 enabled_cpu_count;
    u32 ioapic_count;
    u32 iso_count;
    u64 hpet_address;
    u16 hpet_min_tick;
    acpi_cpu_info_t cpus[ACPI_MAX_CPUS];
    acpi_ioapic_info_t ioapics[ACPI_MAX_IOAPICS];
    acpi_iso_info_t isos[ACPI_MAX_ISO];
} acpi_info_t;

void acpi_init(void);
bool acpi_available(void);
const acpi_info_t *acpi_get_info(void);
const void *acpi_find_table(const char sig[4]);
void acpi_format_status(char *out, usize out_len);
bool acpi_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
