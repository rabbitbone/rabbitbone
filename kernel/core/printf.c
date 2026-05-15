#include <rabbitbone/console.h>
#include <rabbitbone/libc.h>

#ifndef RABBITBONE_HOST_TEST
#include <rabbitbone/drivers.h>
#endif


typedef struct outbuf {
    char *buf;
    usize cap;
    usize len;
    usize used;
    rabbitbone_vprintf_emit_fn emit;
    void *emit_ctx;
} outbuf_t;

#define PRINTF_MAX_WIDTH 4096
#define KPRINTF_CHUNK_SIZE 256u

static void out_flush(outbuf_t *out) {
    if (!out || !out->emit || !out->used) return;
    out->emit(out->buf, out->used, out->emit_ctx);
    out->used = 0;
}

static void out_emit_bytes(outbuf_t *out, const char *s, usize n) {
    if (!out || !s || !n) return;
    if (!out->emit) {
        if (out->cap && out->len < out->cap - 1u) {
            usize room = out->cap - 1u - out->len;
            usize take = n < room ? n : room;
            memcpy(out->buf + out->len, s, take);
        }
        if (out->len <= (usize)-1 - n) out->len += n;
        else out->len = (usize)-1;
        return;
    }
    if (!out->buf || !out->cap) {
        out->emit(s, n, out->emit_ctx);
        if (out->len <= (usize)-1 - n) out->len += n;
        else out->len = (usize)-1;
        return;
    }
    usize off = 0;
    while (off < n) {
        usize avail = out->cap - out->used;
        if (!avail) {
            out_flush(out);
            avail = out->cap;
        }
        usize take = n - off;
        if (take > avail) take = avail;
        memcpy(out->buf + out->used, s + off, take);
        out->used += take;
        off += take;
        if (out->used == out->cap) out_flush(out);
    }
    if (out->len <= (usize)-1 - n) out->len += n;
    else out->len = (usize)-1;
}

static void out_char(outbuf_t *out, char c) {
    out_emit_bytes(out, &c, 1u);
}

static void out_str(outbuf_t *out, const char *s) {
    if (!s) s = "(null)";
    out_emit_bytes(out, s, strlen(s));
}

static void out_repeat(outbuf_t *out, char c, int count) {
    while (count-- > 0) out_char(out, c);
}

static void out_uint(outbuf_t *out, u64 v, unsigned base, bool upper, int width, bool pad_zero) {
    char digits[65];
    const char *map = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    usize n = 0;
    do {
        digits[n++] = map[v % base];
        v /= base;
    } while (v);
    int pad = width - (int)n;
    out_repeat(out, pad_zero ? '0' : ' ', pad);
    while (n) out_char(out, digits[--n]);
}

static void out_int(outbuf_t *out, i64 v, int width, bool pad_zero) {
    if (v < 0) {
        out_char(out, '-');
        u64 u = (u64)(-(v + 1)) + 1u;
        out_uint(out, u, 10, false, width > 0 ? width - 1 : 0, pad_zero);
    } else {
        out_uint(out, (u64)v, 10, false, width, pad_zero);
    }
}

static int kvformat(outbuf_t *out, const char *fmt, __builtin_va_list ap) {
    if (!out) return 0;
    for (const char *p = fmt; p && *p; ++p) {
        if (*p != '%') {
            out_char(out, *p);
            continue;
        }
        ++p;
        bool pad_zero = false;
        int width = 0;
        if (*p == '0') { pad_zero = true; ++p; }
        while (*p >= '0' && *p <= '9') {
            int digit = *p - '0';
            if (width > (PRINTF_MAX_WIDTH - digit) / 10) width = PRINTF_MAX_WIDTH;
            else width = width * 10 + digit;
            ++p;
        }
        bool long_flag = false;
        bool long_long_flag = false;
        if (*p == 'l') {
            long_flag = true;
            ++p;
            if (*p == 'l') { long_long_flag = true; ++p; }
        }
        switch (*p) {
            case '%': out_char(out, '%'); break;
            case 'c': out_char(out, (char)__builtin_va_arg(ap, int)); break;
            case 's': out_str(out, __builtin_va_arg(ap, const char *)); break;
            case 'd':
            case 'i': {
                i64 v = long_long_flag ? __builtin_va_arg(ap, long long) : (long_flag ? __builtin_va_arg(ap, long) : __builtin_va_arg(ap, int));
                out_int(out, v, width, pad_zero);
                break;
            }
            case 'u': {
                u64 v = long_long_flag ? __builtin_va_arg(ap, unsigned long long) : (long_flag ? __builtin_va_arg(ap, unsigned long) : __builtin_va_arg(ap, unsigned int));
                out_uint(out, v, 10, false, width, pad_zero);
                break;
            }
            case 'x':
            case 'X': {
                u64 v = long_long_flag ? __builtin_va_arg(ap, unsigned long long) : (long_flag ? __builtin_va_arg(ap, unsigned long) : __builtin_va_arg(ap, unsigned int));
                out_uint(out, v, 16, *p == 'X', width, pad_zero);
                break;
            }
            case 'p': {
                uptr v = (uptr)__builtin_va_arg(ap, void *);
                out_str(out, "0x");
                out_uint(out, v, 16, false, (int)(sizeof(void*) * 2), true);
                break;
            }
            default:
                out_char(out, '%');
                if (*p) out_char(out, *p);
                break;
        }
    }
    return out->len > 2147483647u ? 2147483647 : (int)out->len;
}

int kvsnprintf(char *buf, usize cap, const char *fmt, __builtin_va_list ap) {
    if (!buf) cap = 0;
    outbuf_t out = { buf, cap, 0, 0, 0, 0 };
    int n = kvformat(&out, fmt, ap);
    if (cap) {
        usize pos = out.len < cap ? out.len : cap - 1;
        buf[pos] = 0;
    }
    return n;
}

int ksnprintf(char *buf, usize cap, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

int kvprintf_emit_buffered(rabbitbone_vprintf_emit_fn emit, void *ctx, char *chunk, usize chunk_cap, const char *fmt, __builtin_va_list ap) {
    if (!emit || !fmt) return 0;
    outbuf_t out = { chunk, chunk_cap, 0, 0, emit, ctx };
    int n = kvformat(&out, fmt, ap);
    out_flush(&out);
    return n;
}

#ifndef RABBITBONE_HOST_TEST
static void console_emit(const char *s, usize n, void *ctx) {
    (void)ctx;
    if (n) console_write_n(s, n);
}

int kprintf(const char *fmt, ...) {
    char chunk[KPRINTF_CHUNK_SIZE];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvprintf_emit_buffered(console_emit, 0, chunk, sizeof(chunk), fmt, ap);
    __builtin_va_end(ap);
    return n;
}
#else
int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
#endif
