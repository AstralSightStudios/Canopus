/* Run with -ffreestanding so sleep_ms uses the device's monotonic wait path. */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include <stdio.h>
#include <time.h>

static uint32_t monotonic_ms(void *cookie)
{
    struct timespec now;
    (void)cookie;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)((uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u);
}

int main(void)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_sup_platform_v1 platform = { .monotonic_ms = monotonic_ms };
    const struct canopus_context_v1 *ctx;
    uint32_t start, elapsed;
    if (canopus_supervisor_init(&sup, 1u, &platform, 0) != 0 ||
        canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1u, "sleep_test") != 0) return 1;
    ctx = canopus_supervisor_context(&sup, 0u);
    start = monotonic_ms(0);
    ctx->time->sleep_ms(ctx, 1000u);
    elapsed = monotonic_ms(0) - start;
    if (elapsed < 1000u) {
        fprintf(stderr, "FAIL device-branch sleep_ms(1000): elapsed=%u ms\n", elapsed);
        return 1;
    }
    printf("PASS device-branch sleep_ms(1000): elapsed=%u ms\n", elapsed);
    return 0;
}
