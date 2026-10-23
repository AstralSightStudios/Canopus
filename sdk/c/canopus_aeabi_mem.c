/*
 * canopus_aeabi_mem.c — ARM compiler-runtime memory helpers.
 *
 * Freestanding Clang lowers aggregate initialization and struct assignment
 * into calls to `__aeabi_mem*` even under `-ffreestanding -fno-builtin`,
 * because loop-idiom recognition is independent of builtin expansion. Those
 * calls would otherwise be undefined symbols, which the zero-import verifier
 * rejects — so every module and the Supervisor link this definition.
 *
 * Signatures follow the ARM RTABI (IHI0043): the `*set*`/`*clr*` families
 * take the size before the fill byte, and the `*4`/`*8` variants additionally
 * require that alignment.
 *
 * The stores are `volatile` so LLVM cannot fold a loop back into a call to
 * the very helper it defines, which would recurse forever.
 */
#include <stddef.h>

void __aeabi_memcpy(void *dest, const void *src, size_t n);
void __aeabi_memcpy4(void *dest, const void *src, size_t n);
void __aeabi_memcpy8(void *dest, const void *src, size_t n);
void __aeabi_memmove(void *dest, const void *src, size_t n);
void __aeabi_memmove4(void *dest, const void *src, size_t n);
void __aeabi_memmove8(void *dest, const void *src, size_t n);
void __aeabi_memset(void *dest, size_t n, int c);
void __aeabi_memset4(void *dest, size_t n, int c);
void __aeabi_memset8(void *dest, size_t n, int c);
void __aeabi_memclr(void *dest, size_t n);
void __aeabi_memclr4(void *dest, size_t n);
void __aeabi_memclr8(void *dest, size_t n);

void __aeabi_memcpy(void *dest, const void *src, size_t n)
{
    volatile unsigned char *d = dest;
    volatile const unsigned char *s = src;
    while (n--) *d++ = *s++;
}

void __aeabi_memcpy4(void *dest, const void *src, size_t n) { __aeabi_memcpy(dest, src, n); }
void __aeabi_memcpy8(void *dest, const void *src, size_t n) { __aeabi_memcpy(dest, src, n); }

void __aeabi_memmove(void *dest, const void *src, size_t n)
{
    volatile unsigned char *d = dest;
    volatile const unsigned char *s = src;
    if (d == s || n == 0) return;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
}

void __aeabi_memmove4(void *dest, const void *src, size_t n) { __aeabi_memmove(dest, src, n); }
void __aeabi_memmove8(void *dest, const void *src, size_t n) { __aeabi_memmove(dest, src, n); }

void __aeabi_memset(void *dest, size_t n, int c)
{
    volatile unsigned char *d = dest;
    while (n--) *d++ = (unsigned char)c;
}

void __aeabi_memset4(void *dest, size_t n, int c) { __aeabi_memset(dest, n, c); }
void __aeabi_memset8(void *dest, size_t n, int c) { __aeabi_memset(dest, n, c); }

void __aeabi_memclr(void *dest, size_t n)
{
    volatile unsigned char *d = dest;
    while (n--) *d++ = 0u;
}

void __aeabi_memclr4(void *dest, size_t n) { __aeabi_memclr(dest, n); }
void __aeabi_memclr8(void *dest, size_t n) { __aeabi_memclr(dest, n); }
