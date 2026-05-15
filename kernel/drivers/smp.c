#include <rabbitbone/smp.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/apic.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>

static smp_info_t smp_info;

void smp_init_groundwork(void) {
    memset(&smp_info, 0, sizeof(smp_info));
    smp_info.initialized = true;
    smp_info.bootstrap_only = true;
    const acpi_info_t *ai = acpi_get_info();
    const apic_info_t *ap = apic_get_info();
    smp_info.bsp_apic_id = ap ? ap->bsp_apic_id : 0;
    if (ai && ai->madt_found && ai->cpu_count) {
        for (u32 i = 0; i < ai->cpu_count && smp_info.cpu_count < SMP_MAX_CPUS; ++i) {
            const acpi_cpu_info_t *ac = &ai->cpus[i];
            smp_cpu_info_t *sc = &smp_info.cpus[smp_info.cpu_count];
            sc->logical_id = (u8)smp_info.cpu_count;
            sc->acpi_id = ac->acpi_id;
            sc->apic_id = ac->apic_id;
            sc->enabled = ac->enabled;
            sc->bsp = ac->apic_id == smp_info.bsp_apic_id;
            sc->started = sc->bsp;
            ++smp_info.cpu_count;
            if (sc->enabled) ++smp_info.enabled_cpu_count;
        }
    } else {
        smp_cpu_info_t *sc = &smp_info.cpus[0];
        sc->logical_id = 0;
        sc->acpi_id = 0;
        sc->apic_id = (u8)smp_info.bsp_apic_id;
        sc->enabled = true;
        sc->bsp = true;
        sc->started = true;
        smp_info.cpu_count = 1;
        smp_info.enabled_cpu_count = 1;
    }
    bool found_bsp = false;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) if (smp_info.cpus[i].bsp) found_bsp = true;
    if (!found_bsp && smp_info.cpu_count > 0) {
        smp_info.cpus[0].bsp = true;
        smp_info.cpus[0].started = true;
        smp_info.bsp_apic_id = smp_info.cpus[0].apic_id;
    }
    KLOG(LOG_INFO, "smp", "groundwork cpus=%u enabled=%u bsp_apic_id=%u ap_startup=disabled",
         smp_info.cpu_count, smp_info.enabled_cpu_count, smp_info.bsp_apic_id);
}

const smp_info_t *smp_get_info(void) { return &smp_info; }


void smp_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    rabbitbone_buf_appendf(&bo, "smp: initialized=%u bootstrap_only=%u cpus=%u enabled=%u bsp_apic_id=%u\n",
            smp_info.initialized ? 1u : 0u, smp_info.bootstrap_only ? 1u : 0u,
            smp_info.cpu_count, smp_info.enabled_cpu_count, smp_info.bsp_apic_id);
    for (u32 i = 0; i < smp_info.cpu_count; ++i) {
        const smp_cpu_info_t *c = &smp_info.cpus[i];
        rabbitbone_buf_appendf(&bo, "  cpu%u acpi=%u apic=%u bsp=%u enabled=%u started=%u\n",
                c->logical_id, c->acpi_id, c->apic_id, c->bsp ? 1u : 0u, c->enabled ? 1u : 0u,
                c->started ? 1u : 0u);
    }
}

bool smp_selftest(void) {
    if (!smp_info.initialized) return false;
    if (smp_info.cpu_count == 0 || smp_info.cpu_count > SMP_MAX_CPUS) return false;
    u32 bsp_count = 0;
    for (u32 i = 0; i < smp_info.cpu_count; ++i) if (smp_info.cpus[i].bsp) ++bsp_count;
    return bsp_count == 1;
}
