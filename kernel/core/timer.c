#include <aurora/timer.h>
#include <aurora/drivers.h>
#include <aurora/arch/io.h>

void timer_sleep_ticks(u64 delta) {
    u64 start = pit_ticks();
    while ((pit_ticks() - start) < delta) {
        cpu_sti();
        cpu_hlt();
    }
}

bool timer_selftest(void) {
    u64 before = pit_ticks();
    timer_sleep_ticks(2);
    u64 after = pit_ticks();
    return (after - before) >= 2u;
}
