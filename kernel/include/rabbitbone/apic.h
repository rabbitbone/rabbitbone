#ifndef RABBITBONE_APIC_H
#define RABBITBONE_APIC_H
#include <rabbitbone/types.h>

#define RABBITBONE_APIC_TIMER_VECTOR 239u
#define RABBITBONE_APIC_TIMER_INITIAL_COUNT 1000000u
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
    bool ioapic_routing_enabled;
    bool lapic_timer_enabled;
    u32 irq_balance_next_cpu;
    u32 lapic_timer_vector;
    u64 ioapic_route_updates;
    u64 lapic_timer_irqs;
} apic_info_t;

void apic_init(void);
void apic_local_init(void);
u32 apic_current_id(void);
void apic_send_eoi(void);
bool apic_send_init_ipi(u32 apic_id);
bool apic_send_startup_ipi(u32 apic_id, u8 vector);
bool apic_send_fixed_ipi(u32 apic_id, u8 vector);
bool apic_send_fixed_ipi_all_others(u8 vector);
bool apic_route_legacy_irq(u8 irq, u8 vector, u32 cpu_id);
bool apic_irq_balance_legacy(u8 irq, u8 vector);
void apic_ioapic_routing_enable(bool enabled);
bool apic_local_timer_enable(u8 vector, u32 initial_count, bool periodic);
void apic_local_timer_disable(void);
void apic_timer_note_interrupt(void);
const apic_info_t *apic_get_info(void);
void apic_format_status(char *out, usize out_len);
bool apic_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
