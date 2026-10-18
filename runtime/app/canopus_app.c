/* canopus_app.c — public native-app descriptor + versioned status (CAN-APP-008/011). */
#include "canopus_app.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

int canopus_app_descriptor_check(const struct canopus_app_descriptor_v1 *d)
{
    if (d == 0) {
        return -1;
    }
    if (d->struct_size != sizeof(struct canopus_app_descriptor_v1)) {
        return -1;
    }
    if (d->abi_major != CANOPUS_APP_ABI_MAJOR) {
        return -1;
    }
    if (d->abi_minor > CANOPUS_APP_ABI_MINOR) {
        return -1;
    }
    return 0;
}

/* Versioned app status record written through the portable status writer
 * (CAN-APP-011): magic "APP2" + app_state + app_flags, then published. */
#define CANOPUS_APP_STATUS_MAGIC 0x41505032u /* "APP2" */

int canopus_app_status_write(struct canopus_status_writer_v1 *w,
                             uint32_t app_state,
                             uint32_t app_flags)
{
    struct canopus_status_writer_v1 tmp;
    if (w == 0) {
        return -1;
    }
    tmp = *w;
    if (canopus_status_put_u32(&tmp, CANOPUS_APP_STATUS_MAGIC) != 0) {
        return -1;
    }
    if (canopus_status_put_u32(&tmp, CANOPUS_APP_ABI_MAJOR) != 0) {
        return -1;
    }
    if (canopus_status_put_u32(&tmp, app_state) != 0) {
        return -1;
    }
    if (canopus_status_put_u32(&tmp, app_flags) != 0) {
        return -1;
    }
    canopus_status_writer_publish(&tmp);
    *w = tmp;
    return 0;
}
