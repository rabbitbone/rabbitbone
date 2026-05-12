#include <aurora/console.h>
#include <aurora/libc.h>

#ifndef AURORA_HOST_TEST
#include <aurora/drivers.h>
#endif

typedef struct outbuf {
    char *buf;
    usize cap;
    usize len;
} outbuf_t;

#define PRINTF_MAX_WIDTH 4096

static void out_char(outbuf_t *out, char c) {
    if (out->cap && out->len < out->cap - 1u) out->buf[out->len] = c;
    if (out->len != (usize)-1) ++out->len;
}

static void out_str(outbuf_t *out, const char *s) {
    if (!s) s = "(null)";
    while (*s) out_char(out, *s++);
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

int kvsnprintf(char *buf, usize cap, const char *fmt, __builtin_va_list ap) {
    outbuf_t out = { buf, cap, 0 };
    for (const char *p = fmt; p && *p; ++p) {
        if (*p != '%') {
            out_char(&out, *p);
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
            case '%': out_char(&out, '%'); break;
            case 'c': out_char(&out, (char)__builtin_va_arg(ap, int)); break;
            case 's': out_str(&out, __builtin_va_arg(ap, const char *)); break;
            case 'd':
            case 'i': {
                i64 v = long_long_flag ? __builtin_va_arg(ap, long long) : (long_flag ? __builtin_va_arg(ap, long) : __builtin_va_arg(ap, int));
                out_int(&out, v, width, pad_zero);
                break;
            }
            case 'u': {
                u64 v = long_long_flag ? __builtin_va_arg(ap, unsigned long long) : (long_flag ? __builtin_va_arg(ap, unsigned long) : __builtin_va_arg(ap, unsigned int));
                out_uint(&out, v, 10, false, width, pad_zero);
                break;
            }
            case 'x':
            case 'X': {
                u64 v = long_long_flag ? __builtin_va_arg(ap, unsigned long long) : (long_flag ? __builtin_va_arg(ap, unsigned long) : __builtin_va_arg(ap, unsigned int));
                out_uint(&out, v, 16, *p == 'X', width, pad_zero);
                break;
            }
            case 'p': {
                uptr v = (uptr)__builtin_va_arg(ap, void *);
                out_str(&out, "0x");
                out_uint(&out, v, 16, false, (int)(sizeof(void*) * 2), true);
                break;
            }
            default:
                out_char(&out, '%');
                if (*p) out_char(&out, *p);
                break;
        }
    }
    if (cap) {
        usize pos = out.len < cap ? out.len : cap - 1;
        buf[pos] = 0;
    }
    return out.len > 2147483647u ? 2147483647 : (int)out.len;
}

int ksnprintf(char *buf, usize cap, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(buf, cap, fmt, ap);
    __builtin_va_end(ap);
    return n;
}

#ifndef AURORA_HOST_TEST
int kprintf(const char *fmt, ...) {
    char tmp[1024];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = kvsnprintf(tmp, sizeof(tmp), fmt, ap);
    __builtin_va_end(ap);
    console_write(tmp);
    return n;
}
#else
int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
#endif
