/* canopus_supervisor_platform_stub.c — device-gated platform stubs.
 *
 * The supervisor's device operations (registering /dev/canopus, loading
 * Canopus ET_REL modules through the stock modlib, package staging) depend on
 * THIS firmware's exact APIs and are not yet proven (device gates G0/G4).
 * These stubs let the supervisor module cross-compile to a zero-import ELF32
 * ET_REL and pass the Canopus verifier so the stock loader can at least be
 * tested (G0: does the loader accept it at all). Every stub fails closed.
 *
 * Replace this file with the real implementation once the device APIs are
 * recovered (mirror btpatch's char-device registration pattern).
 */
#include "canopus_supervisor_platform.h"

static int stub_register_device(void *cookie)
{
    (void)cookie;
    return -1; /* device RE pending */
}

static int stub_unregister_device(void *cookie)
{
    (void)cookie;
    return -1;
}

static int stub_load_module(void *cookie, uint32_t index,
                            const char *name, uint32_t lifecycle_class)
{
    (void)cookie; (void)index; (void)name; (void)lifecycle_class;
    return -1;
}

static int stub_unload_module(void *cookie, uint32_t index)
{
    (void)cookie; (void)index;
    return -1;
}

static int stub_stage_package(void *cookie, const char *path)
{
    (void)cookie; (void)path;
    return -1;
}

const struct canopus_sup_platform_v1 canopus_sup_platform = {
    stub_register_device,
    stub_unregister_device,
    stub_load_module,
    stub_unload_module,
    stub_stage_package,
};
