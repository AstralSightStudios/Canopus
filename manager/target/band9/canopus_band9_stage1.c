/* Band-9 position-independent stage-1 file loader entered through NSH exec. */
#include "canopus_band9_loader_config.h"

#include <stdint.h>

#ifndef CANOPUS_BAND9_STAGE2_SIZE
#error "CANOPUS_BAND9_STAGE2_SIZE is required"
#endif

#define STAGE2_PATH "/data/canopus/stage2.bin"
#define FW_OPEN CANOPUS_FW_OPEN
#define FW_CLOSE CANOPUS_FW_CLOSE
#define FW_READ CANOPUS_FW_READ
#define FW_MEMALIGN CANOPUS_FW_MEMALIGN
#define FW_FREE CANOPUS_FW_FREE
#define FW_MPU_ALLOC CANOPUS_FW_MPU_ALLOC
#define FW_MPU_CONFIGURE CANOPUS_FW_MPU_CONFIGURE
#define FW_MPU_RELEASE CANOPUS_FW_MPU_RELEASE
#define MPU_RNR CANOPUS_MPU_RNR
#define MPU_RLAR CANOPUS_MPU_RLAR

__attribute__((used, visibility("default")))
int canopus_band9_stage1_entry(int ignored)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    typedef void *(*memalign_fn)(uint32_t, uint32_t);
    typedef void (*free_fn)(void *);
    typedef uint32_t (*mpu_alloc_fn)(void);
    typedef int (*mpu_configure_fn)(uint32_t, uint32_t, uint32_t,
                                    uint32_t, uint32_t);
    typedef void (*mpu_release_fn)(uint32_t);
    typedef int (*stage2_fn)(int);
    uint32_t allocation_size =
        (CANOPUS_BAND9_STAGE2_SIZE + 31u) & ~UINT32_C(31);
    uint8_t *image;
    uint32_t used = 0;
    uint32_t region;
    int fd;
    int32_t got;
    int rc;
    (void)ignored;

    image = ((memalign_fn)(uintptr_t)FW_MEMALIGN)(32u, allocation_size);
    if (image == 0) return -10;
    fd = ((open_fn)(uintptr_t)FW_OPEN)(STAGE2_PATH, 1);
    if (fd < 0) {
        ((free_fn)(uintptr_t)FW_FREE)(image);
        return -11;
    }
    while (used < CANOPUS_BAND9_STAGE2_SIZE) {
        got = ((read_fn)(uintptr_t)FW_READ)(
            fd, image + used, CANOPUS_BAND9_STAGE2_SIZE - used);
        if (got <= 0) {
            ((close_fn)(uintptr_t)FW_CLOSE)(fd);
            ((free_fn)(uintptr_t)FW_FREE)(image);
            return -12;
        }
        used += (uint32_t)got;
    }
    ((close_fn)(uintptr_t)FW_CLOSE)(fd);
    region = ((mpu_alloc_fn)(uintptr_t)FW_MPU_ALLOC)();
    if (region >= CANOPUS_BAND9_MPU_REGION_COUNT) {
        ((free_fn)(uintptr_t)FW_FREE)(image);
        return -13;
    }
    if (((mpu_configure_fn)(uintptr_t)FW_MPU_CONFIGURE)(
            region, (uint32_t)(uintptr_t)image, allocation_size,
            CANOPUS_BAND9_EXEC_ACCESS_ATTR, CANOPUS_BAND9_EXEC_MEM_ATTR) != 0) {
        *(volatile uint32_t *)(uintptr_t)MPU_RNR = region;
        *(volatile uint32_t *)(uintptr_t)MPU_RLAR = 0u;
        __asm__ volatile("dsb sy\n"
                         "isb sy\n"
                         ::: "memory");
        ((mpu_release_fn)(uintptr_t)FW_MPU_RELEASE)(region);
        ((free_fn)(uintptr_t)FW_FREE)(image);
        return -14;
    }
    rc = ((stage2_fn)(uintptr_t)((uint32_t)(uintptr_t)image | 1u))(0);
    *(volatile uint32_t *)(uintptr_t)MPU_RNR = region;
    *(volatile uint32_t *)(uintptr_t)MPU_RLAR = 0u;
    __asm__ volatile("dsb sy\n"
                     "isb sy\n"
                     ::: "memory");
    ((mpu_release_fn)(uintptr_t)FW_MPU_RELEASE)(region);
    ((free_fn)(uintptr_t)FW_FREE)(image);
    return rc;
}
