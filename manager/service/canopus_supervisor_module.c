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

struct canopus_supervisor_v1 *canopus_supervisor_get(void)
{
    return &g_sup;
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
}

__attribute__((destructor)) static void canopus_sup_dtor(void)
{
    if (g_device_registered && canopus_sup_platform.unregister_device != 0) {
        (void)canopus_sup_platform.unregister_device(0);
        g_device_registered = 0;
    }
}
