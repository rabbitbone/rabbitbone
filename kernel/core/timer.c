#include <rabbitbone/timer.h>
#include <rabbitbone/drivers.h>
#include <rabbitbone/hpet.h>
#include <rabbitbone/arch/cpu.h>
#include <rabbitbone/arch/io.h>
#include <rabbitbone/spinlock.h>
#include <rabbitbone/panic.h>
#include <rabbitbone/libc.h>
#include <rabbitbone/log.h>
#include <rabbitbone/format.h>
#include <rabbitbone/console.h>
#include <rabbitbone/math64.h>

#define RABBITBONE_PIT_HZ 100ull
#define TIMER_HPET_CALIBRATION_NS 2000000ull
#define TIMER_HPET_CALIBRATION_SPIN_LIMIT 1000000u

static timer_info_t timer_info_state;
static u64 tsc_base;
static u64 ns_base;

void timer_init_sources(void) {
    memset(&timer_info_state, 0, sizeof(timer_info_state));
    timer_info_state.pit_active = true;
    timer_info_state.pit_hz = RABBITBONE_PIT_HZ;
    const hpet_info_t *hi = hpet_get_info();
    timer_info_state.hpet_active = hi && hi->enabled;
    timer_info_state.tsc_supported = cpu_invariant_tsc_supported();
    timer_info_state.clocksource = timer_info_state.hpet_active ? "hpet" : "pit";
    if (timer_info_state.hpet_active && timer_info_state.tsc_supported) {
        u64 hpet_a = 0;
        u64 hpet_b = 0;
        if (hpet_now_ns(&hpet_a)) {
            u64 tsc_a = cpu_read_tsc();
            bool advanced = false;
            for (u32 spin = 0; spin < TIMER_HPET_CALIBRATION_SPIN_LIMIT; ++spin) {
                if (!hpet_now_ns(&hpet_b)) break;
                if (hpet_b >= hpet_a && hpet_b - hpet_a >= TIMER_HPET_CALIBRATION_NS) {
                    advanced = true;
                    break;
                }
            }
            u64 tsc_b = cpu_read_tsc();
            if (advanced && hpet_b >= hpet_a) {
                u64 ns_delta = hpet_b - hpet_a;
                if (tsc_b > tsc_a && ns_delta != 0) {
                    timer_info_state.tsc_hz = rabbitbone_u64_mul_div_saturating(tsc_b - tsc_a, 1000000000ull, ns_delta);
                    if (timer_info_state.tsc_hz != 0 && timer_info_state.tsc_hz != ~0ull) {
                        timer_info_state.tsc_calibrated = true;
                        timer_info_state.clocksource = "tsc";
                        tsc_base = cpu_read_tsc();
                        if (!hpet_now_ns(&ns_base)) ns_base = hpet_b;
                    }
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
        u64 now = cpu_read_tsc();
        u64 delta = now >= tsc_base ? now - tsc_base : 0;
        u64 ns_delta = rabbitbone_u64_mul_div_saturating(delta, 1000000000ull, timer_info_state.tsc_hz);
        return rabbitbone_u64_add_saturating(ns_base, ns_delta);
    }
    u64 hpet_ns = 0;
    if (hpet_now_ns(&hpet_ns)) return hpet_ns;
    return pit_ticks() * (1000000000ull / RABBITBONE_PIT_HZ);
}

void timer_get_info(timer_info_t *out) {
    if (!out) return;
    *out = timer_info_state;
    out->monotonic_ns = timer_now_ns();
}

void timer_format_status(char *out, usize out_len) {
    if (!out || out_len == 0) return;
    rabbitbone_buf_out_t bo;
    rabbitbone_buf_init(&bo, out, out_len);
    timer_info_t ti;
    timer_get_info(&ti);
    rabbitbone_buf_appendf(&bo, "timer: clocksource=%s now_ns=%llu pit=%u pit_hz=%llu hpet=%u tsc_supported=%u tsc_calibrated=%u tsc_hz=%llu\n",
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
#if defined(RABBITBONE_HOST_TEST)
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
