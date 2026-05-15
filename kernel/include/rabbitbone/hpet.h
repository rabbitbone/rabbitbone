#ifndef RABBITBONE_HPET_H
#define RABBITBONE_HPET_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct hpet_info {
    bool present;
    bool enabled;
    u64 base;
    u32 period_fs;
    u32 counter_bits;
    u32 comparator_count;
    u64 frequency_hz;
} hpet_info_t;

void hpet_init(void);
const hpet_info_t *hpet_get_info(void);
bool hpet_now_ns(u64 *out_ns);
bool hpet_selftest(void);
void hpet_format_status(char *out, usize out_len);

#if defined(__cplusplus)
}
#endif
#endif
