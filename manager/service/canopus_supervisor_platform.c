/* canopus_supervisor_platform.c — real device platform.
 *
 * Registers /dev/canopus exactly the way btpatch registers /dev/btpatch:
 * stock `register_driver` (0x0C1A0D51) with a 12-word file_operations array
 * whose read side renders the 384-byte status ABI and whose write side
 * dispatches the 16-byte command ABI. This is the same managed symbol the
 * target pack exposes via the generated veneer (`canopus_fw_register_driver`).
 *
 * Loading/unloading Canopus modules and package staging remain fail-closed
 * until the stock modlib path for arbitrary ET_REL modules is proven (G0 for
 * target modules); the installer control surface works now.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_veneer.h" /* canopus_fw_register_driver / canopus_fw_unregister_driver */

#define CANOPUS_SUP_DEVICE_PATH "/dev/canopus"
#define CANOPUS_SUP_DEVICE_MODE 438u /* 0666 */
#define CANOPUS_SUP_FOPS_WORDS 12u   /* matches the stock file_operations table */

static uint32_t s_fops[CANOPUS_SUP_FOPS_WORDS];

static int sup_control_open(void *filep)
{
    (void)filep;
    return 0;
}

static int sup_control_close(void *filep)
{
    (void)filep;
    return 0;
}

static int32_t sup_control_read(void *filep, void *buffer, uint32_t count)
{
    struct canopus_supervisor_v1 *sup;
    (void)filep;
    if (buffer == 0 || count < CANOPUS_SUP_STATUS_SIZE) {
        return -1;
    }
    sup = canopus_supervisor_get();
    if (canopus_supervisor_render_status(sup, (uint8_t *)buffer) != 0) {
        return -1;
    }
    return (int32_t)CANOPUS_SUP_STATUS_SIZE;
}

static int32_t sup_control_write(void *filep, const void *buffer, uint32_t count)
{
    struct canopus_supervisor_v1 *sup;
    (void)filep;
    if (buffer == 0 || count < CANOPUS_SUP_COMMAND_SIZE) {
        return -1;
    }
    sup = canopus_supervisor_get();
    (void)canopus_supervisor_handle_command(sup, (const uint8_t *)buffer);
    return 0;
}

static int sup_register_device(void *cookie)
{
    (void)cookie;
    s_fops[0] = (uint32_t)(uintptr_t)&sup_control_open;
    s_fops[1] = (uint32_t)(uintptr_t)&sup_control_close;
    s_fops[2] = (uint32_t)(uintptr_t)&sup_control_read;
    s_fops[3] = (uint32_t)(uintptr_t)&sup_control_write;
    return canopus_fw_register_driver(CANOPUS_SUP_DEVICE_PATH,
                                     (const void *)s_fops,
                                     CANOPUS_SUP_DEVICE_MODE,
                                     (void *)0);
}

static int sup_unregister_device(void *cookie)
{
    (void)cookie;
    return canopus_fw_unregister_driver(CANOPUS_SUP_DEVICE_PATH);
}

static int sup_load_module(void *cookie, uint32_t index,
                           const char *name, uint32_t lifecycle_class)
{
    (void)cookie; (void)index; (void)name; (void)lifecycle_class;
    return -1; /* stock modlib load of Canopus ET_REL modules: G0 pending */
}

static int sup_unload_module(void *cookie, uint32_t index)
{
    (void)cookie; (void)index;
    return -1;
}

static int sup_stage_package(void *cookie, const char *path)
{
    (void)cookie; (void)path;
    return -1; /* package staging + signature verify: pending */
}

const struct canopus_sup_platform_v1 canopus_sup_platform = {
    sup_register_device,
    sup_unregister_device,
    sup_load_module,
    sup_unload_module,
    sup_stage_package,
};
