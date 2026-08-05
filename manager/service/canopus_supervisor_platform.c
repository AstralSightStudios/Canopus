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
#include "canopus_manager_native_probe.h"
#include "canopus_veneer.h" /* canopus_fw_register_driver / canopus_fw_unregister_driver */

#define CANOPUS_SUP_DEVICE_PATH "/dev/canopus"
#define CANOPUS_SUP_DEVICE_MODE 438u /* 0666 */
#define CANOPUS_SUP_FOPS_WORDS 12u   /* matches the stock file_operations table */

/* CAN-P2-002: the fops table is typed from the recovered layout instead of a
 * bare uint32_t[12]. The veneer's `file_operations` carries the exact device
 * byte layout (open/close/read/write at +0x0..+0xc, ioctl at +0x14, 0x30
 * total); a compile-time size assertion locks it to the 12-word table the
 * driver expects. */
static file_operations s_fops;

CANOPUS_STATIC_ASSERT(sizeof(file_operations) == 0x30u,
                      "file_operations must match the 12-word stock table");
CANOPUS_STATIC_ASSERT(CANOPUS_SUP_FOPS_WORDS * 4u == sizeof(file_operations),
                      "fops word count must equal the typed layout");

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
    (void)filep;
    return canopus_supervisor_device_read(canopus_supervisor_get(), buffer, count);
}

static int32_t sup_control_write(void *filep, const void *buffer, uint32_t count)
{
    (void)filep;
    return canopus_supervisor_device_write(canopus_supervisor_get(), buffer, count);
}

static int sup_register_device(void *cookie)
{
    (void)cookie;
    s_fops.open = (void *)(uintptr_t)&sup_control_open;
    s_fops.close = (void *)(uintptr_t)&sup_control_close;
    s_fops.read = (void *)(uintptr_t)&sup_control_read;
    s_fops.write = (void *)(uintptr_t)&sup_control_write;
    return canopus_fw_register_driver(CANOPUS_SUP_DEVICE_PATH,
                                     (const void *)&s_fops,
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
    (void)cookie;
    /* The legacy INSTALL request has no payload and is reserved for the native
     * Manager bootstrap. Crucially, this hook runs synchronously in the process
     * that writes /dev/canopus. The watchface therefore invokes app_install in
     * miwear's process, where the libuv event-loop file descriptors are valid,
     * rather than in the `system -c insmod` process. CPC2 package tokens remain
     * fail-closed until signature-backed third-party staging is implemented. */
    if (path == 0) {
        return canopus_manager_native_install();
    }
    return -1;
}

const struct canopus_sup_platform_v1 canopus_sup_platform = {
    sup_register_device,
    sup_unregister_device,
    sup_load_module,
    sup_unload_module,
    sup_stage_package,
    0, /* deactivate: device teardown happens inside unload (G0 pending) */
    0, /* stop */
};
