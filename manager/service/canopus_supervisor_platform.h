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
 * implementation once the APIs are proven. Hardware hooks (haptics, display,
 * wake locks, battery, properties, Bluetooth state) come from recovered
 * firmware entry points and are present per target; see canopus_target_config.h
 * and canopus_band11_addresses.h.
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
    /* Stage a native package when package_path is non-null. A payload-free
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
    int64_t (*file_seek)(void *cookie, int fd, int64_t offset, int32_t whence);
    int (*file_sync)(void *cookie, int fd);
    int (*file_close)(void *cookie, int fd);
    int (*file_remove)(void *cookie, const char *path);
    int (*file_rename)(void *cookie, const char *old_path, const char *new_path);
    int (*file_stat)(void *cookie, const char *path, void *out_stat);
    /* mkdir creates one directory; an existing one is EEXIST. Optional. */
    int (*dir_mkdir)(void *cookie, const char *path);
    void *(*dir_open)(void *cookie, const char *path);
    int (*dir_read)(void *cookie, void *dir, void *out_entry);
    int (*dir_close)(void *cookie, void *dir);
    /* Enumerate completed native staging pairs in the Canopus inbox.
     * Returns 0 for an id, 1 at EOF, negative on error. */
    int (*list_packages)(void *cookie, uint32_t index, char *id);
    /* Monotonic milliseconds (wraps at 2^32). Optional: without it the TIME
     * service group is absent. */
    uint32_t (*monotonic_ms)(void *cookie);
    /* Raw heap. Both or neither; without them the MEM service group is absent. */
    void *(*heap_alloc)(void *cookie, uint32_t size);
    void *(*heap_realloc)(void *cookie, void *ptr, uint32_t size);
    void (*heap_release)(void *cookie, void *ptr);
    /* Post a user-visible notification attributed to `source` (the posting
     * module's id) with the caller's `title`. `icon` is the absolute path of
     * the icon image, or NULL for the Canopus icon. Returns 0 on success.
     * Optional: without it the NOTIFY service group is absent. */
    int (*notify)(void *cookie, const char *source, const char *title,
                  const char *body, const char *icon);
    int (*reboot)(void *cookie);
    /* Every hardware hook below is optional. A target without a recovered
     * firmware entry point leaves it NULL and the service answers UNSUPPORTED
     * (or its documented "unknown" value); none may fake a result. Failures
     * are negative errno values, 0 is success.
     *
     * Haptics: vibrate runs the motor for duration_ms (> 0), vibrate_cancel
     * stops whatever the Supervisor started. */
    int (*vibrate)(void *cookie, uint32_t duration_ms);
    int (*vibrate_cancel)(void *cookie);
    /* Display. Percent is 0..100 of the manual range; restore_brightness drops
     * the Supervisor's override. get_brightness reports the manual setting
     * and returns STATE while auto-brightness decides. */
    int (*set_brightness)(void *cookie, uint32_t percent);
    int (*get_brightness)(void *cookie, uint32_t *percent);
    int (*restore_brightness)(void *cookie);
    int (*wake_screen)(void *cookie);
    /* 0 off, 1 always-on display, 2 on. */
    int (*screen_state)(void *cookie);
    /* Wake locks: `index` names one of CANOPUS_WAKELOCK_MAX Supervisor slots
     * (the platform keeps one native lock object per index and reuses it).
     * release undoes exactly one acquire. */
    int (*wakelock_acquire)(void *cookie, uint32_t index);
    int (*wakelock_release)(void *cookie, uint32_t index);
    /* Battery: STATE until the firmware has reported both values. */
    int (*battery_state)(void *cookie, uint32_t *percent, int *charging);
    /* System property read. Copies at most max_len - 1 bytes plus NUL and
     * returns the copied length, or NOTFOUND when the key is absent. */
    int (*get_property)(void *cookie, const char *key, char *buf, uint32_t max_len);
    /* Bluetooth: stock adapter state and whether a MiWear companion link
     * exists (0/1; BUSY until the firmware has reported it). */
    int (*bt_adapter_state)(void *cookie, uint32_t *state);
    int (*bt_is_connected)(void *cookie);
    /* Start a boot-resident event source on the serialized module/UI task.
     * It must keep calling poll after module activation returns. No source
     * means timer_create returns UNSUPPORTED; no worker may invoke UI callbacks. */
    int (*start_timer_pump)(void *cookie, void (*poll)(void *), void *data);
    /* Like start_timer_pump, but the source keeps ticking while the screen is
     * off (LVGL suspended), on the same task. Only timers marked background are
     * dispatched from it. Optional: without it timer_set_background answers
     * UNSUPPORTED. */
    int (*start_background_pump)(void *cookie, void (*poll)(void *), void *data);
    /* WearPacket Layer 2 communication (ADR-CAN-008). Optional. */
    int (*l2_send)(void *cookie, uint8_t channel, const void *payload, uint16_t len);
    /* CanopusConnect (ADR-CAN-009), both or neither. l2_event queues one
     * complete CPC2 EVENT frame for the phone on the Canopus channel, behind
     * Manager responses and on a queue of its own: 0 when queued, BUSY when
     * that queue is full, STATE without a phone session. l2_peer identifies the
     * current phone session (0 without one) and changes when the phone
     * reconnects, which is when remote listeners lapse. */
    int (*l2_event)(void *cookie, const void *frame, uint16_t len);
    uint32_t (*l2_peer)(void *cookie);
    /* Module UI pages (ADR-CAN-010): finish the page the module in `slot` has
     * open, if any (1 when one was closed). Present only where the Manager UI
     * is; without it ctx->ui->set_entry answers UNSUPPORTED. */
    int (*ui_page_close)(void *cookie, uint32_t slot);
    /* Module launcher entries (ADR-CAN-011), both or neither, and only where
     * ui_page_close is. launcher_publish queues an entry for `slot` (the strings
     * are copied; icon may be NULL) and returns 0, or a negative errno
     * (NOMEM when every entry is taken). The firmware calls run later, one per
     * launcher_step, each on its own UI event turn: step returns 1 after doing
     * one and 0 when nothing is left, negative when a call failed. */
    int (*launcher_publish)(void *cookie, uint32_t slot, const char *label,
                            const char *icon);
    int (*launcher_step)(void *cookie);
    int (*validate_receipt)(void *cookie, const void *receipt);
};

int canopus_sup_l2_start(void);
void canopus_sup_l2_poll(void);
/* Registers `poll` on the L2 owner timer, bootstrapping that timer if needed. */
int canopus_sup_l2_background(void (*poll)(void *), void *data);
int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len);
int sup_l2_event(void *cookie, const void *frame, uint16_t len);
uint32_t sup_l2_peer(void *cookie);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SUPERVISOR_PLATFORM_H */
