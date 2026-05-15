#ifndef RABBITBONE_LIBC_H
#define RABBITBONE_LIBC_H
#include <rabbitbone/types.h>
#if defined(__cplusplus)
extern "C" {
#endif

void *memset(void *dst, int value, usize n);
void *memcpy(void *dst, const void *src, usize n);
void *memmove(void *dst, const void *src, usize n);
int memcmp(const void *a, const void *b, usize n);
int bcmp(const void *a, const void *b, usize n);
usize strlen(const char *s);
usize strnlen(const char *s, usize max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, usize n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, usize n);
char *strchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
u64 strtou64(const char *s, const char **end, int base);

#if defined(__cplusplus)
}
#endif
#endif
