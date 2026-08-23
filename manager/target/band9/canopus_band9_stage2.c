/* Band-9 position-independent stage-2 loader entered through NSH exec. */
#include "canopus_elf32_loader.h"
#include "canopus_memory.h"
#include "canopus_band9_loader_config.h"

#include <stdint.h>

#ifndef CANOPUS_BAND9_SUPERVISOR_SIZE
#error "CANOPUS_BAND9_SUPERVISOR_SIZE is required"
#endif

#define SUPERVISOR_PATH "/data/canopus/supervisor.elf"
#define FW_OPEN CANOPUS_FW_OPEN
#define FW_CLOSE CANOPUS_FW_CLOSE
#define FW_READ CANOPUS_FW_READ
#define FW_MEMALIGN CANOPUS_FW_MEMALIGN
#define FW_FREE CANOPUS_FW_FREE
#define FW_MPU_ALLOC CANOPUS_FW_MPU_ALLOC
#define FW_MPU_CONFIGURE CANOPUS_FW_MPU_CONFIGURE
#define FW_MPU_RELEASE CANOPUS_FW_MPU_RELEASE

struct stage2_state {
    uint8_t regions[3];
    uint32_t region_count;
};

static void *image_allocate(void *cookie, uint32_t size, uint32_t alignment,
                            uint32_t *target_base)
{
    typedef void *(*fn)(uint32_t, uint32_t);
    void *p;
    (void)cookie;
    p = ((fn)(uintptr_t)FW_MEMALIGN)(alignment, size);
    if (p != 0) *target_base = (uint32_t)(uintptr_t)p;
    return p;
}

static void image_release(void *cookie, void *allocation, uint32_t size)
{
    typedef void (*free_fn)(void *);
    typedef void (*release_fn)(uint32_t);
    struct stage2_state *state = cookie;
    (void)size;
    while (state->region_count != 0u) {
        uint32_t region;
        state->region_count--;
        region = state->regions[state->region_count];
        *(volatile uint32_t *)(uintptr_t)CANOPUS_MPU_RNR = region;
        *(volatile uint32_t *)(uintptr_t)CANOPUS_MPU_RLAR = 0u;
        __asm__ volatile("dsb sy\n"
                         "isb sy\n"
                         ::: "memory");
        ((release_fn)(uintptr_t)FW_MPU_RELEASE)(region);
    }
    ((free_fn)(uintptr_t)FW_FREE)(allocation);
}

static int image_finalize(void *cookie, void *allocation, uint32_t target_base,
                          uint32_t size,
                          const struct canopus_elf_region *regions,
                          uint32_t count)
{
    typedef uint32_t (*alloc_fn)(void);
    typedef int (*configure_fn)(uint32_t, uint32_t, uint32_t,
                                uint32_t, uint32_t);
    struct stage2_state *state = cookie;
    uint32_t i;
    uint32_t physical_count;
    uint32_t id;
    uint32_t base;
    uint32_t length;
    uint32_t access;
    (void)allocation;
    (void)size;

    if (count < 2u || count > 3u ||
        regions[0].kind != CANOPUS_ELF_REGION_EXEC) return -1;
    physical_count = regions[1].kind == CANOPUS_ELF_REGION_RO ?
                     count - 1u : count;
    for (i = 0; i < physical_count; i++) {
        id = ((alloc_fn)(uintptr_t)FW_MPU_ALLOC)();
        if (id >= CANOPUS_BAND9_MPU_REGION_COUNT) return -1;
        state->regions[state->region_count++] = (uint8_t)id;
        if (i == 0u && regions[1].kind == CANOPUS_ELF_REGION_RO) {
            base = target_base + regions[0].offset;
            length = regions[0].size + regions[1].size;
            access = CANOPUS_BAND9_EXEC_ACCESS_ATTR;
        } else {
            uint32_t logical = i +
                (regions[1].kind == CANOPUS_ELF_REGION_RO ? 1u : 0u);
            base = target_base + regions[logical].offset;
            length = regions[logical].size;
            access = regions[logical].kind == CANOPUS_ELF_REGION_EXEC ?
                     CANOPUS_BAND9_EXEC_ACCESS_ATTR :
                     (regions[logical].kind == CANOPUS_ELF_REGION_RW ?
                      CANOPUS_BAND9_RW_ACCESS_ATTR : CANOPUS_BAND9_RO_ACCESS_ATTR);
        }
        if (((configure_fn)(uintptr_t)FW_MPU_CONFIGURE)(
                id, base, length, access, CANOPUS_BAND9_EXEC_MEM_ATTR) != 0) return -1;
    }
    return 0;
}

static int image_invoke(void *cookie, uint32_t callable)
{
    typedef void (*fn)(void);
    (void)cookie;
    ((fn)(uintptr_t)(callable | 1u))();
    return 0;
}

__attribute__((used, visibility("default")))
int canopus_band9_stage2_entry(int ignored)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    typedef void *(*memalign_fn)(uint32_t, uint32_t);
    typedef void (*free_fn)(void *);
    struct stage2_state state;
    struct canopus_elf_module module;
    struct canopus_elf_loader_ops ops;
    uint8_t *elf;
    uint32_t used = 0;
    int fd;
    int32_t got;
    int rc;
    (void)ignored;

    canopus_memset(&state, 0, sizeof(state));
    elf = ((memalign_fn)(uintptr_t)FW_MEMALIGN)(4u,
                                               CANOPUS_BAND9_SUPERVISOR_SIZE);
    if (elf == 0) return -3;
    fd = ((open_fn)(uintptr_t)FW_OPEN)(SUPERVISOR_PATH, 1);
    if (fd < 0) {
        ((free_fn)(uintptr_t)FW_FREE)(elf);
        return -4;
    }
    while (used < CANOPUS_BAND9_SUPERVISOR_SIZE) {
        got = ((read_fn)(uintptr_t)FW_READ)(
            fd, elf + used, CANOPUS_BAND9_SUPERVISOR_SIZE - used);
        if (got <= 0) {
            ((close_fn)(uintptr_t)FW_CLOSE)(fd);
            ((free_fn)(uintptr_t)FW_FREE)(elf);
            return -5;
        }
        used += (uint32_t)got;
    }
    ((close_fn)(uintptr_t)FW_CLOSE)(fd);
    ops.cookie = &state;
    ops.allocate = image_allocate;
    ops.release = image_release;
    ops.finalize = image_finalize;
    ops.invoke = image_invoke;
    rc = canopus_elf32_load(elf, CANOPUS_BAND9_SUPERVISOR_SIZE, &ops, &module);
    ((free_fn)(uintptr_t)FW_FREE)(elf);
    return rc;
}
