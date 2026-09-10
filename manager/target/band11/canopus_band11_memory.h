/* Exact q66cn .139 primitives. Static test ABI; device validation pending.
 * Evidence: targets/xiaomi-band-11-4.100.139/loader/native-audit.md. */
#ifndef CANOPUS_BAND11_MEMORY_H
#define CANOPUS_BAND11_MEMORY_H
#include <stdint.h>
#include <stddef.h>
#define B11_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define B11_MPU_BITMAP 0x200F5190u
#define B11_MPU_CTRL 0xE000ED94u
#define B11_MPU_RNR 0xE000ED98u
#define B11_MPU_RBAR 0xE000ED9Cu
#define B11_MPU_RLAR 0xE000EDA0u
static inline void b11_barrier(void) { __asm__ volatile("dsb sy\nisb sy" ::: "memory"); }
static inline uint32_t b11_irq_lock(void) {
    uint32_t p; __asm__ volatile("mrs %0, primask\ncpsid i" : "=r"(p) :: "memory"); return p;
}
static inline void b11_irq_unlock(uint32_t p) { __asm__ volatile("msr primask, %0" :: "r"(p) : "memory"); }
static inline int b11_context_ok(void) {
    uint32_t control, ipsr;
    __asm__ volatile("mrs %0, control\nmrs %1, ipsr" : "=r"(control), "=r"(ipsr));
    return !(control & 1u) && !ipsr && B11_REG(B11_MPU_CTRL) == 5u &&
           ((B11_REG(0xE000ED90u) >> 8) & 0xffu) == 8u &&
           B11_REG(0xE000EDC0u) == 0x00447722u;
}
static inline int b11_heap_contains(uintptr_t p, uint32_t size) {
    uintptr_t heap = B11_REG(0x200B01C8u);
    if (heap < 0x20000000u || heap > 0x20160000u - 0x24u) return 0;
    return p > B11_REG(heap + 0x1cu) && p < 0x20160000u &&
           size <= 0x20160000u - p && p + size <= B11_REG(heap + 0x20u);
}
static inline void *b11_alloc(uint32_t alignment, uint32_t size) {
    typedef void *(*fn)(void *, uint32_t, uint32_t);
    void *p = ((fn)(uintptr_t)0x0C3507E9u)((void *)(uintptr_t)B11_REG(0x200B01C8u), alignment, size);
    return p;
}
static inline void b11_free(void *p) {
    if (p) ((void (*)(void *))(uintptr_t)0x0C34CCB9u)(p);
}
/* The input ELF and loader bookkeeping are never executed. Keep them out of
 * the small kernel heap. mm_memalign takes an explicit heap; umm_free at
 * 0x0c34cd2c uses this same Umem global (not the separate libc pool wrapper). */
static inline void *b11_temp_alloc(uint32_t alignment, uint32_t size) {
    typedef void *(*fn)(void *, uint32_t, uint32_t);
    uintptr_t heap = B11_REG(0x200B2590u), p;
    if (heap != 0x3C356B40u) return 0;
    p = (uintptr_t)((fn)(uintptr_t)0x0C3507E9u)((void *)heap, alignment, size);
    if (!p) return 0;
    if (p <= B11_REG(heap + 0x1cu) || p >= 0x3D000000u ||
        size > 0x3D000000u - p || p + size > B11_REG(heap + 0x20u)) return 0;
    return (void *)p;
}
static inline void b11_temp_free(void *p) {
    if (p) ((void (*)(void *))(uintptr_t)0x0C34CD2Du)(p);
}
/* Reserve only 4..6, leaving region 7 available to firmware. The .139
 * scheduler uses MSPLIM/PSPLIM; region-7 stack-guard ownership is NOT proven.
 * RNR is restored and
 * the bitmap/hardware update is atomic with respect to interrupt scheduling. */
static inline int b11_map_exec(uint32_t base, uint32_t size) {
    uint32_t irq, old, id, end, rbar, rlar;
    int result = -1;
    if (!b11_context_ok() || (base & 31u) || !size || (size & 31u) ||
        !b11_heap_contains(base, size)) return -1;
    end = base + size - 1u;
    irq = b11_irq_lock(); old = B11_REG(B11_MPU_RNR);
    /* PMSAv8 overlapping enabled regions fault even when both allow access.
     * Check hardware too: the allocator bitmap alone is insufficient. */
    for (id = 0u; id < 8u; id++) {
        B11_REG(B11_MPU_RNR) = id;
        rbar = B11_REG(B11_MPU_RBAR); rlar = B11_REG(B11_MPU_RLAR);
        if ((rlar & 1u) && base <= (rlar | 31u) && end >= (rbar & ~31u)) goto done;
    }
    __asm__ volatile("dmb sy" ::: "memory");
    for (id = 4u; id < 7u; id++) {
        if (B11_REG(B11_MPU_BITMAP) & (1u << id)) continue;
        B11_REG(B11_MPU_RNR) = id;
        if (B11_REG(B11_MPU_RLAR) & 1u) continue;
        B11_REG(B11_MPU_BITMAP) |= 1u << id;
        B11_REG(B11_MPU_RBAR) = base | 6u; /* RO, executable */
        B11_REG(B11_MPU_RLAR) = (end & ~31u) | 3u; /* MAIR1 */
        b11_barrier();
        if (B11_REG(B11_MPU_RBAR) == (base | 6u) &&
            B11_REG(B11_MPU_RLAR) == ((end & ~31u) | 3u)) result = (int)id;
        else {
            B11_REG(B11_MPU_RLAR) = 0u;
            b11_barrier(); B11_REG(B11_MPU_BITMAP) &= ~(1u << id);
        }
        break;
    }
done:
    B11_REG(B11_MPU_RNR) = old; b11_barrier(); b11_irq_unlock(irq);
    return result;
}
static inline void b11_unmap(uint32_t id) {
    uint32_t irq, old;
    if (id < 4u || id >= 7u) return;
    irq = b11_irq_lock(); old = B11_REG(B11_MPU_RNR);
    B11_REG(B11_MPU_RNR) = id; B11_REG(B11_MPU_RLAR) = 0u;
    b11_barrier(); B11_REG(B11_MPU_BITMAP) &= ~(1u << id);
    B11_REG(B11_MPU_RNR) = old; b11_irq_unlock(irq);
}
static inline uint32_t b11_crc32(const void *data, uint32_t size) {
    const uint8_t *p = data; uint32_t crc = ~0u, bit;
    while (size--) { crc ^= *p++; for (bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u))); }
    return ~crc;
}
static inline int b11_read_file(const char *path, void *data, uint32_t size, uint32_t crc) {
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    int fd = ((int (*)(const char *, int, ...))(uintptr_t)0x0C342C55u)(path, 1);
    uint32_t done = 0; uint8_t extra; int32_t got;
    if (fd < 0) return -1;
    while (done < size) {
        got = ((read_fn)(uintptr_t)0x0C33D785u)(fd, (uint8_t *)data + done, size - done);
        if (got <= 0 || (uint32_t)got > size - done) break;
        done += (uint32_t)got;
    }
    got = ((read_fn)(uintptr_t)0x0C33D785u)(fd, &extra, 1);
    ((int (*)(int))(uintptr_t)0x0C33818Du)(fd);
    return done == size && got == 0 && b11_crc32(data, size) == crc ? 0 : -1;
}
#endif
