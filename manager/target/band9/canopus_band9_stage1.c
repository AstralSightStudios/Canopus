/* Band-9 position-independent stage-1 file loader entered through NSH exec. */
#include <stdint.h>

#ifndef CANOPUS_BAND9_STAGE2_SIZE
#error "CANOPUS_BAND9_STAGE2_SIZE is required"
#endif

#define STAGE2_PATH "/data/canopus/stage2.bin"
#define FW_OPEN UINT32_C(0x0C37F761)
#define FW_CLOSE UINT32_C(0x0C37EFF9)
#define FW_READ UINT32_C(0x0C37F9EB)
#define FW_MEMALIGN UINT32_C(0x0C0F21ED)
#define FW_FREE UINT32_C(0x0C0F1B01)
#define FW_MPU_ALLOC UINT32_C(0x0C51D8D1)
#define FW_MPU_CONFIGURE UINT32_C(0x0C51D759)
#define FW_MPU_RELEASE UINT32_C(0x0C51D929)
#define MPU_RNR UINT32_C(0xE000ED98)
#define MPU_RLAR UINT32_C(0xE000EDA0)
#define CAVE_CLEANUP UINT32_C(0x20084E11)

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
    if (region > 7u) {
        ((free_fn)(uintptr_t)FW_FREE)(image);
        return -13;
    }
    if (((mpu_configure_fn)(uintptr_t)FW_MPU_CONFIGURE)(
            region, (uint32_t)(uintptr_t)image, allocation_size, 1u, 2u) != 0) {
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
