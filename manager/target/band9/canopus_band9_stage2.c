/* Band-9 position-independent stage-2 loader entered through NSH exec. */
#include "canopus_elf32_loader.h"
#include "canopus_memory.h"
#include "canopus_band9_loader_config.h"
#include "canopus_band9_loader_status.h"

#include <stdint.h>

#ifndef CANOPUS_BAND9_SUPERVISOR_SIZE
#error "CANOPUS_BAND9_SUPERVISOR_SIZE is required"
#endif

#define SUPERVISOR_PATH "/data/canopus/supervisor.elf"
#define FW_OPEN CANOPUS_FW_OPEN
#define FW_CLOSE CANOPUS_FW_CLOSE
#define FW_READ CANOPUS_FW_READ
#define FW_KMEM_MALLOC CANOPUS_FW_KMEM_MALLOC
#define FW_KMEM_FREE CANOPUS_FW_KMEM_FREE
#define FW_MPU_ALLOC CANOPUS_FW_MPU_ALLOC
#define FW_MPU_CONFIGURE CANOPUS_FW_MPU_CONFIGURE
#define FW_MPU_RELEASE CANOPUS_FW_MPU_RELEASE

struct stage2_state {
    uint8_t regions[3];
    uint32_t region_count;
    void *image_raw;
};

/* Band 9 keeps code and data in one MPU-mapped SRAM view, so both bases are
 * the allocation itself. */
static void *image_allocate(void *cookie, uint32_t size, uint32_t alignment,
                            uint32_t *code_base, uint32_t *data_base)
{
    typedef void *(*malloc_fn)(uint32_t);
    struct stage2_state *state = cookie;
    uintptr_t aligned;
    uint32_t extra;

    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) return 0;
    extra = alignment - 1u;
    if (size > UINT32_MAX - extra) return 0;
    state->image_raw = ((malloc_fn)(uintptr_t)FW_KMEM_MALLOC)(size + extra);
    if (state->image_raw == 0) return 0;
    aligned = ((uintptr_t)state->image_raw + extra) & ~((uintptr_t)extra);
    *code_base = (uint32_t)aligned;
    *data_base = (uint32_t)aligned;
    return (void *)aligned;
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
    (void)allocation;
    if (state->image_raw != 0) {
        ((free_fn)(uintptr_t)FW_KMEM_FREE)(state->image_raw);
        state->image_raw = 0;
    }
}

static int image_finalize(void *cookie, void *allocation, uint32_t target_base,
                          uint32_t data_base, uint32_t size,
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

    if (target_base != data_base) return -1; /* no second view on this target */
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
    typedef void *(*malloc_fn)(uint32_t);
    typedef void (*free_fn)(void *);
    struct stage2_state state;
    struct canopus_elf_module module;
    struct canopus_elf_loader_ops ops;
    uint8_t *elf;
    void *scratch_raw;
    void *scratch;
    uint32_t used = 0;
    int fd;
    int32_t got;
    int rc;
    (void)ignored;

    canopus_memset(&state, 0, sizeof(state));
    *(volatile uint32_t *)(uintptr_t)CANOPUS_BAND9_CAVE_RESULT =
        CANOPUS_BAND9_CTOR_SENTINEL;
    __asm__ volatile("dsb sy\n"
                     ::: "memory");
    elf = ((malloc_fn)(uintptr_t)FW_KMEM_MALLOC)(
        CANOPUS_BAND9_SUPERVISOR_SIZE);
    if (elf == 0) return -3;
    scratch_raw = ((malloc_fn)(uintptr_t)FW_KMEM_MALLOC)(
        CANOPUS_ELF32_SCRATCH_SIZE + 31u);
    if (scratch_raw == 0) {
        ((free_fn)(uintptr_t)FW_KMEM_FREE)(elf);
        return -6;
    }
    scratch = (void *)(((uintptr_t)scratch_raw + 31u) & ~((uintptr_t)31u));
    fd = ((open_fn)(uintptr_t)FW_OPEN)(SUPERVISOR_PATH, 1);
    if (fd < 0) {
        ((free_fn)(uintptr_t)FW_KMEM_FREE)(scratch_raw);
        ((free_fn)(uintptr_t)FW_KMEM_FREE)(elf);
        return -4;
    }
    while (used < CANOPUS_BAND9_SUPERVISOR_SIZE) {
        got = ((read_fn)(uintptr_t)FW_READ)(
            fd, elf + used, CANOPUS_BAND9_SUPERVISOR_SIZE - used);
        if (got <= 0) {
            ((close_fn)(uintptr_t)FW_CLOSE)(fd);
            ((free_fn)(uintptr_t)FW_KMEM_FREE)(scratch_raw);
            ((free_fn)(uintptr_t)FW_KMEM_FREE)(elf);
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
    rc = canopus_elf32_load(elf, CANOPUS_BAND9_SUPERVISOR_SIZE, &ops,
                            &module, scratch);
    ((free_fn)(uintptr_t)FW_KMEM_FREE)(scratch_raw);
    ((free_fn)(uintptr_t)FW_KMEM_FREE)(elf);
    if (rc != CANOPUS_ELF_LOAD_OK) return -100 + rc;
    rc = *(volatile int32_t *)(uintptr_t)CANOPUS_BAND9_CAVE_RESULT;
    if ((uint32_t)rc == CANOPUS_BAND9_CTOR_SENTINEL) {
        return CANOPUS_BAND9_CTOR_NOT_OBSERVED;
    }
    return rc;
}
