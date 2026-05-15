#include <rabbitbone_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_preemptinfo_t pi;
    au_memset(&pi, 0, sizeof(pi));
    if (au_preemptinfo(&pi) != 0) return 70;
    if (!pi.enabled) return 71;
    if (pi.quantum_ticks == 0 || pi.quantum_ticks > 1000u) return 72;
    if (pi.total_timer_ticks < pi.user_ticks) return 73;
    if (pi.total_timer_ticks < pi.kernel_ticks) return 74;

    au_schedinfo_t st;
    au_memset(&st, 0, sizeof(st));
    if (au_schedinfo(&st) != 0) return 75;
    if (!st.preempt_enabled || st.quantum_ticks != pi.quantum_ticks) return 76;
    if (st.queue_capacity == 0) return 77;
    if (au_preemptinfo((au_preemptinfo_t *)0) >= 0) return 78;
    return 0;
}
