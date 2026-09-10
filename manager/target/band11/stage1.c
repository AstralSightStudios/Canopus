#include "canopus_band11_memory.h"
#include "canopus_band11_sizes.h"
int canopus_band11_stage1(volatile int32_t *mailbox) {
    void *image; int region, rc;
    *mailbox = -301;
    if (!b11_context_ok()) return -302;
    image = b11_alloc(32u, (B11_STAGE2_SIZE + 31u) & ~31u);
    if (!image) return -303;
    if (!b11_heap_contains((uintptr_t)image, (B11_STAGE2_SIZE + 31u) & ~31u) ||
        b11_read_file("/data/canopus/stage2.bin", image, B11_STAGE2_SIZE, B11_STAGE2_CRC)) {
        b11_free(image); return -304;
    }
    region = b11_map_exec((uint32_t)(uintptr_t)image, (B11_STAGE2_SIZE + 31u) & ~31u);
    if (region < 0) { b11_free(image); return -305; }
    *mailbox = -306;
    rc = ((int (*)(volatile int32_t *))((uintptr_t)image | 1u))(mailbox);
    b11_unmap((uint32_t)region); b11_free(image);
    return rc;
}
