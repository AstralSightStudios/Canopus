/* Freestanding ELF32 ARM ET_REL loader. */
#ifndef CANOPUS_ELF32_LOADER_H
#define CANOPUS_ELF32_LOADER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_ELF32_MAX_SECTIONS 256u
#define CANOPUS_ELF32_MAX_ARRAY_ENTRIES 64u

struct canopus_elf_section_info {
    uint32_t memory_offset;
    uint32_t kind;
    uint8_t loaded;
    uint8_t reserved[3];
};

#define CANOPUS_ELF32_SCRATCH_SIZE \
    (CANOPUS_ELF32_MAX_SECTIONS * sizeof(struct canopus_elf_section_info))

#define CANOPUS_ELF_LOAD_OK             0
#define CANOPUS_ELF_LOAD_INVALID       -1
#define CANOPUS_ELF_LOAD_UNSUPPORTED   -2
#define CANOPUS_ELF_LOAD_NOMEM         -3
#define CANOPUS_ELF_LOAD_RELOCATION    -4
#define CANOPUS_ELF_LOAD_FINALIZE      -5
#define CANOPUS_ELF_LOAD_CONSTRUCTORS  -6

enum canopus_elf_region_kind {
    CANOPUS_ELF_REGION_EXEC = 1,
    CANOPUS_ELF_REGION_RO = 2,
    CANOPUS_ELF_REGION_RW = 3,
};

struct canopus_elf_region {
    uint32_t offset;
    uint32_t size;
    uint32_t kind;
};

struct canopus_elf_loader_ops {
    void *cookie;
    /* Returns writable storage and its 32-bit target virtual base. */
    void *(*allocate)(void *cookie, uint32_t size, uint32_t alignment,
                      uint32_t *target_base);
    void (*release)(void *cookie, void *allocation, uint32_t size);
    /* Establish permissions and perform all required D/I-cache maintenance. */
    int (*finalize)(void *cookie, void *allocation, uint32_t target_base,
                    uint32_t size, const struct canopus_elf_region *regions,
                    uint32_t region_count);
    /* Invokes one relocated constructor at an odd Thumb callable address. */
    int (*invoke)(void *cookie, uint32_t callable);
};

struct canopus_elf_module {
    void *allocation;
    uint32_t target_base;
    uint32_t allocation_size;
    struct canopus_elf_region regions[3];
    uint32_t region_count;
    uint32_t preinit[CANOPUS_ELF32_MAX_ARRAY_ENTRIES];
    uint32_t preinit_count;
    uint32_t init[CANOPUS_ELF32_MAX_ARRAY_ENTRIES];
    uint32_t init_count;
    uint32_t fini[CANOPUS_ELF32_MAX_ARRAY_ENTRIES];
    uint32_t fini_count;
};

/* Parses, lays out, relocates, finalizes, then invokes preinit/init arrays.
 * scratch must point to writable CANOPUS_ELF32_SCRATCH_SIZE bytes. */
int canopus_elf32_load(const uint8_t *elf, uint32_t elf_size,
                       const struct canopus_elf_loader_ops *ops,
                       struct canopus_elf_module *module, void *scratch);

/* Invokes fini entries in reverse order, then releases the allocation. */
void canopus_elf32_unload(const struct canopus_elf_loader_ops *ops,
                          struct canopus_elf_module *module);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_ELF32_LOADER_H */
