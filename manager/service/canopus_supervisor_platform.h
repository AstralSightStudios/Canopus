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

#define CANOPUS_SUP_REGISTER_VERIFY_ERRNO_BASE 65536
#define CANOPUS_SUP_REGISTER_CALL_ERRNO_BASE 73728
#define CANOPUS_SUP_REGISTER_ERRNO_MAX 4095

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
    /* Package generations (A=0, B=1) and the client-written staging area.
     * stage_verify: verify <token>'s staged receipt + artifact; on success
     *   fills the receipt's version/lifecycle and returns 0, else sets
     *   sup->error_code to the failing stage and returns -1.
     * stage_commit: move the verified staged package into generation `gen`
     *   of <token> (replacing whatever was there). Returns 0.
     * gen_exists: a receipt file is present for (token, gen).
     * gen_verify: full receipt + artifact verification of (token, gen);
     *   fills version/lifecycle; 0 on success.
     * Optional: platforms without them cannot update or roll back. */
    int (*stage_verify)(void *cookie, const char *token, uint32_t *version,
                        uint32_t *lifecycle);
    int (*stage_commit)(void *cookie, const char *token, uint32_t gen);
    int (*gen_exists)(void *cookie, const char *token, uint32_t gen);
    int (*gen_verify)(void *cookie, const char *token, uint32_t gen,
                      uint32_t *version, uint32_t *lifecycle);
    /* Module-private file access for the FS service. `path` is a complete
     * path built by the core. file_open: flags are CANOPUS_FS_READ or
     * CANOPUS_FS_WRITE (create/truncate); returns a file descriptor >= 0 or
     * a negative error. Optional: without them the FS group is absent. */
    int (*file_open)(void *cookie, const char *path, uint32_t flags);
    int32_t (*file_read)(void *cookie, int fd, void *buffer, uint32_t size);
    int32_t (*file_write)(void *cookie, int fd, const void *buffer, uint32_t size);
    int (*file_close)(void *cookie, int fd);
    int (*file_remove)(void *cookie, const char *path);
    /* Phone-installed Canopus packages (quick apps named
     * com.canopus.pkg.<id> under /data/app). list_packages writes the module
     * id of the index-th package into `id` (capacity CANOPUS_SUP_MODULE_ID_MAX)
     * and returns 0; 1 past the last; negative on error. import_package copies
     * the payload for this firmware into the staging area (<id>.cmi.next /
     * <id>.ko.next); 0 on success, else sets sup->error_code. */
    int (*list_packages)(void *cookie, uint32_t index, char *id);
    int (*import_package)(void *cookie, const char *id);
    /* Monotonic milliseconds (wraps at 2^32). Optional: without it the TIME
     * service group is absent. */
    uint32_t (*monotonic_ms)(void *cookie);
    /* Raw heap. Both or neither; without them the MEM service group is absent. */
    void *(*heap_alloc)(void *cookie, uint32_t size);
    void (*heap_release)(void *cookie, void *ptr);
    /* Post a user-visible notification. Returns 0 on success. Optional:
     * without it the NOTIFY service group is absent. */
    int (*notify)(void *cookie, const char *title, const char *body);
};

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SUPERVISOR_PLATFORM_H */
