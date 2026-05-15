#include <rabbitbone/drivers.h>
#include <rabbitbone/arch/io.h>

static volatile u64 ticks;

#define PIT_BASE_HZ 1193182u
#define PIT_MAX_DIVISOR 65535u
#define PIT_DEFAULT_HZ 100u

static u16 pit_divisor_for_hz(u32 hz) {
    if (hz == 0 || hz > PIT_BASE_HZ) hz = PIT_DEFAULT_HZ;
    u32 divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > PIT_MAX_DIVISOR) divisor = PIT_MAX_DIVISOR;
    return (u16)divisor;
}

void pit_init(u32 hz) {
    u16 divisor = pit_divisor_for_hz(hz);
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xffu));
    outb(0x40, (u8)((divisor >> 8u) & 0xffu));
    ticks = 0;
}

void pit_irq(void) { __atomic_add_fetch(&ticks, 1u, __ATOMIC_RELAXED); }
u64 pit_ticks(void) { return __atomic_load_n(&ticks, __ATOMIC_RELAXED); }

bool pit_selftest(void) {
    return pit_divisor_for_hz(0) == (u16)(PIT_BASE_HZ / PIT_DEFAULT_HZ) &&
           pit_divisor_for_hz(PIT_BASE_HZ + 1u) == (u16)(PIT_BASE_HZ / PIT_DEFAULT_HZ) &&
           pit_divisor_for_hz(PIT_BASE_HZ) == 1u &&
           pit_divisor_for_hz(1u) == PIT_MAX_DIVISOR;
}
