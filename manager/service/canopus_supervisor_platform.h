/*
 * canopus_supervisor_platform.h — device-gated hooks for the supervisor.
 *
 * These are the operations that depend on THIS firmware's exact APIs and
 * therefore require real-device RE before they can be implemented:
 *
 *   register_device   : create /dev/canopus with read(status)/write(command).
 *                       The btpatch module proves a char-device path exists on
 *                       this firmware; the exact register_driver/device API is
 *                       still device RE (G0/G4).
 *   load_module       : load a Canopus ELF32 ET_REL module through the stock
 *                       modlib and let it run its constructor.
 *   unload_module     : drain + rmmod (removable classes only).
 *   stage_package     : make a staged .canopus available for INSTALL.
 *
 * Host tests provide a fake platform; the device build provides the real
 * implementation once the APIs are proven.
 */
#ifndef CANOPUS_SUPERVISOR_PLATFORM_H
#define CANOPUS_SUPERVISOR_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct canopus_sup_platform_v1 {
    /* Returns 0 on success. */
    int (*register_device)(void *cookie);
    int (*unregister_device)(void *cookie);
    /* Load/unload a module slot. load returns the module's state on success
     * (CANOPUS_STATE_ACTIVE / BOOT_RESIDENT) or -1 on failure. */
    int (*load_module)(void *cookie, uint32_t index,
                       const char *module_name, uint32_t lifecycle_class);
    int (*unload_module)(void *cookie, uint32_t index);
    /* Make the latest staged package available for INSTALL. Returns 0/1. */
    int (*stage_package)(void *cookie, const char *package_path);
    /* Remove the supervisor-owned artifact after a successful removable
     * REMOVE. Failure retains the unloaded slot so the command can retry. */
    int (*remove_artifact)(void *cookie, uint32_t index);
    /* CAN-P0-005: per-module teardown phases called by a removable disable
     * before unload. Both are optional (the device unload path may perform
     * them itself); NULL hooks are skipped. */
    int (*deactivate)(void *cookie, uint32_t index);
    int (*stop)(void *cookie, uint32_t index);
};

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SUPERVISOR_PLATFORM_H */
