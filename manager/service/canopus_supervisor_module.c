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
#ifdef CANOPUS_SUP_P65_MODLIB
#include "canopus_p65_safety.h"
#endif
#ifdef CANOPUS_SUP_BAND9_BOOTSTRAP
#include "canopus_band9_loader_config.h"
#include "band9/canopus_band9_loader_status.h"
#elif defined(CANOPUS_SUP_BAND11_BOOTSTRAP)
#define CANOPUS_BAND9_CTOR_IDENTITY_FAILED -201
#define CANOPUS_BAND9_CTOR_INIT_FAILED -202
#define CANOPUS_BAND9_CTOR_REGISTER_HOOK_MISSING -203
#define CANOPUS_BAND9_CTOR_REGISTER_FAILED -204
static volatile int32_t *s_ctor_mailbox;
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
#elif defined(CANOPUS_SUP_BAND11_BOOTSTRAP)
    if (s_ctor_mailbox) *s_ctor_mailbox = status;
    __asm__ volatile("dsb sy" ::: "memory");
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

#if !defined(CANOPUS_SUP_BAND11_BOOTSTRAP) && \
    !defined(CANOPUS_SUP_P65_MODLIB)
__attribute__((constructor))
#endif
static void canopus_sup_ctor(void)
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

#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
/* The private staged loader supplies r0 to init-array entries. Standard
 * no-argument constructors ignore it; this wrapper publishes a checked result. */
static void canopus_sup_ctor_mailbox(volatile int32_t *mailbox)
{
    s_ctor_mailbox = mailbox;
    canopus_sup_ctor();
    s_ctor_mailbox = 0;
}
__attribute__((used, section(".init_array")))
static void (*const canopus_sup_init_entry)(volatile int32_t *) = canopus_sup_ctor_mailbox;
#endif

#if !defined(CANOPUS_SUP_P65_MODLIB)
__attribute__((destructor))
#endif
static void canopus_sup_dtor(void)
{
    if (g_device_registered && canopus_sup_platform.unregister_device != 0) {
        (void)canopus_sup_platform.unregister_device(0);
        g_device_registered = 0;
    }
}

#ifdef CANOPUS_SUP_P65_MODLIB
struct canopus_p65_modlib_unload_pair {
    int32_t (*callback)(void *context);
    void *context;
};

_Static_assert(sizeof(struct canopus_p65_modlib_unload_pair) == 8u,
               "P65 modlib callback/context pair size");

static int32_t canopus_sup_p65_unload(void *context)
{
    (void)context;
#ifdef CANOPUS_SUP_P65_RESIDENT
    /* Exact P65 modlib checks the callback result before unlink/free.
     * Keep firmware-owned fops and UI callbacks resident until reboot. */
    return -16; /* -EBUSY */
#endif
    if (g_device_registered && canopus_sup_platform.unregister_device != 0) {
        int rc = canopus_sup_platform.unregister_device(0);
        if (rc < 0) return rc;
        g_device_registered = 0;
    }
    canopus_sup_dtor();
    return 0;
}

/* Stock P65 ET_REL modlib calls e_entry(module_record + 104). The two words
 * are the rmmod callback and its context. Init-array/fini-array are not the
 * ET_REL lifecycle path, so publish the unload pair explicitly here. */
__attribute__((used, section(".text.canopus_module_entry")))
int32_t canopus_supervisor_module_initialize(
    struct canopus_p65_modlib_unload_pair *unload_pair)
{
    if (unload_pair == 0) {
        return -1;
    }
    unload_pair->callback = 0;
    unload_pair->context = 0;

    /* Deny before constructor, file I/O or any callback can escape.
     * Rejecting after partial registration would permit modlib to free it. */
    if (!CANOPUS_P65_DEPLOYMENT_ENABLED) {
        return CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED;
    }
    if (canopus_sup_platform.register_device == 0) return -1;
#ifndef CANOPUS_SUP_P65_RESIDENT
    /* Removable services must provide their complete teardown path. */
    if (canopus_sup_platform.unregister_device == 0) return -1;
#endif

    /* Reject a wrong firmware before any registered callback can escape. */
    if (canopus_identity_guard() != 0) return -1;
    canopus_sup_ctor();
#ifndef CANOPUS_SUP_P65_RESIDENT
    if (!g_device_registered) return -1;
#endif
    /* Resident registration can fail after publishing the first fops table.
     * Keep this image alive even then: modlib must not free escaped callbacks.
     * This historical retention path is not a recovery guarantee; current
     * P65 deployment is denied before the constructor above. */
    unload_pair->callback = canopus_sup_p65_unload;
    unload_pair->context = 0;
    return 0;
}
#endif
