#include <aurora/apic.h>
#include <aurora/acpi.h>
#include <aurora/arch/cpu.h>
#include <aurora/vmm.h>
#include <aurora/libc.h>
#include <aurora/console.h>
#include <aurora/log.h>
#include <aurora/format.h>
#include <aurora/types.h>
#include <aurora/memory.h>

#define IA32_APIC_BASE 0x1bu
#define IA32_APIC_BASE_ENABLE (1ull << 11)
#define LAPIC_ID_REG 0x20u
#define LAPIC_VER_REG 0x30u

static apic_info_t apic_info;

static inline u32 lapic_read(u64 base, u32 reg) {
    volatile u32 *p = (volatile u32 *)(uptr)(base + reg);
    return *p;
}

void apic_init(void) {
    memset(&apic_info, 0, sizeof(apic_info));
    u32 a = 0, b = 0, c = 0, d = 0;
    cpu_cpuid(1u, 0u, &a, &b, &c, &d);
    apic_info.cpu_has_apic = (d & (1u << 9)) != 0;
    apic_info.x2apic_supported = (c & (1u << 21)) != 0;
    apic_info.bsp_apic_id = (b >> 24) & 0xffu;
    const acpi_info_t *ai = acpi_get_info();
    apic_info.ioapic_count = ai ? ai->ioapic_count : 0;
    apic_info.ioapic_present = apic_info.ioapic_count != 0;
    apic_info.legacy_pic_fallback = true;
    if (!apic_info.cpu_has_apic) {
        KLOG(LOG_WARN, "apic", "CPU local APIC not advertised; using legacy PIC fallback");
        return;
    }
    u64 base_msr = cpu_read_msr(IA32_APIC_BASE);
    apic_info.lapic_base = base_msr & 0xfffff000ull;
    apic_info.x2apic_enabled = (base_msr & (1ull << 10)) != 0;
    if (ai && ai->lapic_address) apic_info.lapic_base = ai->lapic_address;
    if (apic_info.lapic_base) {
        u64 start = AURORA_ALIGN_DOWN(apic_info.lapic_base, PAGE_SIZE);
        u64 end = AURORA_ALIGN_UP(apic_info.lapic_base + PAGE_SIZE, PAGE_SIZE);
        for (u64 p = start; p < end; p += PAGE_SIZE) {
            uptr phys = 0;
            u64 flags = 0;
            if (!vmm_translate((uptr)p, &phys, &flags)) {
                (void)vmm_map_4k((uptr)p, (uptr)p, VMM_WRITE | VMM_NX | VMM_NOCACHE | VMM_GLOBAL);
            }
        }
        apic_info.lapic_present = (base_msr & IA32_APIC_BASE_ENABLE) != 0;
        if (apic_info.lapic_present && !apic_info.x2apic_enabled) {
            u32 id_reg = lapic_read(apic_info.lapic_base, LAPIC_ID_REG);
            u32 ver = lapic_read(apic_info.lapic_base, LAPIC_VER_REG);
            apic_info.bsp_apic_id = (id_reg >> 24) & 0xffu;
            apic_info.lapic_version = ver & 0xffu;
        }
    }
    KLOG(LOG_INFO, "apic", "lapic=%u base=%p bsp_apic_id=%u ioapics=%u pic_fallback=1",
         apic_info.lapic_present ? 1u : 0u, (void *)(uptr)apic_info.lapic_base,
         apic_info.bsp_apic_id, apic_info.ioapic_count);
}

const apic_info_t *apic_get_info(void) { return &apic_info; }


void apic_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    aurora_buf_out_t bo;
    aurora_buf_init(&bo, out, out_len);
    aurora_buf_appendf(&bo,
            "apic: cpu_apic=%u lapic=%u base=%p version=0x%x bsp_apic_id=%u x2apic_supported=%u x2apic_enabled=%u\n",
            apic_info.cpu_has_apic ? 1u : 0u, apic_info.lapic_present ? 1u : 0u,
            (void *)(uptr)apic_info.lapic_base, apic_info.lapic_version, apic_info.bsp_apic_id,
            apic_info.x2apic_supported ? 1u : 0u, apic_info.x2apic_enabled ? 1u : 0u);
    aurora_buf_appendf(&bo, "  ioapics=%u pic_fallback=%u ioapic_routing_enabled=0\n",
            apic_info.ioapic_count, apic_info.legacy_pic_fallback ? 1u : 0u);
}

bool apic_selftest(void) {
    if (!apic_info.cpu_has_apic) return true;
    if (apic_info.lapic_base == 0) return false;
    if (apic_info.ioapic_count > ACPI_MAX_IOAPICS) return false;
    return true;
}
