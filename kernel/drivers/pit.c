#include <aurora/drivers.h>
#include <aurora/arch/io.h>

static volatile u64 ticks;

#define PIT_BASE_HZ 1193182u

void pit_init(u32 hz) {
    if (hz == 0 || hz > PIT_BASE_HZ) hz = 100;
    u32 divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) divisor = 1;
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xff));
    outb(0x40, (u8)((divisor >> 8) & 0xff));
    ticks = 0;
}

void pit_irq(void) { ++ticks; }
u64 pit_ticks(void) { return ticks; }
