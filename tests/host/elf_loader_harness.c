/* Loads a real ET_REL artifact through the portable loader on the host. */
#include "canopus_elf32_loader.h"

#include <stdio.h>
#include <stdlib.h>

struct harness {
    void *allocation;
    uint32_t allocation_size;
    uint32_t invokes[128];
    uint32_t invoke_count;
    uint32_t finalized;
};

static void *allocate_image(void *cookie, uint32_t size, uint32_t alignment,
                            uint32_t *target_base)
{
    struct harness *h = cookie;
    void *allocation = aligned_alloc(alignment, size);
    h->allocation = allocation;
    h->allocation_size = size;
    *target_base = UINT32_C(0x21000000);
    return allocation;
}

static void release_image(void *cookie, void *allocation, uint32_t size)
{
    struct harness *h = cookie;
    (void)size;
    free(allocation);
    h->allocation = NULL;
}

static int finalize_image(void *cookie, void *allocation, uint32_t target_base,
                          uint32_t size,
                          const struct canopus_elf_region *regions,
                          uint32_t region_count)
{
    struct harness *h = cookie;
    uint32_t i;
    uint32_t end = 0;
    (void)allocation;
    (void)target_base;

    if (region_count == 0 || region_count > 3) {
        return -1;
    }
    for (i = 0; i < region_count; i++) {
        if ((regions[i].offset & 31u) != 0 || (regions[i].size & 31u) != 0 ||
            regions[i].offset != end || regions[i].size == 0) {
            return -1;
        }
        end = regions[i].offset + regions[i].size;
    }
    if (end > size) {
        return -1;
    }
    h->finalized = 1;
    return 0;
}

static int record_invoke(void *cookie, uint32_t callable)
{
    struct harness *h = cookie;
    if ((callable & 1u) == 0 || h->invoke_count >= 128) {
        return -1;
    }
    h->invokes[h->invoke_count++] = callable;
    return 0;
}

int main(int argc, char **argv)
{
    struct harness h = { 0 };
    struct canopus_elf_loader_ops ops = {
        .cookie = &h,
        .allocate = allocate_image,
        .release = release_image,
        .finalize = finalize_image,
        .invoke = record_invoke,
    };
    struct canopus_elf_module module;
    FILE *file;
    long length;
    uint8_t *data;
    int rc;

    if (argc != 2) {
        return 2;
    }
    file = fopen(argv[1], "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        return 2;
    }
    data = malloc((size_t)length);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        return 2;
    }
    fclose(file);
    rc = canopus_elf32_load(data, (uint32_t)length, &ops, &module);
    free(data);
    if (rc != 0) {
        fprintf(stderr, "load failed: %d\n", rc);
        return 1;
    }
    printf("loaded=%u regions=%u preinit=%u init=%u fini=%u invoked=%u\n",
           module.allocation_size, module.region_count, module.preinit_count,
           module.init_count, module.fini_count, h.invoke_count);
    if (!h.finalized || module.init_count == 0 || module.fini_count == 0) {
        return 1;
    }
    canopus_elf32_unload(&ops, &module);
    if (h.allocation != NULL) {
        return 1;
    }
    return 0;
}
