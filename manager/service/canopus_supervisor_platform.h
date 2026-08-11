/*
 * canopus_supervisor_platform.h — device-gated hooks for the supervisor.
 *
 * These are the operations that depend on THIS firmware's exact APIs and
 * therefore require real-device RE before they can be implemented:
 *
 *   register_device   : create /dev/canopus with read(status)/write(command).
 *   load_module       : load a verified Canopus ELF32 ET_REL module through
 *                       the exact target loader and run its constructors.
 *   stage_package     : make a staged .canopus available for INSTALL.
 *   remove_artifact   : delete a module's owned files at boot (remove intent).
 *   persist/restore   : atomic write / read of the module registry so slots
 *                       survive reboot and canopus reinstall.
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
    /* Exact target accepted by module descriptors and package receipts. */
    const char *target_id;
    /* Returns 0 on success. */
    int (*register_device)(void *cookie);
    int (*unregister_device)(void *cookie);
    /* Load a module slot. Returns the module's state on success
     * (CANOPUS_STATE_ACTIVE / BOOT_RESIDENT) or -1 on failure. */
    int (*load_module)(void *cookie, uint32_t index,
                       const char *module_name, uint32_t lifecycle_class);
    /* Stage a signed package when package_path is non-null. A payload-free
     * legacy INSTALL uses stage 0 for Manager registration, stage 1 for loaded
     * modules' app/page registration, and stage 2 for Launcher publication.
     * Returns 0 on success. */
    int (*stage_package)(void *cookie, const char *package_path,
                         uint32_t stage);
    /* Delete the module's owned inbox artifacts (remove intent, applied at
     * boot). Returns 0 on success. */
    int (*remove_artifact)(void *cookie, uint32_t index);
    /* Atomic save of the registry bytes. Returns 0 on success. */
    int (*persist)(void *cookie, const uint8_t *data, uint32_t len);
    /* Read the registry bytes. Returns 0 (present), 1 (absent) or -1. */
    int (*restore)(void *cookie, uint8_t *data, uint32_t len);
};

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SUPERVISOR_PLATFORM_H */
