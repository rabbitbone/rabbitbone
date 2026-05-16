#ifndef RABBITBONE_KTEST_H
#define RABBITBONE_KTEST_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ktest_totals {
    u32 passed;
    u32 failed;
    u32 skipped;
    u32 suites;
} ktest_totals_t;

#define RABBITBONE_KTEST_FLAG_SMP_EXPANDED (1u << 0)

bool ktest_run_all(void);
bool ktest_run_all_flags(u32 flags);
void ktest_get_last_totals(ktest_totals_t *out);

#if defined(__cplusplus)
}
#endif
#endif
