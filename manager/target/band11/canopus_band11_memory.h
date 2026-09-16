/* Exact q66cn .139 primitives. Static test ABI; device validation pending.
 * Evidence: targets/xiaomi-band-11-4.100.139/loader/native-audit.md. */
#ifndef CANOPUS_BAND11_MEMORY_H
#define CANOPUS_BAND11_MEMORY_H
#include <stdint.h>
#include "canopus_band11_addresses.h"
#include <stddef.h>
#define B11_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
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
    uintptr_t heap = B11_REG(B11_KMEM_SLOT);
    if (heap < 0x20000000u || heap > 0x20160000u - 0x24u) return 0;
    return p > B11_REG(heap + 0x1cu) && p < 0x20160000u &&
           size <= 0x20160000u - p && p + size <= B11_REG(heap + 0x20u);
}
/* mm_malloc 0x0c35056c ends its failure path with _assert("mm_malloc.c", 417,
 * "panic") for exactly the two well-known heaps (Umem *0x200b2590 and Kmem
 * *0x200b01c8), so a request this heap cannot satisfy takes the whole device
 * down instead of returning NULL. Every allocation here is therefore gated on
 * mm_mallinfo first.
 *
 * mm_mallinfo 0x0c34f0a0 is `struct mallinfo mm_mallinfo(heap)`: AAPCS returns
 * the 7-word record through the hidden r0 pointer with the heap in r1. Its
 * per-node callback 0x0c347d0c fills ordblks/aordblks/mxordblk/uordblks/
 * fordblks, and mm_foreach 0x0c34f028 holds the heap lock across the walk.
 * mxordblk is the largest free node's `size & ~3` — the very quantity
 * mm_malloc compares its rounded request against, so `request <= mxordblk`
 * is the allocator's own success test, not an estimate. */
struct b11_mallinfo {
    uint32_t arena, ordblks, aordblks, mxordblk, uordblks, fordblks, usmblks;
};
/* mm_malloc rounds a request to (max(size,12)+11)&~7 before matching a free
 * node. mm_memalign 0x0c3507e8 forwards the request unchanged while the
 * alignment is <= 8 and otherwise asks for 2*max(align,16) + align8(size).
 * Reproduce both so the gate tests what will actually be requested. */
static inline uint32_t b11_alloc_request(uint32_t alignment, uint32_t size) {
    uint32_t want = size < 12u ? 12u : size;
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u ||
        alignment >= 0x7FFFFFFFu) return 0xFFFFFFFFu;
    if (alignment > 8u) {
        uint32_t slack = alignment < 16u ? 16u : alignment;
        if (want > 0xFFFFFFFFu - 7u) return 0xFFFFFFFFu;
        want = (want + 7u) & ~7u;
        if (slack > 0x3FFFFFFFu || want > 0xFFFFFFFFu - 2u * slack) return 0xFFFFFFFFu;
        want += 2u * slack;
    }
    if (want > 0xFFFFFFFFu - 11u) return 0xFFFFFFFFu;
    return (want + 11u) & ~7u;
}
/* Leave a chunk of headroom: another task may allocate between the query and
 * the request, and a false negative only fails one module load. */
#define B11_ALLOC_MARGIN 256u
static inline int b11_heap_can_alloc(uintptr_t heap, uint32_t alignment,
                                     uint32_t size) {
    typedef void *(*fn)(struct b11_mallinfo *, void *);
    struct b11_mallinfo info;
    uint32_t need = b11_alloc_request(alignment, size);
    if (heap == 0u || need == 0xFFFFFFFFu ||
        need > 0xFFFFFFFFu - B11_ALLOC_MARGIN) return 0;
    info.mxordblk = 0u;
    ((fn)(uintptr_t)B11_MM_MALLINFO)(&info, (void *)heap);
    return info.mxordblk >= need + B11_ALLOC_MARGIN;
}
static inline void *b11_alloc(uint32_t alignment, uint32_t size) {
    typedef void *(*fn)(void *, uint32_t, uint32_t);
    uintptr_t heap = B11_REG(B11_KMEM_SLOT);
    void *p;
    if (!b11_heap_can_alloc(heap, alignment, size)) return 0;
    p = ((fn)(uintptr_t)B11_MM_MEMALIGN)((void *)heap, alignment, size);
    return p;
}
static inline void b11_free(void *p) {
    if (p) ((void (*)(void *))(uintptr_t)B11_KMEM_FREE)(p);
}
/* The input ELF and loader bookkeeping are never executed. Keep them out of
 * the small kernel heap: Kmem is 2010e9e0..2015f2bc (~322 KiB, most of it
 * already resident firmware) while Umem is 3c356b40..3cfefe00. mm_memalign
 * takes an explicit heap; umm_free at 0x0c34cd2c uses this same Umem global
 * (not the separate libc pool wrapper). */
static inline void *b11_temp_alloc(uint32_t alignment, uint32_t size) {
    typedef void *(*fn)(void *, uint32_t, uint32_t);
    uintptr_t heap = B11_REG(B11_UMEM_SLOT), p;
    if (heap != B11_UMEM_DESCRIPTOR) return 0;
    if (!b11_heap_can_alloc(heap, alignment, size)) return 0;
    p = (uintptr_t)((fn)(uintptr_t)B11_MM_MEMALIGN)((void *)heap, alignment, size);
    if (!p) return 0;
    if (p <= B11_REG(heap + 0x1cu) || p >= 0x3D000000u ||
        size > 0x3D000000u - p || p + size > B11_REG(heap + 0x20u)) return 0;
    return (void *)p;
}
static inline void b11_temp_free(void *p) {
    if (p) ((void (*)(void *))(uintptr_t)B11_UMEM_FREE)(p);
}
/* PSRAM is visible through more than one window. The startup copy 0x0c0c024c
 * writes the PSRAM image at 0x3c000000 while its code runs at 0x1c000000
 * (delta 0x20000000), which the privileged default Code map covers with
 * MPU_CTRL=5 — stage1 already executes Lua-owned Umem bytes that way on
 * hardware. Its read-only data is addressed through the 0x3c view (the blob
 * holds 362 words pointing into 0x3c0xxxxx against 106 odd 0x1c... callables)
 * and its writable data through a third window at 0x38280000, so the windows
 * are per-attribute, not interchangeable. Literal pools inside .text are still
 * read through 0x1c on every PC-relative load, which is why the executable and
 * read-only extent may share the code view.
 *
 * A Umem-resident image therefore executes at `address - 0x20000000` with no
 * MPU lease, leaving Kmem — the firmware's own ~322 KiB heap — untouched. */
#define B11_PSRAM_EXEC_DELTA 0x20000000u
/* Returns the instruction view of a Umem range, or 0 when the range is not
 * Umem or an enabled MPU region overrides the default map there. */
static inline uint32_t b11_exec_alias(uintptr_t data, uint32_t size) {
    uint32_t irq, old, id, base, end, rbar, rlar;
    uint32_t result;
    if (data < 0x3C000000u || data >= 0x3D000000u || !size ||
        size > 0x3D000000u - data) return 0;
    base = (uint32_t)(data - B11_PSRAM_EXEC_DELTA);
    end = base + size - 1u;
    result = base;
    irq = b11_irq_lock(); old = B11_REG(B11_MPU_RNR);
    for (id = 0u; id < 8u; id++) {
        B11_REG(B11_MPU_RNR) = id;
        rbar = B11_REG(B11_MPU_RBAR); rlar = B11_REG(B11_MPU_RLAR);
        if ((rlar & 1u) && base <= (rlar | 31u) && end >= (rbar & ~31u)) {
            result = 0u;
            break;
        }
    }
    B11_REG(B11_MPU_RNR) = old; b11_barrier(); b11_irq_unlock(irq);
    return result;
}
/* Make bytes written through the data view fetchable through the code view:
 * D clean-all 0x0c93021a, DSB/ISB 0x0c91e542, the enabled I cache's
 * clean/invalidate-all branch 0x0c0c181e, DSB/ISB again. This is the exact
 * sequence the Lua bootstrap runs before entering stage1, and each leaf
 * establishes its own arguments and clobbers only r0/r2/r3. Both controllers
 * must already be enabled; the I-cache leaf would otherwise take its enable
 * branch and change cache configuration. */
static inline int b11_publish_code(void) {
    typedef void (*leaf_fn)(void);
    if (!(B11_REG(0x07FFA000u) & 1u) || !(B11_REG(0x07FFC000u) & 1u)) return -1;
    ((leaf_fn)(uintptr_t)B11_CACHE_D_CLEAN)();
    ((leaf_fn)(uintptr_t)B11_CACHE_BARRIER)();
    ((leaf_fn)(uintptr_t)B11_CACHE_I_INVALIDATE)();
    ((leaf_fn)(uintptr_t)B11_CACHE_BARRIER)();
    return 0;
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
    int fd = ((int (*)(const char *, int, ...))(uintptr_t)FW_NUTTX_OPEN)(path, 1);
    uint32_t done = 0; uint8_t extra; int32_t got;
    if (fd < 0) return -1;
    while (done < size) {
        got = ((read_fn)(uintptr_t)FW_NUTTX_READ)(fd, (uint8_t *)data + done, size - done);
        if (got <= 0 || (uint32_t)got > size - done) break;
        done += (uint32_t)got;
    }
    got = ((read_fn)(uintptr_t)FW_NUTTX_READ)(fd, &extra, 1);
    ((int (*)(int))(uintptr_t)FW_NUTTX_CLOSE)(fd);
    return done == size && got == 0 && b11_crc32(data, size) == crc ? 0 : -1;
}
#endif
