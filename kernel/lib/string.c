#include <rabbitbone/libc.h>

void *memset(void *dst, int value, usize n) {
    u8 *d = (u8 *)dst;
    for (usize i = 0; i < n; ++i) d[i] = (u8)value;
    return dst;
}

void *memcpy(void *dst, const void *src, usize n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    for (usize i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, usize n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (usize i = 0; i < n; ++i) d[i] = s[i];
    } else {
        for (usize i = n; i > 0; --i) d[i - 1] = s[i - 1];
    }
    return dst;
}

int memcmp(const void *a, const void *b, usize n) {
    const u8 *aa = (const u8 *)a;
    const u8 *bb = (const u8 *)b;
    for (usize i = 0; i < n; ++i) {
        if (aa[i] != bb[i]) return (int)aa[i] - (int)bb[i];
    }
    return 0;
}

int bcmp(const void *a, const void *b, usize n) {
    return memcmp(a, b, n);
}

usize strlen(const char *s) {
    usize n = 0;
    while (s && s[n]) ++n;
    return n;
}

usize strnlen(const char *s, usize max) {
    usize n = 0;
    if (!s) return 0;
    while (n < max && s[n]) ++n;
    return n;
}

int strcmp(const char *a, const char *b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *a == *b) { ++a; ++b; }
    return (int)(u8)*a - (int)(u8)*b;
}

int strncmp(const char *a, const char *b, usize n) {
    if (a == b || n == 0) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (usize i = 0; i < n; ++i) {
        u8 ca = (u8)a[i];
        u8 cb = (u8)b[i];
        if (ca != cb || ca == 0 || cb == 0) return (int)ca - (int)cb;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++) != 0) {}
    return ret;
}

char *strncpy(char *dst, const char *src, usize n) {
    usize i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = 0;
    return dst;
}


usize strlcpy(char *dst, const char *src, usize size) {
    if (!src) {
        if (dst && size) dst[0] = 0;
        return 0;
    }
    usize slen = strlen(src);
    if (!dst || size == 0) return slen;
    usize copy = slen;
    if (copy >= size) copy = size - 1u;
    if (copy) memcpy(dst, src, copy);
    dst[copy] = 0;
    return slen;
}

usize strlcat(char *dst, const char *src, usize size) {
    if (!src) return dst ? strnlen(dst, size) : 0;
    usize dlen = strnlen(dst, size);
    usize slen = strlen(src);
    if (!dst || dlen >= size) return size + slen;
    usize room = size - dlen;
    usize copy = slen;
    if (copy >= room) copy = room - 1u;
    if (copy) memcpy(dst + dlen, src, copy);
    dst[dlen + copy] = 0;
    return dlen + slen;
}

void memzero_explicit(void *ptr, usize len) {
    volatile u8 *p = (volatile u8 *)ptr;
    while (len--) *p++ = 0;
}

char *strchr(const char *s, int c) {
    if (!s) return 0;
    while (*s) {
        if (*s == (char)c) return (char *)s;
        ++s;
    }
    return c == 0 ? (char *)s : 0;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return 0;
    usize nl = strlen(needle);
    if (nl == 0) return (char *)haystack;
    for (const char *p = haystack; *p; ++p) {
        if (strncmp(p, needle, nl) == 0) return (char *)p;
    }
    return 0;
}

static int digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

u64 strtou64(const char *s, const char **end, int base) {
    const char *p = s;
    if (!s) { if (end) *end = s; return 0; }
    if (base != 0 && (base < 2 || base > 16)) { if (end) *end = s; return 0; }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; ++p; }
        else base = 10;
    }
    u64 value = 0;
    int any = 0;
    int overflow = 0;
    for (;;) {
        int d = digit_value(*p);
        if (d < 0 || d >= base) break;
        if (value > (0xffffffffffffffffull - (u64)d) / (u64)base) {
            overflow = 1;
            value = 0xffffffffffffffffull;
            ++p;
            while ((d = digit_value(*p)) >= 0 && d < base) ++p;
            break;
        }
        value = value * (u64)base + (u64)d;
        ++p;
        any = 1;
    }
    if (end) *end = (any || overflow) ? p : s;
    return value;
}
