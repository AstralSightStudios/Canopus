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
#ifdef CANOPUS_SUP_BAND9_BOOTSTRAP
#include "canopus_band9_loader_config.h"
#include "band9/canopus_band9_loader_status.h"
#else
#define CANOPUS_BAND9_CTOR_IDENTITY_FAILED 0
#define CANOPUS_BAND9_CTOR_INIT_FAILED 0
#define CANOPUS_BAND9_CTOR_REGISTER_HOOK_MISSING 0
#define CANOPUS_BAND9_CTOR_REGISTER_FAILED 0
#endif

extern const struct canopus_sup_platform_v1 canopus_sup_platform;

static struct canopus_supervisor_v1 g_sup;
static int g_device_registered;
static int g_module_activation_started;

static void canopus_sup_publish_ctor_status(int32_t status)
{
#ifdef CANOPUS_SUP_BAND9_BOOTSTRAP
    *(volatile int32_t *)(uintptr_t)CANOPUS_BAND9_CAVE_RESULT = status;
    __asm__ volatile("dsb sy\n"
                     ::: "memory");
#else
    (void)status;
#endif
}

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
        canopus_sup_publish_ctor_status(CANOPUS_BAND9_CTOR_IDENTITY_FAILED);
        return;
    }
    if (canopus_supervisor_init(&g_sup, 1u, &canopus_sup_platform, 0) != 0) {
        canopus_sup_publish_ctor_status(CANOPUS_BAND9_CTOR_INIT_FAILED);
        return;
    }
    if (canopus_sup_platform.register_device == 0) {
        canopus_sup_publish_ctor_status(
            CANOPUS_BAND9_CTOR_REGISTER_HOOK_MISSING);
        return;
    }
    {
        int register_result = canopus_sup_platform.register_device(0);
        if (register_result != 0) {
            int verify_min = -(CANOPUS_SUP_REGISTER_VERIFY_ERRNO_BASE +
                               CANOPUS_SUP_REGISTER_ERRNO_MAX);
            int status;
            if (register_result <= -CANOPUS_SUP_REGISTER_VERIFY_ERRNO_BASE &&
                register_result >= verify_min) {
                status = register_result;
            } else if (register_result < 0 &&
                       register_result >= -CANOPUS_SUP_REGISTER_ERRNO_MAX) {
                status = -(CANOPUS_SUP_REGISTER_CALL_ERRNO_BASE -
                           register_result);
            } else {
                status = CANOPUS_BAND9_CTOR_REGISTER_FAILED;
            }
            canopus_sup_publish_ctor_status(status);
            return;
        }
    }
    g_device_registered = 1;
    canopus_sup_publish_ctor_status(0);
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
