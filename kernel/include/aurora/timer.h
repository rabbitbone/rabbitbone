#ifndef AURORA_TIMER_H
#define AURORA_TIMER_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

void timer_sleep_ticks(u64 delta);
bool timer_selftest(void);

#if defined(__cplusplus)
}
#endif
#endif
