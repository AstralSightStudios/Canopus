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
    /* Restore the persisted slot table so modules installed in a previous
     * session survive reboot / canopus reinstall. Enabled intents are loaded
     * here; remove intents delete their inbox artifacts. A registry that
     * exists but cannot be read back (truncated write, bad magic) is a real
     * storage failure, not a fresh install: surface it as ERR_REGISTRY so the
     * installer status shows error= instead of silently dropping every module.
     * Per-module load failures already leave the slot FAILED with ERR_LOAD. */
    if (canopus_supervisor_restore_registry(&g_sup) != 0) {
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
