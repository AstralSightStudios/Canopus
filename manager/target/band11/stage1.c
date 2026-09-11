#include "canopus_band11_memory.h"
#include "canopus_band11_sizes.h"
int canopus_band11_stage1(volatile int32_t *mailbox) {
    uint32_t size = (B11_STAGE2_SIZE + 31u) & ~31u;
    void *image; uint32_t exec; int region, rc;
    *mailbox = -301;
    if (!b11_context_ok()) return -302;
    /* stage2 runs from Umem through the PSRAM instruction alias, the same way
     * this blob is itself entered from a Lua-owned Umem string. That keeps the
     * whole bootstrap out of the firmware's own Kmem, where a request the heap
     * cannot serve panics instead of failing. Kmem behind an MPU lease stays
     * as the fallback. */
    image = b11_temp_alloc(32u, size);
    exec = image ? b11_exec_alias((uintptr_t)image, size) : 0u;
    if (image && !exec) { b11_temp_free(image); image = 0; }
    if (image) {
        if (b11_read_file("/data/canopus/stage2.bin", image, B11_STAGE2_SIZE, B11_STAGE2_CRC)) {
            b11_temp_free(image); return -304;
        }
        /* -307 distinguishes "caches are not in the state the publish
         * sequence requires" from -305, which is a refused MPU lease. */
        if (b11_publish_code()) { b11_temp_free(image); return -307; }
        *mailbox = -306;
        rc = ((int (*)(volatile int32_t *))((uintptr_t)exec | 1u))(mailbox);
        b11_temp_free(image);
        return rc;
    }
    image = b11_alloc(32u, size);
    if (!image) return -303;
    if (!b11_heap_contains((uintptr_t)image, size) ||
        b11_read_file("/data/canopus/stage2.bin", image, B11_STAGE2_SIZE, B11_STAGE2_CRC)) {
        b11_free(image); return -304;
    }
    region = b11_map_exec((uint32_t)(uintptr_t)image, size);
    if (region < 0) { b11_free(image); return -305; }
    *mailbox = -306;
    rc = ((int (*)(volatile int32_t *))((uintptr_t)image | 1u))(mailbox);
    b11_unmap((uint32_t)region); b11_free(image);
    return rc;
}
