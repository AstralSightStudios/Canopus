/*
 * canopus_services.h — Supervisor service table and Service Objects suite (ADR-CAN-006).
 *
 * Generic capabilities are implemented once per firmware inside the
 * Supervisor and handed to modules through `canopus_context_v1`. A module
 * that uses this header contains no firmware addresses and is portable
 * across every firmware whose Supervisor implements the required groups.
 *
 * First-Class Service Objects:
 *   ctx->fs      : Complete POSIX-style file system, random seeking & directories
 *   ctx->kv      : Persistent key-value configuration & preferences store
 *   ctx->mem     : Uncapped heap allocator with realloc & quota tracking
 *   ctx->time    : Monotonic millisecond clock, realtime, and module timers
 *   ctx->hook    : Universal dynamic function hook engine (FPB / Inline / Slot)
 *   ctx->haptics : Linear vibration motor, pulses, and preset tactile feedback
 *   ctx->display : Brightness control, wake locks, screen on/off & metrics
 *   ctx->bus     : Inter-module & system pub/sub event bus
 *   ctx->system  : Device model, firmware identity, battery, and reboot
 *   ctx->bt      : WearPacket Layer 2 transport, companion app connectivity
 *   ctx->connect : CanopusConnect text/binary messages with AstroBox plugins
 *   ctx->log     : Categorized structured logging
 *   ctx->notify  : User-visible notification popups & alerts
 */
#ifndef CANOPUS_SERVICES_H
#define CANOPUS_SERVICES_H

#include <stdint.h>
#include "canopus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Service groups. Each has an independent version; a zero version means the
 * group is absent on this Supervisor. */
enum canopus_service_group {
    CANOPUS_SVC_MODULE    = 0,  /* descriptor registration */
    CANOPUS_SVC_LOG       = 1,  /* logging */
    CANOPUS_SVC_FS        = 2,  /* files and directories */
    CANOPUS_SVC_TIME      = 3,  /* monotonic clock & timers */
    CANOPUS_SVC_MEM       = 4,  /* heap allocator */
    CANOPUS_SVC_NOTIFY    = 5,  /* user notifications */
    CANOPUS_SVC_HOOK      = 6,  /* universal function hook engine */
    CANOPUS_SVC_SYSTEM    = 7,  /* system and hardware status */
    CANOPUS_SVC_BLUETOOTH = 8,  /* bluetooth transport and L2 */
    CANOPUS_SVC_KV        = 9,  /* persistent key-value store */
    CANOPUS_SVC_HAPTICS   = 10, /* vibration & haptic feedback */
    CANOPUS_SVC_DISPLAY   = 11, /* brightness, screen & wake locks */
    CANOPUS_SVC_BUS       = 12, /* inter-module pub/sub event bus */
    CANOPUS_SVC_CONNECT   = 13, /* CanopusConnect messages (ADR-CAN-009) */
    CANOPUS_SVC_LAUNCHER  = 14, /* the module's own launcher entry (ADR-CAN-011) */
    CANOPUS_SVC_GROUP_COUNT
};

/* Return values: 0 or a non-negative count on success, negative on failure.
 * The codes are negated POSIX errno values; a group answers UNSUPPORTED when
 * this target has no recovered firmware entry point for the operation, never a
 * made-up success. Getters that return a plain integer report "unknown" through
 * the sentinels documented next to them. */
#define CANOPUS_SVC_OK              0
#define CANOPUS_SVC_ERR_ARGUMENT    (-22)
#define CANOPUS_SVC_ERR_UNSUPPORTED (-38)
#define CANOPUS_SVC_ERR_IO          (-5)
#define CANOPUS_SVC_ERR_STATE       (-16)
#define CANOPUS_SVC_ERR_NOMEM       (-12)
#define CANOPUS_SVC_ERR_NOTFOUND    (-2)
#define CANOPUS_SVC_ERR_BUSY        (-16)
#define CANOPUS_SVC_ERR_ACCESS      (-13) /* path or operation not permitted */
#define CANOPUS_SVC_ERR_EXIST       (-17) /* already exists */
#define CANOPUS_SVC_ERR_NOTDIR      (-20)
#define CANOPUS_SVC_ERR_ISDIR       (-21)
#define CANOPUS_SVC_ERR_NOSPACE     (-28)
#define CANOPUS_SVC_ERR_NOTEMPTY    (-39)

/* -------------------------------------------------------------------------- */
/* LOG                                                                        */
/* -------------------------------------------------------------------------- */

#define CANOPUS_LOG_ERROR 0u
#define CANOPUS_LOG_WARN  1u
#define CANOPUS_LOG_INFO  2u
#define CANOPUS_LOG_DEBUG 3u
#define CANOPUS_LOG_MESSAGE_MAX 128u

/* -------------------------------------------------------------------------- */
/* FS (File System & Directories)                                             */
/* -------------------------------------------------------------------------- */

#define CANOPUS_FS_READ     0x0001u
#define CANOPUS_FS_WRITE    0x0002u /* create or truncate */
#define CANOPUS_FS_APPEND   0x0004u
#define CANOPUS_FS_CREATE   0x0008u
#define CANOPUS_FS_TRUNC    0x0010u
#define CANOPUS_FS_RDWR     (CANOPUS_FS_READ | CANOPUS_FS_WRITE)

#define CANOPUS_FS_SEEK_SET 0
#define CANOPUS_FS_SEEK_CUR 1
#define CANOPUS_FS_SEEK_END 2

#define CANOPUS_FS_MAX_OPEN 32u
#define CANOPUS_FS_NAME_MAX 63u
#define CANOPUS_FS_PATH_MAX 256u

/* Path forms accepted by ctx->fs:
 *   "name", "dir/sub/name"   module-private, below /data/canopus/m.<id>/
 *   "/any/absolute/path"     any path on the device
 * Paths are resolved lexically first ("." and ".." segments, "//" and a
 * trailing "/"), so a spelling never changes which file is meant. A relative
 * path may not climb out of the private directory. The one exception to "any
 * path" is Canopus's own state: /data/canopus (registry, inbox, KV files and
 * every other module's private directory), /dev/canopus and /canopus are
 * refused with ACCESS for every operation, read included, whatever the
 * spelling or letter case. A module keeps its own private directory, also by
 * its absolute path, and cannot reach another module's. Nothing else is
 * guarded: an absolute path can damage the firmware's own files.
 * Control bytes and a ".." above the root or the private directory are
 * ARGUMENT.
 * Failures carry the filesystem's reason: a missing file or parent is
 * NOTFOUND (the open that finds no config file is distinguishable from an I/O
 * fault), EXIST, NOTDIR, ISDIR, NOTEMPTY, NOSPACE and BUSY map to their codes, anything
 * else is IO. Parent directories are not created implicitly (use mkdir); only
 * the private directory is created on the first write.
 * stat reports type, size and mtime (seconds, 0 when the filesystem keeps no
 * time); sync is the firmware's fsync, UNSUPPORTED where none is recovered. */

struct canopus_fs_stat {
    uint32_t size;
    uint8_t is_dir;
    uint8_t _pad[3];
    uint32_t mtime;
};

struct canopus_fs_dirent {
    char name[64];
    uint8_t is_dir;
    uint8_t _pad[3];
};

/* -------------------------------------------------------------------------- */
/* MEM (Heap Allocation & Quotas)                                             */
/* -------------------------------------------------------------------------- */

#define CANOPUS_MEM_MAX_BLOCKS 128u
#define CANOPUS_MEM_BUDGET     262144u /* 256 KiB baseline */

/* -------------------------------------------------------------------------- */
/* NOTIFY                                                                     */
/* -------------------------------------------------------------------------- */

#define CANOPUS_NOTIFY_BODY_MAX 256u
#define CANOPUS_NOTIFY_TITLE_MAX 64u
#define CANOPUS_NOTIFY_MAX_POSTS 16u
/* The notification shows the caller's title (the module id when it is NULL or
 * empty) and always names the posting module as its source. The stock
 * icon of post_with_icon is the absolute path of an image file (NULL or "" is
 * the Canopus icon): the stock LVGL v9 ARGB8888 .bin format, readable under
 * the same rules as ctx->fs (ACCESS for Canopus state, NOTFOUND when missing,
 * ISDIR for a directory, ARGUMENT for a relative path). post_with_icon
 * returns CANOPUS_NOTIFY_HAPTIC_SKIPPED when the notification was posted but
 * the requested vibration could not be performed. */
#define CANOPUS_NOTIFY_HAPTIC_SKIPPED 1

/* -------------------------------------------------------------------------- */
/* HOOK (Universal Dynamic Function Hook Engine)                              */
/* -------------------------------------------------------------------------- */

#define CANOPUS_HOOK_MAX 16u

typedef enum canopus_hook_type {
    CANOPUS_HOOK_AUTO    = 0, /* Registered RAM slot for Flash, inline for Thumb RAM code */
    CANOPUS_HOOK_FPB     = 1, /* Legacy selector: requires a registered RAM dispatch slot */
    CANOPUS_HOOK_INLINE  = 2, /* Thumb-2 Trampoline jump */
    CANOPUS_HOOK_POINTER = 3, /* Global function pointer table replacement */
} canopus_hook_type_t;

/* -------------------------------------------------------------------------- */
/* HAPTICS (Vibration & Tactile Presets)                                      */
/* -------------------------------------------------------------------------- */

#define CANOPUS_HAPTIC_CLICK        1u /* one 20 ms pulse */
#define CANOPUS_HAPTIC_DOUBLE_CLICK 2u /* 20 ms, 60 ms gap, 20 ms */
#define CANOPUS_HAPTIC_HEAVY        3u /* one 100 ms pulse */
#define CANOPUS_HAPTIC_ALERT        4u /* one 250 ms pulse */
#define CANOPUS_HAPTIC_TICK         5u /* one 10 ms pulse */

/* vibrate: 1..CANOPUS_HAPTIC_MAX_MS. vibrate_pattern alternates on/off
 * durations starting with ON (pattern_ms[0] is the first pulse, [1] the gap
 * after it, ...); a zero entry is skipped and at most
 * CANOPUS_HAPTIC_PATTERN_MAX entries are accepted. Steps after the first are
 * played from the Supervisor's timer pump (10 ms resolution). One vibration
 * plays at a time: a new request from any module replaces the running one, and
 * cancel only stops the caller's own. The user's "vibration disabled" setting
 * is honored by the firmware and surfaces as STATE. */
#define CANOPUS_HAPTIC_MAX_MS       5000u
#define CANOPUS_HAPTIC_PATTERN_MAX  16u

/* -------------------------------------------------------------------------- */
/* DISPLAY & WAKE LOCKS                                                       */
/* -------------------------------------------------------------------------- */

#define CANOPUS_WAKELOCK_SCREEN 1u

/* Wake locks are owned by the acquiring module: release_wake_lock accepts only
 * the module's own ids, an unloading module loses its locks and the screen
 * lock is reference counted by the firmware. SCREEN is the only lock type and
 * keeps the screen on (and therefore the CPU); any other flags are ARGUMENT.
 * At most CANOPUS_WAKELOCK_MAX locks exist, CANOPUS_WAKELOCK_PER_MODULE per
 * module (BUSY beyond that). */
#define CANOPUS_WAKELOCK_MAX        8u
#define CANOPUS_WAKELOCK_PER_MODULE 4u

/* Screen states (is_screen_on is 1 only for ON; the always-on display is 0). */
#define CANOPUS_SCREEN_OFF 0u
#define CANOPUS_SCREEN_AOD 1u
#define CANOPUS_SCREEN_ON  2u

/* Brightness is a percentage of the firmware's manual range. set_brightness
 * overrides the system setting (the lowest value is the stock minimum, never a
 * black screen) until the same module sets it again, the module unloads, or
 * another module overrides it; the override is dropped on unload.
 * get_brightness returns the active override, else the stored manual setting,
 * and CANOPUS_DISPLAY_BRIGHTNESS_UNKNOWN while auto-brightness controls it or
 * this target cannot read it. */
#define CANOPUS_DISPLAY_BRIGHTNESS_UNKNOWN UINT32_C(0xFFFFFFFF)

/* Forward declaration */
struct canopus_context_v1;

/* System events on ctx->bus. Payloads are packed bytes; each is delivered when
 * the value changes, and once to a new subscriber after it subscribes (the
 * Supervisor samples the device every 500 ms from its timer pump, so delivery
 * needs a target with a timer pump). */
#define CANOPUS_BUS_TOPIC_SCREEN  "canopus/screen/state"  /* u8 CANOPUS_SCREEN_* */
#define CANOPUS_BUS_TOPIC_BATTERY "canopus/battery/level" /* u8 percent, u8 charging */
#define CANOPUS_BUS_TOPIC_BT      "canopus/bt/state"      /* u8 adapter state, u8 link: 1 up, 0 down, 0xFF unknown */

/* Registry lifecycle, written only by the Supervisor. Payload is a native C
 * canopus_module_state_v1 (copy it before returning), not a packed wire record.
 * Events describe completed registry/activation operations, including failures;
 * ENABLE/DISABLE/REMOVE record next-boot intent and do not imply hot unload.
 * No initial event or history is replayed; boot removal precedes subscriptions,
 * and an absent module is reported by query_module as NOTFOUND. On the owner task,
 * subscribe first, then query_module without yielding: no transition can occur
 * between these calls. Lifecycle subscription/query needs no timer backend.
 * Callbacks execute synchronously on the task performing the transition (boot,
 * Manager or transport owner); they must defer UI work to the UI dispatcher. */
#define CANOPUS_BUS_TOPIC_MODULE "canopus/module/state"
#define CANOPUS_MODULE_ID_MAX 32u
#define CANOPUS_MODULE_INTENT_DISABLED 0u
#define CANOPUS_MODULE_INTENT_ENABLED  1u
#define CANOPUS_MODULE_INTENT_REMOVE   2u

struct canopus_module_state_v1 {
    char module_id[CANOPUS_MODULE_ID_MAX];
    uint32_t state; /* CANOPUS_STATE_* from canopus_runtime.h; 0 = removed */
    uint32_t intent; /* CANOPUS_MODULE_INTENT_* */
    uint32_t version;
    uint32_t lifecycle_class; /* CANOPUS_LIFECYCLE_* */
    uint32_t flags; /* bit 0: previous package exists; bit 1: update pending */
    int32_t activation_error;
    uint32_t active_generation; /* package generation A = 0, B = 1 */
};

/* system: battery_level is 0..100, or CANOPUS_SYSTEM_BATTERY_UNKNOWN before the
 * firmware has reported it / when this target cannot read it. is_charging is
 * 0 or 1; a negative value is an error (STATE: not reported yet, UNSUPPORTED). */
#define CANOPUS_SYSTEM_BATTERY_UNKNOWN UINT32_C(0xFFFFFFFF)

/* bt: adapter_state is the stock Bluetooth stack's state (0 off, 4 on, others
 * are transitions) or CANOPUS_BT_ADAPTER_UNKNOWN. is_connected is 1 while a
 * MiWear companion link is up and 0 when it is down (the stock system state);
 * negative when the firmware has not reported it yet (STATE) or this target
 * cannot tell (UNSUPPORTED).
 * l2_register_rx accepts channels CANOPUS_BT_MODULE_CHANNEL_FIRST..LAST except
 * the Supervisor's control channel 0x43; a registered channel's encrypted
 * frames are delivered to the callback on the module/UI task instead of the
 * stock handler. A NULL callback removes the caller's registration. */
#define CANOPUS_BT_ADAPTER_OFF     0u
#define CANOPUS_BT_ADAPTER_ON      4u
#define CANOPUS_BT_ADAPTER_UNKNOWN UINT32_C(0xFFFFFFFF)
#define CANOPUS_BT_MODULE_CHANNEL_FIRST 0x40u
#define CANOPUS_BT_MODULE_CHANNEL_LAST  0x4Fu
/* l2_send refuses the Supervisor's control channel 0x43 (ARGUMENT): a raw frame
 * there would corrupt the Manager's stream. Module messages use ctx->connect. */

/* connect: CanopusConnect is a message channel between one module and the
 * remote plugins (AstroBox) that listen to its module id, carried inside the
 * Supervisor's L2 0x43 stream without touching the Manager's state. A message
 * is either text (UTF-8) or binary; one message is one frame, at most
 * CANOPUS_CONNECT_DATA_MAX bytes of data.
 *  - listen(cb) receives the messages remote plugins send to this module id.
 *    cb runs on the module/UI task and must return promptly; for text, data
 *    is NUL-terminated and len excludes the NUL. A NULL cb removes it.
 *  - send queues a message to the remote plugins listening to this module id.
 *    It returns CANOPUS_CONNECT_NOT_LISTENING (positive, nothing queued) when
 *    no plugin listens in the current phone session, BUSY when the outgoing
 *    event queue is full, UNSUPPORTED on a target without an L2 backend.
 *  - remote_listening is 1 while a plugin listens to this module's id in the
 *    current phone session, 0 otherwise, negative when unsupported.
 * Remote listening is per phone session: a reconnect starts without listeners
 * until the phone subscribes again. */
#define CANOPUS_CONNECT_TEXT          1u
#define CANOPUS_CONNECT_BINARY        2u
#define CANOPUS_CONNECT_DATA_MAX      480u
#define CANOPUS_CONNECT_NOT_LISTENING 1

/* Callbacks */
typedef void (*canopus_timer_callback_t)(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data);
typedef void (*canopus_bus_callback_t)(const struct canopus_context_v1 *ctx, const char *topic, const void *payload, uint32_t len, void *user_data);
typedef void (*canopus_l2_rx_callback_t)(const struct canopus_context_v1 *ctx, uint8_t channel, const void *payload, uint16_t len, void *user_data);
typedef void (*canopus_connect_callback_t)(const struct canopus_context_v1 *ctx, uint32_t kind, const void *data, uint32_t len, void *user_data);

/* -------------------------------------------------------------------------- */
/* Service Objects                                                            */
/* -------------------------------------------------------------------------- */

struct canopus_fs_obj {
    int32_t (*open)(const struct canopus_context_v1 *ctx, const char *path, uint32_t flags);
    int32_t (*read)(const struct canopus_context_v1 *ctx, int32_t fd, void *buffer, uint32_t size);
    int32_t (*write)(const struct canopus_context_v1 *ctx, int32_t fd, const void *buffer, uint32_t size);
    int64_t (*seek)(const struct canopus_context_v1 *ctx, int32_t fd, int64_t offset, int32_t whence);
    int64_t (*tell)(const struct canopus_context_v1 *ctx, int32_t fd);
    int64_t (*size)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*sync)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*close)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*remove)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*rename)(const struct canopus_context_v1 *ctx, const char *old_path, const char *new_path);
    int32_t (*stat)(const struct canopus_context_v1 *ctx, const char *path, struct canopus_fs_stat *out_stat);
    int32_t (*exists)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*mkdir)(const struct canopus_context_v1 *ctx, const char *path);
    void *(*opendir)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*readdir)(const struct canopus_context_v1 *ctx, void *dir, struct canopus_fs_dirent *out_entry);
    int32_t (*closedir)(const struct canopus_context_v1 *ctx, void *dir);
    int32_t (*read_all)(const struct canopus_context_v1 *ctx, const char *path, void *buffer, uint32_t max_size);
    int32_t (*write_all)(const struct canopus_context_v1 *ctx, const char *path, const void *buffer, uint32_t size);
    int32_t (*get_data_path)(const struct canopus_context_v1 *ctx, char *buffer, uint32_t max_size);
    /* FS group version 2: check canopus_services_has(ctx, CANOPUS_SVC_FS, 2)
     * before accessing this appended member. Removes an empty directory only;
     * NOTEMPTY leaves its contents intact, NOTDIR for a file. */
    int32_t (*rmdir)(const struct canopus_context_v1 *ctx, const char *path);
};


/* A module's pairs live in its own file. Keys are at most CANOPUS_KV_KEY_MAX - 1
 * bytes and string values at most CANOPUS_KV_VAL_MAX - 1; longer ones are
 * rejected with CANOPUS_SVC_ERR_ARGUMENT, and a new key beyond
 * CANOPUS_KV_MAX_PAIRS pairs with CANOPUS_SVC_ERR_NOMEM. */
#define CANOPUS_KV_MAX_PAIRS 64u
#define CANOPUS_KV_KEY_MAX 32u
#define CANOPUS_KV_VAL_MAX 512u

struct canopus_kv_obj {
    int32_t (*get_int)(const struct canopus_context_v1 *ctx, const char *key, int32_t default_val);
    int32_t (*set_int)(const struct canopus_context_v1 *ctx, const char *key, int32_t val);
    int32_t (*get_string)(const struct canopus_context_v1 *ctx, const char *key, char *buf, uint32_t buf_size);
    int32_t (*set_string)(const struct canopus_context_v1 *ctx, const char *key, const char *val);
    int     (*get_bool)(const struct canopus_context_v1 *ctx, const char *key, int default_val);
    int32_t (*set_bool)(const struct canopus_context_v1 *ctx, const char *key, int val);
    int32_t (*remove)(const struct canopus_context_v1 *ctx, const char *key);
    int32_t (*clear)(const struct canopus_context_v1 *ctx);
};

struct canopus_mem_obj {
    void *(*alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void *(*calloc)(const struct canopus_context_v1 *ctx, uint32_t num, uint32_t size);
    void *(*realloc)(const struct canopus_context_v1 *ctx, void *ptr, uint32_t new_size);
    void (*free)(const struct canopus_context_v1 *ctx, void *ptr);
    uint32_t (*available)(const struct canopus_context_v1 *ctx);
    uint32_t (*used)(const struct canopus_context_v1 *ctx);
};

#define CANOPUS_TIMER_MAX_INTERVAL_MS UINT32_C(0x7fffffff)

/* Timer creation/start and callbacks belong to the serialized module/UI task.
 * Normal UI builds use a boot-resident 10 ms LVGL heartbeat. Delivery pauses
 * while LVGL is suspended (including screen-off) unless the timer was made a
 * background timer; missed periods are coalesced on resume. Headless builds have no
 * event source and return UNSUPPORTED.
 * Hot unload of the heartbeat and recovery after UI reconstruction are unsupported. */
struct canopus_time_obj {
    uint32_t (*monotonic_ms)(const struct canopus_context_v1 *ctx);
    uint64_t (*realtime_ms)(const struct canopus_context_v1 *ctx);
    void     (*sleep_ms)(const struct canopus_context_v1 *ctx, uint32_t ms);
    int32_t  (*timer_create)(const struct canopus_context_v1 *ctx, uint32_t interval_ms, int periodic, canopus_timer_callback_t cb, void *user_data);
    int32_t  (*timer_start)(const struct canopus_context_v1 *ctx, int32_t timer_id);
    int32_t  (*timer_stop)(const struct canopus_context_v1 *ctx, int32_t timer_id);
    int32_t  (*timer_destroy)(const struct canopus_context_v1 *ctx, int32_t timer_id);
    /* Time group version 2. A background timer keeps firing while the screen is
     * off, from an event source that survives it, on the same serialized task.
     * Its callback must not touch the UI: LVGL is suspended then. Check
     * canopus_services_has(ctx, CANOPUS_SVC_TIME, 2) before calling; UNSUPPORTED
     * where the target has no such source. The CPU may still sleep with the screen
     * off; hold a display wake lock to keep it running. */
    int32_t  (*timer_set_background)(const struct canopus_context_v1 *ctx, int32_t timer_id, int background);
};

struct canopus_hook_obj {
    int32_t (*install)(const struct canopus_context_v1 *ctx,
                       uintptr_t target_fn,
                       uintptr_t hook_fn,
                       uintptr_t *orig_fn,
                       canopus_hook_type_t hook_type);
    int32_t (*uninstall)(const struct canopus_context_v1 *ctx, int32_t hook_id);
    int     (*is_hooked)(const struct canopus_context_v1 *ctx, uintptr_t target_fn);
    int32_t (*register_slot_map)(const struct canopus_context_v1 *ctx,
                                 uintptr_t flash_fn,
                                 uintptr_t ram_slot);
};

struct canopus_haptics_obj {
    int32_t (*vibrate)(const struct canopus_context_v1 *ctx, uint32_t duration_ms);
    int32_t (*vibrate_pattern)(const struct canopus_context_v1 *ctx, const uint16_t *pattern_ms, uint32_t count);
    int32_t (*vibrate_preset)(const struct canopus_context_v1 *ctx, uint32_t preset);
    int32_t (*cancel)(const struct canopus_context_v1 *ctx);
};

struct canopus_display_obj {
    uint32_t (*get_brightness)(const struct canopus_context_v1 *ctx);
    int32_t  (*set_brightness)(const struct canopus_context_v1 *ctx, uint32_t percent);
    int      (*is_screen_on)(const struct canopus_context_v1 *ctx);
    int32_t  (*wake_screen)(const struct canopus_context_v1 *ctx);
    int32_t  (*acquire_wake_lock)(const struct canopus_context_v1 *ctx, uint32_t lock_flags);
    int32_t  (*release_wake_lock)(const struct canopus_context_v1 *ctx, int32_t lock_id);
    void     (*get_resolution)(const struct canopus_context_v1 *ctx, uint32_t *width, uint32_t *height);
};

/* Bus calls and callbacks use the serialized module owner task; no locks,
 * queue or thread switch is provided. publish calls matching subscribers before
 * returning. topic/payload are borrowed only for that callback. Callbacks must
 * return promptly, may query state and unsubscribe (including themselves), but
 * must not publish recursively, subscribe, or mutate the module registry.
 * Reentrant publish/subscribe return BUSY; registry commands return DISALLOWED. An unsubscribed callback is not called later
 * in the same dispatch; unloading releases all of the module's subscriptions.
 * "*" subscribes to every topic. Other topics must be 1..31 bytes exactly. */
struct canopus_bus_obj {
    int32_t (*publish)(const struct canopus_context_v1 *ctx, const char *topic, const void *payload, uint32_t len);
    int32_t (*subscribe)(const struct canopus_context_v1 *ctx, const char *topic, canopus_bus_callback_t cb, void *user_data);
    int32_t (*unsubscribe)(const struct canopus_context_v1 *ctx, int32_t sub_id);
    /* BUS version 2: copies the current registry state by stable module id.
     * NOTFOUND for an absent id, ARGUMENT for malformed arguments. */
    int32_t (*query_module)(const struct canopus_context_v1 *ctx, const char *module_id,
                            struct canopus_module_state_v1 *out);
};

struct canopus_log_obj {
    void (*write)(const struct canopus_context_v1 *ctx, uint32_t level, const char *message);
    void (*info)(const struct canopus_context_v1 *ctx, const char *message);
    void (*warn)(const struct canopus_context_v1 *ctx, const char *message);
    void (*error)(const struct canopus_context_v1 *ctx, const char *message);
    void (*debug)(const struct canopus_context_v1 *ctx, const char *message);
};

struct canopus_notify_obj {
    int32_t (*post)(const struct canopus_context_v1 *ctx, const char *title, const char *body);
    int32_t (*post_with_icon)(const struct canopus_context_v1 *ctx, const char *title, const char *body, const char *icon_path, int vibrate);
};

struct canopus_system_obj {
    const char *(*target_id)(const struct canopus_context_v1 *ctx);
    const char *(*module_id)(const struct canopus_context_v1 *ctx);
    const char *(*device_model)(const struct canopus_context_v1 *ctx);
    const char *(*firmware_version)(const struct canopus_context_v1 *ctx);
    uint32_t    (*framework_revision)(const struct canopus_context_v1 *ctx);
    int         (*is_safe_mode)(const struct canopus_context_v1 *ctx);
    uint64_t    (*uptime_ms)(const struct canopus_context_v1 *ctx);
    uint32_t    (*battery_level)(const struct canopus_context_v1 *ctx);
    int         (*is_charging)(const struct canopus_context_v1 *ctx);
    int32_t     (*reboot)(const struct canopus_context_v1 *ctx);
    int32_t     (*get_property)(const struct canopus_context_v1 *ctx, const char *key, char *buf, uint32_t max_len);
};

struct canopus_bluetooth_obj {
    int32_t  (*l2_send)(const struct canopus_context_v1 *ctx, uint8_t channel, const void *payload, uint16_t len);
    int32_t  (*l2_register_rx)(const struct canopus_context_v1 *ctx, uint8_t channel, canopus_l2_rx_callback_t cb, void *user_data);
    int      (*is_connected)(const struct canopus_context_v1 *ctx);
    uint32_t (*adapter_state)(const struct canopus_context_v1 *ctx);
};

struct canopus_connect_obj {
    int32_t (*send)(const struct canopus_context_v1 *ctx, uint32_t kind, const void *data, uint32_t len);
    int32_t (*listen)(const struct canopus_context_v1 *ctx, canopus_connect_callback_t cb, void *user_data);
    int     (*remote_listening)(const struct canopus_context_v1 *ctx);
};

struct canopus_ui_context_v1;
struct canopus_ui_tree_v1;
struct canopus_ui_navigation_page_props_v1;
struct canopus_ui_section_props_v1;
struct canopus_ui_text_props_v1;
struct canopus_ui_status_row_props_v1;
struct canopus_ui_button_props_v1;
struct canopus_ui_action_row_props_v1;
struct canopus_ui_switch_row_props_v1;
struct canopus_ui_navigation_header_props_v1;
struct canopus_ui_component_props_v1;
struct canopus_ui_style_v1;
struct canopus_ui_layout_v1;
struct canopus_ui_visual_v1;
struct canopus_ui_event_v1;

typedef void (*canopus_ui_task_fn)(const struct canopus_context_v1 *ctx,
                                   void *user_data);

/* ui pages: a module can show its own page, hosted by the Manager and drawn by
 * the Supervisor's native backend (ADR-CAN-010), through ctx->ui. set_entry registers the entry
 * (NULL removes it); while the module runs, its Manager detail page offers
 * "open interface" with `label` (at most 23 bytes, may be NULL) as the detail.
 * Opening runs on the UI task:
 *   on_open  builds the first tree into `ui` with the builder calls above and
 *            commits it before returning; failing, or not committing, cancels
 *            the page. `ui` stays valid until on_close returns.
 *   on_event a node's event id was activated; build the next tree into `ui`.
 *   on_back  (optional) an ordinary page title's back was used; return 0 when
 *            handled, e.g. after popping a route, otherwise the page closes.
 *            Completed system-back gestures also use this callback on exact
 *            Band 10 Pro 3.101.043 and Band 11 4.100.155 / 4.100.179 hosts.
 *            Other targets retain their existing system gesture behavior.
 *            An explicit navigation_header dispatches its back_event_id to
 *            on_event instead; the handler may call close_page at its root.
 *   on_close the page was closed by the user or by close_page (not on unload).
 * One module page is open at a time; callbacks must return promptly. A tree is
 * at most CANOPUS_UI_MAX_NODES nodes. close_page finishes the caller's open
 * page (STATE when it has none). UNSUPPORTED on a build without the Manager UI. */
#define CANOPUS_UI_ENTRY_LABEL_MAX 24u

struct canopus_ui_entry_v1 {
    uint32_t struct_size;
    const char *label;
    int32_t (*on_open)(const struct canopus_context_v1 *ctx,
                       struct canopus_ui_context_v1 *ui, void *user_data);
    int32_t (*on_event)(const struct canopus_context_v1 *ctx,
                        struct canopus_ui_context_v1 *ui,
                        const struct canopus_ui_event_v1 *event, void *user_data);
    int32_t (*on_back)(const struct canopus_context_v1 *ctx,
                       struct canopus_ui_context_v1 *ui, void *user_data);
    void (*on_close)(const struct canopus_context_v1 *ctx, void *user_data);
    void *user_data;
    /* How many firmware pages this entry's routes travel through. 0 keeps the
     * single-page contract: every render replaces one page's tree, and
     * ctx->ui->route answers UNSUPPORTED. Above 0 the backend allocates that
     * many exact-target page descriptors during app publication, on_open
     * serves route 0 (the root), and ctx->ui->route pushes or pops through the
     * firmware page stack so each transition is a stock push/pop animation.
     * Bounded by CANOPUS_UI_ENTRY_PAGES_MAX; the deepest route indexes it. */
    uint32_t page_count;
};

#define CANOPUS_UI_ENTRY_PAGES_MAX 8u

struct canopus_ui_obj {
    uint32_t (*capabilities)(const struct canopus_ui_context_v1 *context);
    int32_t  (*tree_begin)(struct canopus_ui_context_v1 *context, struct canopus_ui_tree_v1 **out_tree);
    int32_t  (*tree_commit)(struct canopus_ui_tree_v1 *tree);
    void     (*tree_abort)(struct canopus_ui_tree_v1 *tree);
    int32_t  (*end)(struct canopus_ui_tree_v1 *tree);
    int32_t  (*navigation_page)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_navigation_page_props_v1 *props);
    int32_t  (*section)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_section_props_v1 *props);
    int32_t  (*text)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_text_props_v1 *props);
    int32_t  (*status_row)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_status_row_props_v1 *props);
    int32_t  (*button)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_button_props_v1 *props);
    int32_t  (*action_row)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_action_row_props_v1 *props);
    int32_t  (*switch_row)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_switch_row_props_v1 *props);
    int32_t  (*navigation_header)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_navigation_header_props_v1 *props);
    int32_t  (*component)(struct canopus_ui_tree_v1 *tree, uint32_t key, uint16_t type, const struct canopus_ui_component_props_v1 *props);

    /* Tree customization, page lifetime and immediate visual controls share the
     * same UI service. Capabilities describe the actual target backend. */
    int32_t (*node_set_style)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_style_v1 *style);
    int32_t (*node_set_layout)(struct canopus_ui_tree_v1 *tree, uint32_t key, const struct canopus_ui_layout_v1 *layout);
    int32_t (*set_entry)(const struct canopus_context_v1 *ctx, const struct canopus_ui_entry_v1 *entry);
    int32_t (*close_page)(const struct canopus_context_v1 *ctx);
    /* Moves a multi-page entry (page_count above 0) to `route` 0-based. A
     * forward route pushes the target firmware page and pauses the source; a
     * backward route finishes the source so the paused page below resumes.
     * The caller's view is re-created on the destination page, so the callback
     * must rebuild its whole tree from its own model afterwards. 0 stays the
     * root page. Answers ERR_UNSUPPORTED for single-page entries. */
    int32_t (*route)(const struct canopus_context_v1 *ctx, uint32_t route);
    int32_t (*node_set_visual)(struct canopus_ui_tree_v1 *tree, uint32_t key,
                               const struct canopus_ui_visual_v1 *visual);
    int32_t (*control)(struct canopus_ui_context_v1 *ui, uint32_t generation,
                       uint32_t key, uint32_t operation, int32_t a, int32_t b,
                       void *out);
    /* Post from the serialized module owner. Repeated identical work coalesces;
     * different pending work returns BUSY. Runs after the current callback on
     * the next foreground UI heartbeat; module release cancels pending work. */
    int32_t (*post)(const struct canopus_context_v1 *ctx,
                    canopus_ui_task_fn callback, void *user_data);
};

/* launcher: Supervisor-owned application registration (ADR-CAN-011).
 * Service version 1 uses one register_app method for managed UI and exact-target
 * native pages. App identity belongs to module_id and survives reboot.
 * Calls belong to the serialized module/UI owner, normally native-app
 * publication callbacks. Descriptors and callbacks remain valid until reboot. */
#define CANOPUS_LAUNCHER_ENTRIES 8u
#define CANOPUS_LAUNCHER_LABEL_MAX 24u
#define CANOPUS_LAUNCHER_ICON_MAX 96u
#define CANOPUS_NATIVE_APP_PAGES_MAX 16u

struct canopus_app_entry_v1 {
    uint32_t struct_size;
    const char *label; /* UTF-8, 1 to 23 bytes, NUL-terminated */
    const char *icon;  /* absolute LVGL image path, at most 95 bytes, or NULL */
    /* NULL with page_count == 0 opens the registered ui->set_entry page.
     * Otherwise supply 1..CANOPUS_NATIVE_APP_PAGES_MAX writable exact-target
     * firmware page descriptors. The array is copied before return; descriptors,
     * referenced data and callbacks must remain valid until reboot. Supervisor
     * writes every native page's app_id during serialized UI registration. */
    void *const *pages;
    uint32_t page_count;
};

struct canopus_launcher_obj {
    /* Queues app_install then launcher_add on separate UI ticks, using the
     * Supervisor's module_id-owned application descriptor and package name.
     * Returns the positive assigned 8-bit app ID, or a negative service error.
     * Managed UI entries require ui->set_entry; native pages do not.
     * An identical definition returns the same ID without publishing again;
     * changing a published definition returns BUSY. Later firmware failure is
     * recorded as CANOPUS_SUP_ERR_LAUNCHER. No synchronous unregister exists. */
    int32_t (*register_app)(const struct canopus_context_v1 *ctx,
                            const struct canopus_app_entry_v1 *entry);
};

/* -------------------------------------------------------------------------- */
/* Legacy Services v1 Table (Maintained for Backward Compatibility)           */
/* -------------------------------------------------------------------------- */

struct canopus_services_v1 {
    uint32_t struct_size;
    uint8_t group_version[CANOPUS_SVC_GROUP_COUNT]; /* 0 = absent */

    int32_t (*module_register)(const struct canopus_context_v1 *ctx,
                               const struct canopus_module_descriptor_v1 *descriptor);
    void (*log_write)(const struct canopus_context_v1 *ctx, uint32_t level,
                      const char *message);
    int32_t (*fs_open)(const struct canopus_context_v1 *ctx, const char *name,
                       uint32_t flags);
    int32_t (*fs_read)(const struct canopus_context_v1 *ctx, int32_t handle,
                       void *buffer, uint32_t size);
    int32_t (*fs_write)(const struct canopus_context_v1 *ctx, int32_t handle,
                        const void *buffer, uint32_t size);
    int32_t (*fs_close)(const struct canopus_context_v1 *ctx, int32_t handle);
    int32_t (*fs_remove)(const struct canopus_context_v1 *ctx, const char *name);
    uint32_t (*time_monotonic_ms)(const struct canopus_context_v1 *ctx);
    void *(*mem_alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void (*mem_free)(const struct canopus_context_v1 *ctx, void *ptr);
    int32_t (*notify_post)(const struct canopus_context_v1 *ctx, const char *body);
};

/* -------------------------------------------------------------------------- */
/* Context v1 (with First-Class Service Objects)                              */
/* -------------------------------------------------------------------------- */

struct canopus_context_v1 {
    uint32_t struct_size;
    uint32_t abi; /* (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR */
    const struct canopus_services_v1 *services;
    uint32_t slot; /* Supervisor-internal */
    const char *target_id;
    const char *module_id;

    /* Object-oriented Service Objects */
    const struct canopus_fs_obj        *fs;
    const struct canopus_kv_obj        *kv;
    const struct canopus_mem_obj       *mem;
    const struct canopus_time_obj      *time;
    const struct canopus_hook_obj      *hook;
    const struct canopus_log_obj       *log;
    const struct canopus_notify_obj    *notify;
    const struct canopus_system_obj    *system;
    const struct canopus_bluetooth_obj *bt;
    const struct canopus_haptics_obj   *haptics;
    const struct canopus_display_obj   *display;
    const struct canopus_bus_obj       *bus;
    const struct canopus_ui_obj        *ui;
    const struct canopus_connect_obj   *connect;
    const struct canopus_launcher_obj  *launcher;
};

static inline const char *canopus_fs_data_path(const struct canopus_context_v1 *ctx, char *buffer, uint32_t max_size)
{
    if (ctx != 0 && ctx->fs != 0 && ctx->fs->get_data_path != 0) {
        int32_t rc = ctx->fs->get_data_path(ctx, buffer, max_size);
        if (rc >= 0) return buffer;
    }
    return 0;
}

/* Non-zero when the group is available at least at `min_version`. */
static inline int canopus_services_has(const struct canopus_context_v1 *ctx,
                                       uint32_t group, uint32_t min_version)
{
    return ctx != 0 && ctx->services != 0 &&
           ctx->struct_size >= sizeof(struct canopus_context_v1) &&
           group < CANOPUS_SVC_GROUP_COUNT &&
           ctx->services->group_version[group] >= min_version &&
           ctx->services->group_version[group] != 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SERVICES_H */
