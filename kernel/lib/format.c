#include <rabbitbone/format.h>
#include <rabbitbone/console.h>

void rabbitbone_buf_init(rabbitbone_buf_out_t *out, char *buf, usize cap) {
    if (!out) return;
    out->buf = buf;
    out->cap = cap;
    out->used = 0;
    if (buf && cap) buf[0] = 0;
}

void rabbitbone_buf_append_raw(rabbitbone_buf_out_t *out, const char *s) {
    if (!out || !out->buf || !out->cap || !s) return;
    if (out->used >= out->cap) out->used = out->cap - 1u;
    while (*s && out->used < out->cap - 1u) out->buf[out->used++] = *s++;
    out->buf[out->used < out->cap ? out->used : out->cap - 1u] = 0;
}

void rabbitbone_buf_vappendf(rabbitbone_buf_out_t *out, const char *fmt, __builtin_va_list ap) {
    if (!out || !out->buf || !out->cap || !fmt) return;
    if (out->used >= out->cap) out->used = out->cap - 1u;
    usize rem = out->cap - out->used;
    int wrote = kvsnprintf(out->buf + out->used, rem, fmt, ap);
    if (wrote <= 0) return;
    usize add = (usize)wrote;
    usize room = rem ? rem - 1u : 0u;
    if (add > room) add = room;
    out->used += add;
    out->buf[out->used < out->cap ? out->used : out->cap - 1u] = 0;
}

void rabbitbone_buf_appendf(rabbitbone_buf_out_t *out, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    rabbitbone_buf_vappendf(out, fmt, ap);
    __builtin_va_end(ap);
}
