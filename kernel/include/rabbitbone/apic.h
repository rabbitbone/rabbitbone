#ifndef RABBITBONE_APIC_H
#define RABBITBONE_APIC_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct apic_info {
    bool cpu_has_apic;
    bool lapic_present;
    bool x2apic_supported;
    bool x2apic_enabled;
    u32 bsp_apic_id;
    u64 lapic_base;
    u32 lapic_version;
    u32 ioapic_count;
    bool ioapic_present;
    bool legacy_pic_fallback;
} apic_info_t;

void apic_init(void);
const apic_info_t *apic_get_info(void);
void apic_format_status(char *out, usize out_len);
bool apic_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
