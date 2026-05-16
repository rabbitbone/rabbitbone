#include <rabbitbone/hpet.h>
#include <rabbitbone/acpi.h>
#include <rabbitbone/vmm.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/console.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/types.h>
#include <rabbitbone/memory.h>
#include <rabbitbone/math64.h>

#define MMIO_IDENTITY_ACCESS_LIMIT 0x0000800000000000ull

#define HPET_GCAP_ID 0x000u
#define HPET_GEN_CONF 0x010u
#define HPET_MAIN_COUNTER 0x0f0u
#define HPET_CONF_ENABLE 0x1u

static hpet_info_t hpet_info;
static volatile u8 *hpet_mmio;

static u64 hpet_read64(u32 off) {
    volatile u64 *p = (volatile u64 *)(hpet_mmio + off);
    return *p;
}

static void hpet_write64(u32 off, u64 v) {
    volatile u64 *p = (volatile u64 *)(hpet_mmio + off);
    *p = v;
}

static u64 hpet_read_counter_raw(void) {
    if (!hpet_info.enabled || !hpet_mmio) return 0;
    if (hpet_info.counter_bits == 32u) return (u32)hpet_read64(HPET_MAIN_COUNTER);
    return hpet_read64(HPET_MAIN_COUNTER);
}

static bool hpet_map_mmio(u64 base, u64 size) {
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

void hpet_init(void) {
    memset(&hpet_info, 0, sizeof(hpet_info));
    hpet_mmio = 0;
    const acpi_info_t *ai = acpi_get_info();
    if (!ai || !ai->hpet_found || ai->hpet_address == 0) {
        KLOG(LOG_INFO, "hpet", "not advertised by ACPI; PIT fallback remains active");
        return;
    }
    hpet_info.present = true;
    hpet_info.base = ai->hpet_address;
    if (!hpet_map_mmio(hpet_info.base, 0x400u)) {
        KLOG(LOG_WARN, "hpet", "invalid or unmappable base=%p; PIT fallback remains active", (void *)(uptr)hpet_info.base);
        memset(&hpet_info, 0, sizeof(hpet_info));
        return;
    }
    hpet_mmio = (volatile u8 *)(uptr)hpet_info.base;
    u64 cap = hpet_read64(HPET_GCAP_ID);
    hpet_info.period_fs = (u32)(cap >> 32);
    hpet_info.counter_bits = (cap & (1ull << 13)) ? 64u : 32u;
    hpet_info.comparator_count = (u32)(((cap >> 8) & 0x1fu) + 1u);
    if (hpet_info.period_fs == 0 || hpet_info.period_fs > 100000000u) {
        KLOG(LOG_WARN, "hpet", "invalid period=%u fs; disabling", hpet_info.period_fs);
        memset(&hpet_info, 0, sizeof(hpet_info));
        hpet_mmio = 0;
        return;
    }
    hpet_info.frequency_hz = 1000000000000000ull / hpet_info.period_fs;
    u64 conf = hpet_read64(HPET_GEN_CONF);
    hpet_write64(HPET_GEN_CONF, conf & ~HPET_CONF_ENABLE);
    hpet_write64(HPET_MAIN_COUNTER, 0);
    hpet_write64(HPET_GEN_CONF, (conf | HPET_CONF_ENABLE) & ~(1ull << 1));
    hpet_info.enabled = true;
    KLOG(LOG_INFO, "hpet", "enabled base=%p period=%u fs freq=%llu Hz bits=%u comparators=%u",
         (void *)(uptr)hpet_info.base, hpet_info.period_fs, (unsigned long long)hpet_info.frequency_hz,
         hpet_info.counter_bits, hpet_info.comparator_count);
}

const hpet_info_t *hpet_get_info(void) { return &hpet_info; }

bool hpet_now_ns(u64 *out_ns) {
    if (!out_ns || !hpet_info.enabled || hpet_info.period_fs == 0) return false;
    u64 counter = hpet_read_counter_raw();
    u64 ns = rabbitbone_u64_mul_div_saturating(counter, hpet_info.period_fs, 1000000ull);
    if (ns == ~0ull) return false;
    *out_ns = ns;
    return true;
}


void hpet_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    u64 now = 0;
    bool have_now = hpet_now_ns(&now);
    rabbitbone_buf_appendf(&bo, "hpet: present=%u enabled=%u base=%p period_fs=%u freq_hz=%llu bits=%u comparators=%u\n",
            hpet_info.present ? 1u : 0u, hpet_info.enabled ? 1u : 0u, (void *)(uptr)hpet_info.base,
            hpet_info.period_fs, (unsigned long long)hpet_info.frequency_hz, hpet_info.counter_bits,
            hpet_info.comparator_count);
    rabbitbone_buf_appendf(&bo, "  now_ns=%llu valid=%u\n", (unsigned long long)now, have_now ? 1u : 0u);
}

bool hpet_selftest(void) {
    if (!hpet_info.present) return true;
    if (!hpet_info.enabled || hpet_info.period_fs == 0 || hpet_info.frequency_hz == 0) return false;
    u64 a = hpet_read_counter_raw();
    for (volatile u32 i = 0; i < 10000u; ++i) { }
    u64 b = hpet_read_counter_raw();
    return b >= a;
}
