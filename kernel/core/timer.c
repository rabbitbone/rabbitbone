#include <aurora/timer.h>
#include <aurora/drivers.h>
#include <aurora/arch/io.h>
#include <aurora/spinlock.h>
#include <aurora/panic.h>

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
    timer_sleep_ticks(2);
    u64 after = pit_ticks();
    return (after - before) >= 2u;
}
