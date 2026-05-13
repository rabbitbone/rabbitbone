#ifndef AURORA_TIMER_H
#define AURORA_TIMER_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct timer_info {
    bool pit_active;
    bool hpet_active;
    bool tsc_supported;
    bool tsc_calibrated;
    u64 pit_hz;
    u64 tsc_hz;
    u64 monotonic_ns;
    const char *clocksource;
} timer_info_t;

void timer_init_sources(void);
u64 timer_now_ns(void);
void timer_get_info(timer_info_t *out);
void timer_format_status(char *out, usize out_len);
void timer_sleep_ticks(u64 delta);
bool timer_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
