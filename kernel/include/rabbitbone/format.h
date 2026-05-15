#ifndef RABBITBONE_FORMAT_H
#define RABBITBONE_FORMAT_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct rabbitbone_buf_out {
    char *buf;
    usize cap;
    usize used;
} rabbitbone_buf_out_t;

void rabbitbone_buf_init(rabbitbone_buf_out_t *out, char *buf, usize cap);
void rabbitbone_buf_append_raw(rabbitbone_buf_out_t *out, const char *s);
void rabbitbone_buf_appendf(rabbitbone_buf_out_t *out, const char *fmt, ...);

#if defined(__cplusplus)
}
#endif
#endif
