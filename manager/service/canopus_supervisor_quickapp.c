/* canopus_supervisor_quickapp.c — QuickApp Feature dispatch (ADR-CAN-014).
 *
 * The firmware builds every QuickApp through its single Framework object's
 * factory slot and then initializes it through the Application's init slot,
 * on the new app's JS task, before any of its sources run. Routing points the
 * Framework object at a copy of its stock vtable whose factory moves each new
 * Application onto a copy of the stock Application vtable; that copy's init
 * runs the stock init and then registers every published Feature description
 * into the app's registry. Only the two objects' primary vptrs change: no
 * firmware code or table is written and secondary vptrs stay stock. Exact
 * layout: EVID-PORT-1043-FERRY-001. */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_target_config.h"
#include "canopus_memory.h"

#ifdef FW_QUICKAPP_FRAMEWORK

typedef void *(*quickapp_factory_fn)(void *framework, const char *uri);
typedef int (*quickapp_init_fn)(void *application, void *options, void *loop);
typedef int (*quickapp_register_fn)(void *registry, const void *description);

/* Both copies keep the two Itanium prefix words; objects point past them. */
static uintptr_t s_framework_table[FW_QUICKAPP_FRAMEWORK_WORDS];
static uintptr_t s_application_table[FW_QUICKAPP_APPLICATION_WORDS];

static uintptr_t quickapp_stock(uintptr_t vtable, uint32_t slot)
{
    return ((const uintptr_t *)vtable)[slot];
}

static uintptr_t quickapp_word(uintptr_t object, uint32_t offset)
{
    return object != 0u ? *(const uintptr_t *)(object + offset) : 0u;
}

/* Between the stock init and the app's first source: nothing can require a
 * Feature yet, so the registry takes every description before it is used. */
static int quickapp_init(void *application, void *options, void *loop)
{
    const void *const volatile *features = canopus_supervisor_quickapp_features();
    int rc = ((quickapp_init_fn)quickapp_stock(FW_QUICKAPP_APPLICATION_VTABLE,
                                               FW_QUICKAPP_INIT_SLOT))(application, options, loop);
    uintptr_t runtime = quickapp_word((uintptr_t)application, FW_QUICKAPP_APPLICATION_RUNTIME);
    uintptr_t manager = quickapp_word(runtime, FW_QUICKAPP_RUNTIME_MANAGER);
    void *registry = (void *)quickapp_word(manager, FW_QUICKAPP_MANAGER_REGISTRY);
    uint32_t i;
    if (registry == 0) return rc;
    for (i = 0u; i < CANOPUS_QUICKAPP_FEATURES; i++) {
        const void *description = features[i];
        if (description != 0) {
            (void)((quickapp_register_fn)FW_QUICKAPP_REGISTER_FEATURE)(registry, description);
        }
    }
    return rc;
}

/* The stock factory returns the constructed Application before its JS task
 * starts, so its primary vptr can still move to the copy. */
static void *quickapp_create(void *framework, const char *uri)
{
    void *application = ((quickapp_factory_fn)quickapp_stock(FW_QUICKAPP_FRAMEWORK_VTABLE,
                                                             FW_QUICKAPP_FACTORY_SLOT))(framework, uri);
    volatile uintptr_t *vptr = application;
    if (vptr != 0 && *vptr == FW_QUICKAPP_APPLICATION_VTABLE) {
        *vptr = (uintptr_t)&s_application_table[FW_QUICKAPP_VTABLE_PREFIX_WORDS];
    }
    return application;
}

/* One word switches the Framework over, so a QuickApp being built meanwhile
 * comes from either the stock factory or the copy, never from half of it. */
int canopus_sup_quickapp_route(void *cookie)
{
    volatile uintptr_t *framework =
        *(volatile uintptr_t *volatile *)(uintptr_t)FW_QUICKAPP_FRAMEWORK;
    uintptr_t routed = (uintptr_t)&s_framework_table[FW_QUICKAPP_VTABLE_PREFIX_WORDS];
    uint32_t prefix = FW_QUICKAPP_VTABLE_PREFIX_WORDS * sizeof(uintptr_t);
    (void)cookie;
    if (framework == 0) return CANOPUS_SVC_ERR_BUSY;
    if (*framework == routed) return CANOPUS_SVC_OK;
    if (*framework != FW_QUICKAPP_FRAMEWORK_VTABLE) return CANOPUS_SVC_ERR_BUSY;
    canopus_memcpy(s_framework_table, (const void *)(FW_QUICKAPP_FRAMEWORK_VTABLE - prefix),
                   sizeof(s_framework_table));
    canopus_memcpy(s_application_table, (const void *)(FW_QUICKAPP_APPLICATION_VTABLE - prefix),
                   sizeof(s_application_table));
    s_framework_table[FW_QUICKAPP_VTABLE_PREFIX_WORDS + FW_QUICKAPP_FACTORY_SLOT] =
        (uintptr_t)quickapp_create;
    s_application_table[FW_QUICKAPP_VTABLE_PREFIX_WORDS + FW_QUICKAPP_INIT_SLOT] =
        (uintptr_t)quickapp_init;
    __asm__ volatile("dmb sy" ::: "memory");
    *framework = routed;
    return CANOPUS_SVC_OK;
}

#endif
