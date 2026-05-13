#ifndef AURORA_FORMAT_H
#define AURORA_FORMAT_H
#include <aurora/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct aurora_buf_out {
    char *buf;
    usize cap;
    usize used;
} aurora_buf_out_t;

void aurora_buf_init(aurora_buf_out_t *out, char *buf, usize cap);
void aurora_buf_append_raw(aurora_buf_out_t *out, const char *s);
void aurora_buf_appendf(aurora_buf_out_t *out, const char *fmt, ...);

#if defined(__cplusplus)
}
#endif
#endif
