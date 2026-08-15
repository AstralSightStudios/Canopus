/* canopus_supervisor_module.c — module glue for the supervisor.
 *
 * Boot-resident native module (like btpatch_phase5): constructor initializes
 * the supervisor and asks the platform to register /dev/canopus; the char
 * device's read side maps to render_status and its write side maps to
 * handle_command. Never rmmod; reboot for recovery.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_veneer.h"

extern const struct canopus_sup_platform_v1 canopus_sup_platform;

static struct canopus_supervisor_v1 g_sup;
static int g_device_registered;
static int g_module_activation_started;

struct canopus_supervisor_v1 *canopus_supervisor_get(void)
{
    return &g_sup;
}

int canopus_supervisor_restore_after_boot(void)
{
    if (g_module_activation_started) {
        return 0;
    }
    g_module_activation_started = 1;
    return canopus_supervisor_activate_restored_modules(&g_sup);
}

__attribute__((constructor)) static void canopus_sup_ctor(void)
{
    if (canopus_identity_guard() != 0) {
        return;
    }
    if (canopus_supervisor_init(&g_sup, 1u, &canopus_sup_platform, 0) != 0) {
        return;
    }
    if (canopus_sup_platform.register_device != 0
        && canopus_sup_platform.register_device(0) == 0) {
        g_device_registered = 1;
    }
    /* Preserve the registry-visible slot table during boot, but do not load
     * enabled third-party modules here. Stock `insmod` executes constructors on
     * a 7.9 KiB stack; nested Rust modules belong on the first Manager page's
     * regular UI task. */
    if (canopus_supervisor_restore_registry_metadata(&g_sup) != 0) {
        g_sup.error_code = CANOPUS_SUP_ERR_REGISTRY;
    }
}

__attribute__((destructor)) static void canopus_sup_dtor(void)
{
    if (g_device_registered && canopus_sup_platform.unregister_device != 0) {
        (void)canopus_sup_platform.unregister_device(0);
        g_device_registered = 0;
    }
}
