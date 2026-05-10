#include <aurora_sys.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    au_schedinfo_t before;
    au_schedinfo_t after;
    if (au_schedinfo(&before) != 0) return 1;
    if (before.queue_capacity < 1) return 2;
    if (au_yield() != 0) return 3;
    if (au_sleep(0) < 0) return 4;
    if (au_schedinfo(&after) != 0) return 5;
    if (after.total_yields < before.total_yields + 1) return 6;
    if (after.total_sleeps < before.total_sleeps + 1) return 7;
    au_exit(0);
    return 0;
}
