#include <aurora/timer.h>
#include <aurora/drivers.h>
#include <aurora/hpet.h>
#include <aurora/arch/cpu.h>
#include <aurora/arch/io.h>
#include <aurora/spinlock.h>
#include <aurora/panic.h>
#include <aurora/libc.h>
#include <aurora/log.h>
#include <aurora/format.h>
#include <aurora/console.h>

#define AURORA_PIT_HZ 100ull

static timer_info_t timer_info_state;
static u64 tsc_base;
static u64 ns_base;


void timer_init_sources(void) {
    memset(&timer_info_state, 0, sizeof(timer_info_state));
    timer_info_state.pit_active = true;
    timer_info_state.pit_hz = AURORA_PIT_HZ;
    const hpet_info_t *hi = hpet_get_info();
    timer_info_state.hpet_active = hi && hi->enabled;
    timer_info_state.tsc_supported = cpu_invariant_tsc_supported();
    timer_info_state.clocksource = timer_info_state.hpet_active ? "hpet" : "pit";
    if (timer_info_state.hpet_active && timer_info_state.tsc_supported) {
        u64 hpet_a = 0;
        u64 hpet_b = 0;
        if (hpet_now_ns(&hpet_a)) {
            u64 tsc_a = cpu_read_tsc();
            do {
                (void)hpet_now_ns(&hpet_b);
            } while (hpet_b - hpet_a < 2000000ull);
            u64 tsc_b = cpu_read_tsc();
            u64 ns_delta = hpet_b - hpet_a;
            if (tsc_b > tsc_a && ns_delta != 0) {
                timer_info_state.tsc_hz = ((tsc_b - tsc_a) * 1000000000ull) / ns_delta;
                if (timer_info_state.tsc_hz != 0) {
                    timer_info_state.tsc_calibrated = true;
                    timer_info_state.clocksource = "tsc";
                    tsc_base = cpu_read_tsc();
                    (void)hpet_now_ns(&ns_base);
                }
            }
        }
    }
    KLOG(LOG_INFO, "timer", "clocksource=%s pit=%u hpet=%u tsc_supported=%u tsc_hz=%llu",
         timer_info_state.clocksource, timer_info_state.pit_active ? 1u : 0u,
         timer_info_state.hpet_active ? 1u : 0u, timer_info_state.tsc_supported ? 1u : 0u,
         (unsigned long long)timer_info_state.tsc_hz);
}

u64 timer_now_ns(void) {
    if (timer_info_state.tsc_calibrated && timer_info_state.tsc_hz) {
        u64 delta = cpu_read_tsc() - tsc_base;
        return ns_base + (delta * 1000000000ull) / timer_info_state.tsc_hz;
    }
    u64 hpet_ns = 0;
    if (hpet_now_ns(&hpet_ns)) return hpet_ns;
    return pit_ticks() * (1000000000ull / AURORA_PIT_HZ);
}

void timer_get_info(timer_info_t *out) {
    if (!out) return;
    *out = timer_info_state;
    out->monotonic_ns = timer_now_ns();
}

void timer_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    aurora_buf_out_t bo;
    aurora_buf_init(&bo, out, out_len);
    timer_info_t ti;
    timer_get_info(&ti);
    aurora_buf_appendf(&bo, "timer: clocksource=%s now_ns=%llu pit=%u pit_hz=%llu hpet=%u tsc_supported=%u tsc_calibrated=%u tsc_hz=%llu\n",
            ti.clocksource ? ti.clocksource : "unknown", (unsigned long long)ti.monotonic_ns,
            ti.pit_active ? 1u : 0u, (unsigned long long)ti.pit_hz, ti.hpet_active ? 1u : 0u,
            ti.tsc_supported ? 1u : 0u, ti.tsc_calibrated ? 1u : 0u,
            (unsigned long long)ti.tsc_hz);
}

void timer_sleep_ticks(u64 delta) {
    u64 flags = irq_save();
    const bool irq_was_enabled = (flags & (1ull << 9)) != 0;
    if (delta != 0 && !irq_was_enabled) {
        irq_restore(flags);
        PANIC("timer_sleep_ticks(%llu) called with interrupts disabled", (unsigned long long)delta);
    }
    u64 start = pit_ticks();
    while ((pit_ticks() - start) < delta) {
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    irq_restore(flags);
}

bool timer_selftest(void) {
    u64 flags;
#if defined(AURORA_HOST_TEST)
    flags = 1ull << 9;
#else
    __asm__ volatile("pushfq; popq %0" : "=r"(flags) :: "memory");
#endif
    if ((flags & (1ull << 9)) == 0) return false;
    u64 before = pit_ticks();
    u64 ns_before = timer_now_ns();
    timer_sleep_ticks(2);
    u64 after = pit_ticks();
    u64 ns_after = timer_now_ns();
    return (after - before) >= 2u && ns_after >= ns_before;
}
