#include <aurora/format.h>
#include <aurora/console.h>

void aurora_buf_init(aurora_buf_out_t *out, char *buf, usize cap) {
    if (!out) return;
    out->buf = buf;
    out->cap = cap;
    out->used = 0;
    if (buf && cap) buf[0] = 0;
}

void aurora_buf_append_raw(aurora_buf_out_t *out, const char *s) {
    if (!out || !out->buf || !out->cap || !s) return;
    while (*s && out->used + 1u < out->cap) out->buf[out->used++] = *s++;
    out->buf[out->used < out->cap ? out->used : out->cap - 1u] = 0;
}

void aurora_buf_appendf(aurora_buf_out_t *out, const char *fmt, ...) {
    char tmp[192];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(tmp, sizeof(tmp), fmt, ap);
    __builtin_va_end(ap);
    aurora_buf_append_raw(out, tmp);
}
