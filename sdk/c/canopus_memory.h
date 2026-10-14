/*
 * canopus_memory.h — minimal freestanding memory helpers.
 *
 * The runtime must not depend on libc's <string.h> (absent on bare-metal
 * toolchains). These tiny loops are the only memory primitives the
 * portable runtime uses.
 */
#ifndef CANOPUS_MEMORY_H
#define CANOPUS_MEMORY_H

#include <stddef.h>

static inline void *canopus_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static inline void *canopus_memset(void *dst, int c, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

static inline size_t canopus_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

/* Bounded strlen: never reads more than `max` characters. Returns max when
 * no NUL is found within that window, so callers can reject unterminated
 * fixed-size arrays instead of reading past them. */
static inline size_t canopus_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

static inline int canopus_strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline char *canopus_strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

#endif /* CANOPUS_MEMORY_H */
