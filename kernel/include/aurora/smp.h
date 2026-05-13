#ifndef AURORA_SMP_H
#define AURORA_SMP_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

#define SMP_MAX_CPUS 16u

typedef struct smp_cpu_info {
    u8 logical_id;
    u8 acpi_id;
    u8 apic_id;
    bool bsp;
    bool enabled;
    bool started;
} smp_cpu_info_t;

typedef struct smp_info {
    bool initialized;
    bool bootstrap_only;
    u32 cpu_count;
    u32 enabled_cpu_count;
    u32 bsp_apic_id;
    smp_cpu_info_t cpus[SMP_MAX_CPUS];
} smp_info_t;

void smp_init_groundwork(void);
const smp_info_t *smp_get_info(void);
void smp_format_status(char *out, usize out_len);
bool smp_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
