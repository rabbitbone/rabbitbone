#include <rabbitbone/apic.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/types.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/smp.h>

#define MMIO_IDENTITY_ACCESS_LIMIT 0x0000800000000000ull

#define IA32_APIC_BASE 0x1bu
#define IA32_APIC_BASE_ENABLE (1ull << 11)
#define IA32_APIC_BASE_BSP (1ull << 8)
#define IA32_APIC_BASE_X2APIC_ENABLE (1ull << 10)
#define X2APIC_ID_MSR 0x802u
#define X2APIC_EOI_MSR 0x80bu
#define X2APIC_SVR_MSR 0x80fu
#define X2APIC_ESR_MSR 0x828u
#define X2APIC_ICR_MSR 0x830u
#define LAPIC_ID_REG 0x20u
#define LAPIC_VER_REG 0x30u
#define LAPIC_EOI_REG 0x0b0u
#define LAPIC_SVR_REG 0x0f0u
#define LAPIC_ESR_REG 0x280u
#define LAPIC_ICR_LOW_REG 0x300u
#define LAPIC_ICR_HIGH_REG 0x310u
#define LAPIC_LVT_TIMER_REG 0x320u
#define LAPIC_TIMER_INITIAL_COUNT_REG 0x380u
#define LAPIC_TIMER_CURRENT_COUNT_REG 0x390u
#define LAPIC_TIMER_DIVIDE_REG 0x3e0u
#define LAPIC_LVT_LINT0_REG 0x350u
#define LAPIC_LVT_LINT1_REG 0x360u
#define LAPIC_LVT_ERROR_REG 0x370u
#define LAPIC_SVR_ENABLE (1u << 8)
#define LAPIC_LVT_MASKED (1u << 16)
#define LAPIC_LVT_TIMER_PERIODIC (1u << 17)
#define LAPIC_ICR_DELIVERY_STATUS (1u << 12)
#define LAPIC_DELIVERY_FIXED 0x000u
#define LAPIC_DELIVERY_INIT 0x500u
#define LAPIC_DELIVERY_STARTUP 0x600u
#define LAPIC_DELIVERY_EXTINT 0x700u
#define LAPIC_DELIVERY_NMI 0x400u
#define LAPIC_LEVEL_ASSERT (1u << 14)
#define LAPIC_TRIGGER_LEVEL (1u << 15)
#define LAPIC_DEST_PHYSICAL 0u
#define LAPIC_DEST_ALL_EXCLUDING_SELF (3u << 18u)

static apic_info_t apic_info;

#define IOAPIC_REGSEL 0x00u
#define IOAPIC_WINDOW 0x10u
#define IOAPIC_REG_VER 0x01u
#define IOAPIC_REDTBL_BASE 0x10u
#define IOAPIC_RED_MASKED (1u << 16)
#define IOAPIC_RED_LEVEL (1u << 15)
#define IOAPIC_RED_LOW_ACTIVE (1u << 13)

typedef struct ioapic_runtime {
    bool mapped;
    u32 address;
    u32 gsi_base;
    u32 redir_count;
} ioapic_runtime_t;

static ioapic_runtime_t ioapics[ACPI_MAX_IOAPICS];

static inline void cpu_pause(void) { __asm__ volatile("pause"); }
static bool map_mmio_window(u64 base, u64 size);

static inline u32 lapic_read_raw(u64 base, u32 reg) {
    volatile u32 *p = (volatile u32 *)(uptr)(base + reg);
    return *p;
}

static inline void lapic_write_raw(u64 base, u32 reg, u32 value) {
    volatile u32 *p = (volatile u32 *)(uptr)(base + reg);
    *p = value;
    (void)lapic_read_raw(base, LAPIC_ID_REG);
}

static inline u32 lapic_read(u32 reg) {
    if (!apic_info.lapic_present || !apic_info.lapic_base) return 0;
    return lapic_read_raw(apic_info.lapic_base, reg);
}

static inline void lapic_write(u32 reg, u32 value) {
    if (!apic_info.lapic_present || !apic_info.lapic_base) return;
    lapic_write_raw(apic_info.lapic_base, reg, value);
}

static inline u32 ioapic_read_reg(const ioapic_runtime_t *io, u32 reg) {
    volatile u32 *sel = (volatile u32 *)(uptr)(io->address + IOAPIC_REGSEL);
    volatile u32 *win = (volatile u32 *)(uptr)(io->address + IOAPIC_WINDOW);
    *sel = reg;
    return *win;
}

static inline void ioapic_write_reg(const ioapic_runtime_t *io, u32 reg, u32 value) {
    volatile u32 *sel = (volatile u32 *)(uptr)(io->address + IOAPIC_REGSEL);
    volatile u32 *win = (volatile u32 *)(uptr)(io->address + IOAPIC_WINDOW);
    *sel = reg;
    *win = value;
}

static const acpi_iso_info_t *apic_find_iso(u8 irq) {
    const acpi_info_t *ai = acpi_get_info();
    if (!ai) return 0;
    for (u32 i = 0; i < ai->iso_count; ++i) if (ai->isos[i].source == irq) return &ai->isos[i];
    return 0;
}

static ioapic_runtime_t *ioapic_for_gsi(u32 gsi) {
    for (u32 i = 0; i < apic_info.ioapic_count && i < ACPI_MAX_IOAPICS; ++i) {
        ioapic_runtime_t *io = &ioapics[i];
        if (io->redir_count == 0) continue;
        if (gsi >= io->gsi_base && gsi < io->gsi_base + io->redir_count) return io;
    }
    return 0;
}

static bool apic_prepare_ioapics(const acpi_info_t *ai) {
    memset(ioapics, 0, sizeof(ioapics));
    if (!ai || ai->ioapic_count == 0) return false;
    bool any = false;
    for (u32 i = 0; i < ai->ioapic_count && i < ACPI_MAX_IOAPICS; ++i) {
        const acpi_ioapic_info_t *src = &ai->ioapics[i];
        ioapic_runtime_t *io = &ioapics[i];
        if (!src->address) continue;
        io->address = src->address;
        io->gsi_base = src->gsi_base;
        io->redir_count = 24u;
        io->mapped = false;
        if (map_mmio_window(src->address, PAGE_SIZE)) {
            u32 ver = ioapic_read_reg(io, IOAPIC_REG_VER);
            u32 count = ((ver >> 16u) & 0xffu) + 1u;
            if (count != 0u && count <= 240u) {
                io->redir_count = count;
                io->mapped = true;
            }
        }
        any = true;
    }
    return any;
}

static void apic_short_delay(u32 rounds) {
    for (volatile u32 i = 0; i < rounds; ++i) cpu_pause();
}

static bool map_mmio_window(u64 base, u64 size) {
    if (base == 0 || size == 0 || base >= MMIO_IDENTITY_ACCESS_LIMIT) return false;
    u64 raw_end = 0;
    if (__builtin_add_overflow(base, size, &raw_end)) return false;
    if (raw_end > MMIO_IDENTITY_ACCESS_LIMIT) return false;
    u64 start = RABBITBONE_ALIGN_DOWN(base, PAGE_SIZE);
    u64 end = 0;
    if (!rabbitbone_align_up_u64_checked(raw_end, PAGE_SIZE, &end)) return false;
    if (end > MMIO_IDENTITY_ACCESS_LIMIT) return false;
    for (u64 p = start; p < end; p += PAGE_SIZE) {
        uptr phys = 0;
        u64 flags = 0;
        if (vmm_translate((uptr)p, &phys, &flags)) {
            if (phys != (uptr)p) return false;
            continue;
        }
        if (!vmm_map_4k((uptr)p, (uptr)p, VMM_WRITE | VMM_NX | VMM_NOCACHE | VMM_GLOBAL)) return false;
    }
    return true;
}

static bool x2apic_send_ipi(u32 apic_id, u32 delivery, u8 vector, u32 control_bits) {
    if (!apic_info.lapic_present || !apic_info.x2apic_enabled) return false;
    u64 value = ((u64)apic_id << 32u) | (u64)(LAPIC_DEST_PHYSICAL | delivery | (u32)vector | control_bits);
    cpu_write_msr(X2APIC_ESR_MSR, 0);
    cpu_write_msr(X2APIC_ICR_MSR, value);
    cpu_write_msr(X2APIC_ESR_MSR, 0);
    return true;
}

static bool lapic_wait_icr_idle(void) {
    for (u32 i = 0; i < 1000000u; ++i) {
        if ((lapic_read(LAPIC_ICR_LOW_REG) & LAPIC_ICR_DELIVERY_STATUS) == 0) return true;
        cpu_pause();
    }
    return false;
}

static bool lapic_send_ipi(u32 apic_id, u32 delivery, u8 vector, u32 control_bits) {
    if (apic_info.x2apic_enabled) return x2apic_send_ipi(apic_id, delivery, vector, control_bits);
    if (apic_id > 255u || !apic_info.lapic_present || !apic_info.lapic_base) return false;
    if (!lapic_wait_icr_idle()) return false;
    lapic_write(LAPIC_ESR_REG, 0);
    lapic_write(LAPIC_ICR_HIGH_REG, apic_id << 24u);
    u32 lo = LAPIC_DEST_PHYSICAL | delivery | (u32)vector | control_bits;
    lapic_write(LAPIC_ICR_LOW_REG, lo);
    if (!lapic_wait_icr_idle()) return false;
    lapic_write(LAPIC_ESR_REG, 0);
    return true;
}

void apic_local_init(void) {
    if (!apic_info.cpu_has_apic) return;
    u64 base_msr = cpu_read_msr(IA32_APIC_BASE);
    if ((base_msr & IA32_APIC_BASE_ENABLE) == 0) {
        cpu_write_msr(IA32_APIC_BASE, base_msr | IA32_APIC_BASE_ENABLE);
        base_msr = cpu_read_msr(IA32_APIC_BASE);
    }
    if (!apic_info.lapic_base) apic_info.lapic_base = base_msr & 0xfffff000ull;
    if (!apic_info.lapic_base && !apic_info.x2apic_enabled) return;
    apic_info.lapic_present = true;
    if (apic_info.x2apic_enabled) {
        u64 svr = cpu_read_msr(X2APIC_SVR_MSR);
        cpu_write_msr(X2APIC_SVR_MSR, (svr & 0xffffffffffffff00ull) | LAPIC_SVR_ENABLE | 0xffu);
    } else {
        lapic_write(LAPIC_SVR_REG, (lapic_read(LAPIC_SVR_REG) & 0xffffff00u) | LAPIC_SVR_ENABLE | 0xffu);
        lapic_write(LAPIC_LVT_TIMER_REG, LAPIC_LVT_MASKED);
    }

    bool is_bsp = (base_msr & IA32_APIC_BASE_BSP) != 0;
    if (!is_bsp && apic_info.bsp_apic_id <= 255u) is_bsp = apic_current_id() == apic_info.bsp_apic_id;
    if (!apic_info.x2apic_enabled) {
        if (is_bsp && apic_info.legacy_pic_fallback) {
            lapic_write(LAPIC_LVT_LINT0_REG, LAPIC_DELIVERY_EXTINT);
            lapic_write(LAPIC_LVT_LINT1_REG, LAPIC_DELIVERY_NMI);
        } else {
            lapic_write(LAPIC_LVT_LINT0_REG, LAPIC_LVT_MASKED);
            lapic_write(LAPIC_LVT_LINT1_REG, LAPIC_LVT_MASKED);
        }
        lapic_write(LAPIC_LVT_ERROR_REG, LAPIC_LVT_MASKED | 0xfeu);
        lapic_write(LAPIC_ESR_REG, 0);
        lapic_write(LAPIC_ESR_REG, 0);
    } else {
        cpu_write_msr(X2APIC_ESR_MSR, 0);
        cpu_write_msr(X2APIC_ESR_MSR, 0);
    }
}

u32 apic_current_id(void) {
    if (apic_info.lapic_present && apic_info.x2apic_enabled) return (u32)cpu_read_msr(X2APIC_ID_MSR);
    if (apic_info.lapic_present && apic_info.lapic_base) {
        return (lapic_read(LAPIC_ID_REG) >> 24u) & 0xffu;
    }
    u32 a = 0, b = 0, c = 0, d = 0;
    cpu_cpuid(1u, 0u, &a, &b, &c, &d);
    (void)a; (void)c; (void)d;
    return (b >> 24u) & 0xffu;
}

void apic_send_eoi(void) {
    if (!apic_info.lapic_present) return;
    if (apic_info.x2apic_enabled) cpu_write_msr(X2APIC_EOI_MSR, 0);
    else if (apic_info.lapic_base) lapic_write(LAPIC_EOI_REG, 0);
}

bool apic_send_init_ipi(u32 apic_id) {
    bool ok = lapic_send_ipi(apic_id, LAPIC_DELIVERY_INIT, 0, LAPIC_TRIGGER_LEVEL | LAPIC_LEVEL_ASSERT);
    apic_short_delay(200000u);
    ok = lapic_send_ipi(apic_id, LAPIC_DELIVERY_INIT, 0, LAPIC_TRIGGER_LEVEL) && ok;
    apic_short_delay(200000u);
    return ok;
}

bool apic_send_startup_ipi(u32 apic_id, u8 vector) {
    bool ok = lapic_send_ipi(apic_id, LAPIC_DELIVERY_STARTUP, vector, 0);
    apic_short_delay(20000u);
    return ok;
}

bool apic_send_fixed_ipi(u32 apic_id, u8 vector) {
    return lapic_send_ipi(apic_id, LAPIC_DELIVERY_FIXED, vector, 0);
}

bool apic_send_fixed_ipi_all_others(u8 vector) {
    return lapic_send_ipi(0, LAPIC_DELIVERY_FIXED, vector, LAPIC_DEST_ALL_EXCLUDING_SELF);
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
    apic_info.ioapic_present = apic_prepare_ioapics(ai);
    apic_info.legacy_pic_fallback = true;
    if (!apic_info.cpu_has_apic) {
        KLOG(LOG_WARN, "apic", "CPU local APIC not advertised; using legacy PIC fallback");
        return;
    }
    u64 base_msr = cpu_read_msr(IA32_APIC_BASE);
    apic_info.lapic_base = base_msr & 0xfffff000ull;
    apic_info.x2apic_enabled = (base_msr & IA32_APIC_BASE_X2APIC_ENABLE) != 0;
    if (ai && ai->lapic_address) apic_info.lapic_base = ai->lapic_address;
    if (apic_info.x2apic_enabled) {
        apic_info.lapic_present = (base_msr & IA32_APIC_BASE_ENABLE) != 0;
        if (apic_info.lapic_present) {
            apic_info.bsp_apic_id = apic_current_id();
            apic_local_init();
        }
    } else if (apic_info.lapic_base) {
        if (!map_mmio_window(apic_info.lapic_base, PAGE_SIZE)) {
            KLOG(LOG_WARN, "apic", "invalid or unmappable LAPIC base=%p; using PIC fallback", (void *)(uptr)apic_info.lapic_base);
            apic_info.lapic_base = 0;
            return;
        }
        apic_info.lapic_present = (base_msr & IA32_APIC_BASE_ENABLE) != 0;
        apic_local_init();
        if (apic_info.lapic_present) {
            u32 id_reg = lapic_read(LAPIC_ID_REG);
            u32 ver = lapic_read(LAPIC_VER_REG);
            apic_info.bsp_apic_id = (id_reg >> 24) & 0xffu;
            apic_info.lapic_version = ver & 0xffu;
        }
    }
    KLOG(LOG_INFO, "apic", "lapic=%u base=%p bsp_apic_id=%u ioapics=%u pic_fallback=1",
         apic_info.lapic_present ? 1u : 0u, (void *)(uptr)apic_info.lapic_base,
         apic_info.bsp_apic_id, apic_info.ioapic_count);
}

bool apic_route_legacy_irq(u8 irq, u8 vector, u32 cpu_id) {
    if (!apic_info.ioapic_present || irq >= 16u || vector < 32u) return false;
    const smp_info_t *si = smp_get_info();
    if (!si || cpu_id >= si->cpu_count || !si->cpus[cpu_id].started) return false;
    const acpi_iso_info_t *iso = apic_find_iso(irq);
    u32 gsi = iso ? iso->gsi : (u32)irq;
    ioapic_runtime_t *io = ioapic_for_gsi(gsi);
    if (!io) return false;
    u32 pin = gsi - io->gsi_base;
    u32 low = (u32)vector;
    if (iso) {
        u16 polarity = iso->flags & 0x3u;
        u16 trigger = (iso->flags >> 2u) & 0x3u;
        if (polarity == 3u) low |= IOAPIC_RED_LOW_ACTIVE;
        if (trigger == 3u) low |= IOAPIC_RED_LEVEL;
    }
    if (si->cpus[cpu_id].apic_id > 255u) return false;
    u32 high = si->cpus[cpu_id].apic_id << 24u;
    if (io->mapped) {
        ioapic_write_reg(io, IOAPIC_REDTBL_BASE + pin * 2u + 1u, high);
        ioapic_write_reg(io, IOAPIC_REDTBL_BASE + pin * 2u, low);
    }
    apic_info.ioapic_routing_enabled = true;
    __atomic_add_fetch(&apic_info.ioapic_route_updates, 1u, __ATOMIC_RELAXED);
    return true;
}

bool apic_irq_balance_legacy(u8 irq, u8 vector) {
    const smp_info_t *si = smp_get_info();
    if (!si || si->started_cpu_count == 0) return false;
    for (u32 tries = 0; tries < si->cpu_count; ++tries) {
        u32 cpu = (apic_info.irq_balance_next_cpu + tries) % si->cpu_count;
        if (!si->cpus[cpu].started) continue;
        if (apic_route_legacy_irq(irq, vector, cpu)) {
            apic_info.irq_balance_next_cpu = (cpu + 1u) % si->cpu_count;
            return true;
        }
    }
    return false;
}

void apic_ioapic_routing_enable(bool enabled) {
    apic_info.ioapic_routing_enabled = enabled && apic_info.ioapic_present;
}

bool apic_local_timer_enable(u8 vector, u32 initial_count, bool periodic) {
    if (!apic_info.lapic_present || apic_info.x2apic_enabled || !apic_info.lapic_base || vector < 32u || initial_count == 0) return false;
    lapic_write(LAPIC_TIMER_DIVIDE_REG, 0x3u);
    lapic_write(LAPIC_LVT_TIMER_REG, (u32)vector | (periodic ? LAPIC_LVT_TIMER_PERIODIC : 0u));
    lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, initial_count);
    apic_info.lapic_timer_enabled = true;
    apic_info.lapic_timer_vector = vector;
    return true;
}

void apic_local_timer_disable(void) {
    if (!apic_info.lapic_present || apic_info.x2apic_enabled || !apic_info.lapic_base) return;
    lapic_write(LAPIC_LVT_TIMER_REG, LAPIC_LVT_MASKED);
    lapic_write(LAPIC_TIMER_INITIAL_COUNT_REG, 0);
    apic_info.lapic_timer_enabled = false;
}

void apic_timer_note_interrupt(void) {
    __atomic_add_fetch(&apic_info.lapic_timer_irqs, 1u, __ATOMIC_RELAXED);
}

const apic_info_t *apic_get_info(void) { return &apic_info; }


void apic_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    rabbitbone_buf_appendf(&bo,
            "apic: cpu_apic=%u lapic=%u base=%p version=0x%x bsp_apic_id=%u current_apic_id=%u x2apic_supported=%u x2apic_enabled=%u\n",
            apic_info.cpu_has_apic ? 1u : 0u, apic_info.lapic_present ? 1u : 0u,
            (void *)(uptr)apic_info.lapic_base, apic_info.lapic_version, apic_info.bsp_apic_id,
            apic_current_id(), apic_info.x2apic_supported ? 1u : 0u, apic_info.x2apic_enabled ? 1u : 0u);
    rabbitbone_buf_appendf(&bo, "  ioapics=%u present=%u pic_fallback=%u ioapic_routing_enabled=%u route_updates=%llu irq_balance_next_cpu=%u\n",
            apic_info.ioapic_count, apic_info.ioapic_present ? 1u : 0u, apic_info.legacy_pic_fallback ? 1u : 0u,
            apic_info.ioapic_routing_enabled ? 1u : 0u, (unsigned long long)apic_info.ioapic_route_updates, apic_info.irq_balance_next_cpu);
    rabbitbone_buf_appendf(&bo, "  lapic_timer=%u vector=%u irqs=%llu\n", apic_info.lapic_timer_enabled ? 1u : 0u,
            apic_info.lapic_timer_vector, (unsigned long long)apic_info.lapic_timer_irqs);
}

bool apic_selftest(void) {
    if (apic_info.ioapic_count > ACPI_MAX_IOAPICS) return false;
    if (apic_info.ioapic_routing_enabled && !apic_info.ioapic_present) return false;
    if (!apic_info.cpu_has_apic) return apic_info.legacy_pic_fallback;
    if (apic_info.lapic_base == 0) return apic_info.legacy_pic_fallback && !apic_info.lapic_present;
    if (apic_info.lapic_present && !apic_info.x2apic_enabled && apic_current_id() > 255u) return false;
    return true;
}
