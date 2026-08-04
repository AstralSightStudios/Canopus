/*
 * canopus_module.c — module descriptor validation and bounded buffer helper.
 */
#include "canopus_runtime.h"
#include "canopus_memory.h"

int canopus_buf_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t n;
    if (dst == 0 || capacity == 0 || src == 0) {
        return -1;
    }
    n = (uint32_t)canopus_strlen(src);
    if (n >= capacity) {
        if (capacity > 1u) {
            canopus_memcpy(dst, src, capacity - 1u);
            dst[capacity - 1u] = '\0';
        } else {
            dst[0] = '\0';
        }
        return -1; /* truncated */
    }
    canopus_memcpy(dst, src, n);
    dst[n] = '\0';
    return (int)n;
}

/* Validates the module descriptor header and identity fields. Returns 0
 * when the descriptor is well-formed for the current ABI. */
int canopus_module_descriptor_check(const struct canopus_module_descriptor_v1 *d)
{
    if (d == 0) {
        return -1;
    }
    if (d->struct_size < offsetof(struct canopus_module_descriptor_v1, query) + 4u) {
        return -1;
    }
    if (d->abi_major != CANOPUS_ABI_MAJOR) {
        return -1;
    }
    return 0;
}
