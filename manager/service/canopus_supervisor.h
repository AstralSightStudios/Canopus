/*
 * canopus_supervisor.h — device-side Canopus supervisor module (Phase 5).
 *
 * A native module (boot-resident, like btpatch_phase5) that registers the
 * /dev/canopus char device and implements the installer control ABI that the
 * Lua installer watchface drives. This is the concrete Phase 5 supervisor:
 *   - CPC2 requests/responses over /dev/canopus and WearPacket L2;
 *   - fixed-width internal status/command records for host callers;
 *   - module slots tracked through the portable lifecycle semantics.
 *
 * The device-gated actions (registering the char device, actually loading /
 * unloading Canopus ET_REL modules via the stock modlib, persisting the
 * package store) are isolated behind canopus_supervisor_platform.h. This file
 * and its .c are host-testable with a fake platform.
 */
#ifndef CANOPUS_SUPERVISOR_H
#define CANOPUS_SUPERVISOR_H

#include <stdint.h>
#include "canopus_abi.h"
#include "canopus_protocol.h"
#include "canopus_services.h"
#include "canopus_installer_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_SUP_STATUS_MAGIC 0x43505331u /* "CPS1" */
#define CANOPUS_SUP_CMD_MAGIC    0x43504331u /* "CPC1" */
#define CANOPUS_SUP_ABI          1u
#define CANOPUS_SUP_STATUS_SIZE  384u /* 128 header + 16 slots x 16 bytes */
#define CANOPUS_SUP_COMMAND_SIZE 16u
#define CANOPUS_SUP_MODULE_SLOTS 16u
#define CANOPUS_SUP_MODULE_SLOT_STRIDE 16u

/* CAN-P1-003: sequence snapshot embedded in the reserved status header
 * (offsets 36-43). begin/end are the same even value when the snapshot is
 * consistent; a reader accepts only begin == end with an even value. */
#define CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF 36u
#define CANOPUS_SUP_STATUS_SEQ_END_OFF   40u
#define CANOPUS_SUP_STATUS_MODULE_ERROR_OFF 44u
/* Result of the last SCAN: new | updated << 8 | failed << 16 | scanned << 24
 * (each saturating at 255). */
#define CANOPUS_SUP_STATUS_SCAN_OFF      88u
#define CANOPUS_SUP_STATUS_RETRIES       4u

/* `flags` persistence diagnostic layout, also returned by QUERY_DEVICE:
 * bits 0..7   last registry stage (0 = none/success)
 * bits 8..23  positive NuttX errno (0 when unavailable)
 * bits 24..31 successful registry transaction count (saturating) */
#define CANOPUS_SUP_DIAG_STAGE_MASK       0x000000FFu
#define CANOPUS_SUP_DIAG_ERRNO_SHIFT      8u
#define CANOPUS_SUP_DIAG_ERRNO_MASK       0x00FFFF00u
#define CANOPUS_SUP_DIAG_SAVE_COUNT_SHIFT 24u
#define CANOPUS_SUP_DIAG_SAVE_COUNT_MASK  0xFF000000u

enum canopus_sup_registry_stage {
    CANOPUS_SUP_REG_STAGE_NONE = 0,
    CANOPUS_SUP_REG_STAGE_OPEN_TMP = 1,
    CANOPUS_SUP_REG_STAGE_WRITE_TMP = 2,
    CANOPUS_SUP_REG_STAGE_CLOSE_TMP = 3,
    CANOPUS_SUP_REG_STAGE_VERIFY_TMP = 4,
    CANOPUS_SUP_REG_STAGE_RENAME = 5,
    CANOPUS_SUP_REG_STAGE_VERIFY_FINAL = 6,
    CANOPUS_SUP_REG_STAGE_RESTORE_OPEN = 7,
    CANOPUS_SUP_REG_STAGE_RESTORE_READ = 8,
};

/* Installer commands (arg0 = module index for enable/disable/remove/...). */
enum canopus_sup_command {
    CANOPUS_SUP_CMD_QUERY = 0x43510001u,
    CANOPUS_SUP_CMD_INSTALL,
    CANOPUS_SUP_CMD_ENABLE,
    CANOPUS_SUP_CMD_DISABLE,
    CANOPUS_SUP_CMD_REMOVE,
    CANOPUS_SUP_CMD_UPDATE, /* removed: updates are INSTALL of a newer version; opcode reserved */
    CANOPUS_SUP_CMD_ROLLBACK, /* switch back to the previous generation at next load */
    CANOPUS_SUP_CMD_ENTER_SAFE_MODE,
    CANOPUS_SUP_CMD_ACTIVATE,
    /* Apply enabled boot intents after the supervisor's own insmod has returned.
     * The installer watchface invokes this from a Lua/LVGL event turn before
     * staged native-app publication. */
    CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT,
    /* Discover native receipt/artifact pairs completed in the Canopus inbox. */
    CANOPUS_SUP_CMD_SCAN,
    /* Leave safe mode at the next boot; arg0 is a canopus_safe_exit_action. */
    CANOPUS_SUP_CMD_EXIT_SAFE_MODE,
};

/* Stable supervisor error codes (CAN-P1-008). `error_code` holds the
 * last operation's error until the next command clears it; a successful
 * command leaves it at NONE. Shown in the CPS1 status at offset 32. */
enum canopus_sup_error {
    CANOPUS_SUP_ERR_NONE = 0,
    CANOPUS_SUP_ERR_BAD_COMMAND = -1,  /* magic/format mismatch */
    CANOPUS_SUP_ERR_BAD_CLASS = -2,    /* invalid lifecycle class */
    CANOPUS_SUP_ERR_TABLE_FULL = -3,   /* module slot table exhausted */
    CANOPUS_SUP_ERR_BAD_SLOT = -4,     /* unknown / empty slot index */
    CANOPUS_SUP_ERR_STAGE = -5,        /* package staging failed */
    CANOPUS_SUP_ERR_LOAD = -6,         /* module load failed */
    CANOPUS_SUP_ERR_UNLOAD = -7,       /* module unload failed */
    CANOPUS_SUP_ERR_UNKNOWN_OP = -8,   /* unrecognized opcode */
    CANOPUS_SUP_ERR_BUSY = -9,         /* open refs / retained resources block unload */
    CANOPUS_SUP_ERR_SAFE_MODE = -10,   /* command disallowed by safe-mode policy */
    CANOPUS_SUP_ERR_REGISTRY = -11,    /* persisted registry present but unreadable/corrupt */
    CANOPUS_SUP_ERR_REGISTRY_OPEN = -12,
    CANOPUS_SUP_ERR_REGISTRY_WRITE = -13,
    CANOPUS_SUP_ERR_REGISTRY_CLOSE = -14,
    CANOPUS_SUP_ERR_REGISTRY_VERIFY_TMP = -15,
    CANOPUS_SUP_ERR_REGISTRY_RENAME = -16,
    CANOPUS_SUP_ERR_REGISTRY_VERIFY_FINAL = -17,
    CANOPUS_SUP_ERR_DESCRIPTOR_MISSING = -18,
    CANOPUS_SUP_ERR_DESCRIPTOR_INVALID = -19,
    CANOPUS_SUP_ERR_ACTIVATE = -20,
    CANOPUS_SUP_ERR_NOMEM = -21,       /* no heap domain could hold the image */
    CANOPUS_SUP_ERR_UPDATE_FALLBACK = -22, /* new generation failed to load; previous one restored */
    CANOPUS_SUP_ERR_NO_PREV = -23,     /* rollback requested but no other generation exists */
    CANOPUS_SUP_ERR_UPDATE_ABANDONED = -24, /* pending generation failed verification at switch */
    CANOPUS_SUP_ERR_UNSTABLE = -25,    /* module never stayed up through consecutive boots; disabled */
    CANOPUS_SUP_ERR_LAUNCHER = -26,    /* a launcher entry could not be registered with the firmware */
    CANOPUS_SUP_ERR_NOT_SAFE_MODE = -27, /* EXIT_SAFE_MODE outside safe mode */
    /* Exact-target installer diagnostics. These stay in the CPS1 status record
     * so a constrained watchface can surface a deterministic failing stage. */
    CANOPUS_SUP_ERR_STAGE_PATH = -101,
    CANOPUS_SUP_ERR_STAGE_RECEIPT = -102,
    CANOPUS_SUP_ERR_STAGE_RECEIPT_INVALID = -103,
    CANOPUS_SUP_ERR_STAGE_ARTIFACT = -104,
    CANOPUS_SUP_ERR_STAGE_DUPLICATE = -105,
    CANOPUS_SUP_ERR_STAGE_REGISTER = -106,
    CANOPUS_SUP_ERR_STAGE_RECEIPT_OPEN = -107,
    CANOPUS_SUP_ERR_STAGE_RECEIPT_READ = -108,
    CANOPUS_SUP_ERR_STAGE_DOWNGRADE = -109, /* receipt version is lower than the installed one */
    CANOPUS_SUP_ERR_STAGE_MOVE = -110,      /* moving the staged package into its generation failed */
    CANOPUS_SUP_ERR_STAGE_NO_TARGET = -111, /* package carries nothing for this firmware */
    /* Read-return diagnostics: -1100 is EOF, -1101 is the NuttX read
     * wrapper's generic -1 failure. */
    CANOPUS_SUP_ERR_STAGE_RECEIPT_READ_BASE = -1100,
    /* -1200 - errno captures NuttX read(2)'s specific failure after the
     * current-process wrapper returned -1. */
    CANOPUS_SUP_ERR_STAGE_RECEIPT_ERRNO_BASE = -1200,
};

/* CAN-P0-006: safe-mode reason and boot-state markers. */
enum canopus_safe_mode_reason {
    CANOPUS_SAFE_MODE_NONE = 0,
    CANOPUS_SAFE_MODE_USER_REQUESTED,
    CANOPUS_SAFE_MODE_CRASH,
    CANOPUS_SAFE_MODE_UNFINISHED_BOOT,
    CANOPUS_SAFE_MODE_STORE_CORRUPT,
    CANOPUS_SAFE_MODE_PROTOCOL_MISMATCH,
};

enum canopus_boot_state {
    CANOPUS_BOOT_BOOTING = 1,
    CANOPUS_BOOT_OK = 2,
};

/* What to do with the installed modules when safe mode is left. Applied as
 * next-boot intents, never as a load: KEEP clears the start counters only,
 * DISABLE_ALL turns every module off, ROLLBACK_ALL asks every module that has an
 * older generation to go back to it. */
enum canopus_safe_exit_action {
    CANOPUS_SAFE_EXIT_KEEP = 0,
    CANOPUS_SAFE_EXIT_DISABLE_ALL,
    CANOPUS_SAFE_EXIT_ROLLBACK_ALL,
};

/* Consecutive crash threshold that forces the next boot into safe mode. */
#define CANOPUS_SUP_CRASH_THRESHOLD 3u
/* Boots in a row that started modules and never proved stable before the
 * Supervisor puts the device in safe mode on its own. */
#define CANOPUS_SUP_UNFINISHED_LIMIT 3u

/* Global boot marker, kept in bytes 12..15 of the registry file (they were
 * reserved, so older registries read as a healthy first boot):
 *   bits 0..7   unfinished boots counted so far
 *   bit  8      safe mode (stays until EXIT_SAFE_MODE)
 *   bits 9..11  canopus_safe_mode_reason
 *   bit  12     trial: safe mode was left, the next boot must reach OK
 *   bit  13     booting: a boot started modules and has not reached OK
 *   bits 16..23 module quarantines since the last boot that reached OK */
#define CANOPUS_SUP_BOOT_UNFINISHED_MASK 0xffu
#define CANOPUS_SUP_BOOT_SAFE            (1u << 8)
#define CANOPUS_SUP_BOOT_REASON_SHIFT    9u
#define CANOPUS_SUP_BOOT_REASON_MASK     (7u << 9)
#define CANOPUS_SUP_BOOT_TRIAL           (1u << 12)
#define CANOPUS_SUP_BOOT_BOOTING         (1u << 13)
#define CANOPUS_SUP_BOOT_CRASH_SHIFT     16u

/* Boot-time crash-loop guard. Every start of an enabled module at boot is
 * written to the registry before its code runs and cleared once the module has
 * stayed up for CANOPUS_SUP_STABLE_MS. A fault takes the device down with the
 * count still on disk; after CANOPUS_SUP_START_LIMIT such starts in a row the
 * module is rolled back to its older generation when one exists, otherwise
 * disabled with CANOPUS_SUP_ERR_UNSTABLE. */
#define CANOPUS_SUP_START_LIMIT 2u
#define CANOPUS_SUP_STABLE_MS   60000u

/* CAN-P0-003: maximum length of a staged-object token. INSTALL never
 * accepts an arbitrary path; only a bounded basename token is allowed. */
#define CANOPUS_SUP_STAGE_TOKEN_MAX 128u
/* Legacy CPC1 QUERY arg0 value that reads the last failure without clearing
 * it. It is diagnostic-only and has no state-changing interpretation. */
#define CANOPUS_SUP_DIAG_QUERY_MAGIC 0x43514431u /* "CQD1" */

#define CANOPUS_SUP_MODULE_ID_MAX 32u
/* One shared namespace for managed native applications, independent of UI slots. */
#define CANOPUS_SUP_NATIVE_APP_FIRST_ID 0xCCu
#define CANOPUS_SUP_NATIVE_APP_IDS CANOPUS_SUP_MODULE_SLOTS
#define CANOPUS_SUP_NATIVE_PACKAGE_MAX (19u + CANOPUS_SUP_MODULE_ID_MAX)
enum canopus_sup_native_app_status {
    CANOPUS_SUP_NATIVE_APP_NONE = 0,
    CANOPUS_SUP_NATIVE_APP_PENDING,
    CANOPUS_SUP_NATIVE_APP_READY,
    CANOPUS_SUP_NATIVE_APP_FAILED,
};

/* Public slot flag bits (CPS1 / v2 query). */
#define CANOPUS_SUP_FLAG_HAS_PREV       (1u << 0) /* the other generation holds a package */
#define CANOPUS_SUP_FLAG_UPDATE_PENDING (1u << 1) /* generation switch applies at next load */
/* Registry slot flags (version 2, same 784-byte shape): bit0 = active
 * generation (0 = A, 1 = B), bit1 = pending generation switch, bits 2..31 =
 * zigzag-encoded activation error. Each module keeps two package generations
 * (A/B); an update is written to the inactive one and activated by flipping
 * the registry bit, so the running generation is never modified in place. */
#define CANOPUS_SUP_REG_FLAG_ACTIVE_B   (1u << 0)
#define CANOPUS_SUP_REG_FLAG_PENDING    (1u << 1)
#define CANOPUS_SUP_REGISTRY_ERROR_SHIFT 2u

/* CAN-P0-005 revision (next-boot lifecycle): enable/disable/remove are never
 * hot operations. Each slot carries a persisted *boot intent* — what the
 * next supervisor load should do with the module. INSTALL always records
 * DISABLED, so a freshly installed module is never loaded. */
enum canopus_sup_intent {
    CANOPUS_SUP_INTENT_DISABLED = 0, /* never load at boot (default) */
    CANOPUS_SUP_INTENT_ENABLED  = 1, /* load at boot */
    CANOPUS_SUP_INTENT_REMOVE   = 2, /* delete artifacts + drop at boot */
};

/* Fixed-format on-disk registry that makes module slots survive reboot and
 * canopus reinstall. One file, written atomically (tmp + rename) on every
 * mutation, read back at supervisor load. Format:
 *   header (16): u32 magic "CRD1", u32 version, u32 module_count, u32 rsvd
 *   16 slots x 48: module_id[32] + u32 class + u32 version + u32 flags/error +
 *                  u32 intent (bits 0..7) | starts << 8 | native_app_id << 16
 * Registry flags: see CANOPUS_SUP_REG_FLAG_*; bits 2..31 retain a zigzag-encoded
 * activation error. Old files used zero in those bits and remain valid.
 * The slot table is the in-memory source of truth; the registry is the
 * boot-time restore source. */
#define CANOPUS_SUP_REGISTRY_MAGIC   0x31524443u /* "CRD1" */
#define CANOPUS_SUP_REGISTRY_VERSION 2u
#define CANOPUS_SUP_REGISTRY_HEADER  16u
#define CANOPUS_SUP_REGISTRY_SLOT_SIZE 48u
#define CANOPUS_SUP_REGISTRY_SIZE \
    (CANOPUS_SUP_REGISTRY_HEADER + \
     CANOPUS_SUP_MODULE_SLOTS * CANOPUS_SUP_REGISTRY_SLOT_SIZE)

/* A tracked module slot. */
struct canopus_sup_module_v1 {
    uint32_t state;            /* CANOPUS_STATE_* */
    uint32_t lifecycle_class;  /* CANOPUS_LIFECYCLE_* */
    uint32_t version;
    uint32_t flags;            /* CANOPUS_SUP_FLAG_* */
    uint32_t intent;           /* CANOPUS_SUP_INTENT_* (persisted) */
    /* CAN-P0-008/CAN-P1-007: stable module identity, used to resolve v2
     * per-module commands by id instead of a UI index. Not part of the
     * 384-byte CPS1 slot render (which keeps the 16-byte stride). */
    uint8_t module_id[CANOPUS_SUP_MODULE_ID_MAX];
    const struct canopus_module_descriptor_v1 *descriptor;
    uint32_t activation_error;
    /* Package generation state (persisted in the registry flags). */
    uint8_t active_gen;
    uint8_t pending_switch;
    /* Set for one boot after a generation switch; load failure then falls
     * back to the previous generation. */
    uint8_t just_switched;
    /* Consecutive boot starts that did not reach CANOPUS_SUP_STABLE_MS
     * (persisted in bits 8..15 of the registry intent word). */
    uint8_t starts;
    /* Started this boot and not yet proven stable; started_ms is its start. */
    uint8_t watching;
    /* Transient explicit-activation publication: 1 = pages, 2 = launcher. */
    uint8_t native_app_stage;
    /* Stable native identity, persisted in registry intent bits 16..23. */
    uint8_t native_app_id;
    uint8_t native_app_status;
    uint32_t started_ms;
};

/* Supervisor model. */
struct canopus_supervisor_v1 {
    uint32_t abi;
    uint32_t framework_revision;
    uint32_t safe_mode;
    uint32_t module_count;
    uint32_t pending_op;       /* last CANOPUS_SUP_CMD_* */
    uint32_t pending_state;    /* CANOPUS_RESULT_* */
    uint32_t flags;
    uint32_t error_code;
    uint32_t scan_summary;     /* see CANOPUS_SUP_STATUS_SCAN_OFF */
    struct canopus_sup_module_v1 modules[CANOPUS_SUP_MODULE_SLOTS];
    int32_t  selected;         /* arg0 from the last command */
    int32_t  loading_slot;     /* slot allowed to self-register during insmod */
    /* CAN-P1-003: sequence snapshot guarding the status record. Every state
     * mutation runs under canopus_snapshot_begin/commit; the sequence value
     * is embedded in the status at SEQ_BEGIN/SEQ_END and advances by 2 per
     * command. A reader accepts the record only when begin == end (even). */
    struct canopus_snapshot_v1 snap;
    /* CAN-P0-006: safe-mode reason, trigger boot id, failing module and a
     * saturating crash counter, plus the boot-state marker. The command
     * policy matrix consults these; boot persistence is device-gated. */
    uint32_t safe_mode_reason;
    uint32_t safe_mode_boot_id;
    uint32_t failing_module_id;
    uint32_t crash_counter;
    uint32_t boot_state;
    uint32_t boot_unfinished;  /* unfinished boots in a row, persisted */
    uint32_t boot_trial;       /* this boot follows an EXIT_SAFE_MODE */
    uint32_t exit_pending;     /* EXIT_SAFE_MODE accepted; takes effect at reboot */
    uint32_t boot_saved;       /* the boot word last read from or written to the registry */
    uint32_t unstable_modules; /* slots with `watching` set */
    uint32_t boot_persistence;
    uint8_t boot_restored;
    uint8_t boot_notice_ready;
    uint8_t boot_notice_sent;
    /* CAN-P0-008: v2 transport state. `last_kind` is 0 for a legacy CPC1
     * command, 1 for a v2 CPC2 request; a read returns the stored v2
     * response when the last write was v2, else the legacy CPS1 status. */
    uint8_t  last_kind;
    uint8_t  v2_response_buf[CANOPUS_TRANSPORT_V2_HEADER_SIZE +
                             CANOPUS_PROTO_MAX_PAYLOAD];
    uint32_t v2_response_len;
    /* Public installer endpoint: separate response mailbox, INSTALL only.
     * The four-byte payload is the installation diagnostic error code. */
    uint8_t installer_response[CANOPUS_TRANSPORT_V2_HEADER_SIZE + 4u];
    uint32_t installer_response_len;
    /* v2 pending-request tracking (CAN-P1-002) */
    struct canopus_pending_table_v1 pending;
    struct {
        uint32_t active;
        int fd;
        uint32_t size;
        uint32_t written;
        /* Resources are accepted only after the artifact stream is complete. */
        uint32_t resources_ready;
        char id[CANOPUS_SUP_MODULE_ID_MAX];
        uint8_t receipt[CANOPUS_INSTALL_RECEIPT_SIZE];
    } upload;
    struct {
        uint32_t active;
        int fd;
        uint32_t size;
        uint32_t written;
        char name[CANOPUS_FS_NAME_MAX + 1u];
        uint8_t sha256[32];
    } resource_upload;
    /* platform hooks (see canopus_supervisor_platform.h) */
    const struct canopus_sup_platform_v1 *platform;
    void *platform_cookie;
};

uint32_t canopus_supervisor_upload(struct canopus_supervisor_v1 *sup,
                                  uint32_t command, const uint8_t *payload,
                                  uint32_t len);

/* Lifecycle helpers reused from the portable runtime. */
int canopus_supervisor_init(struct canopus_supervisor_v1 *sup,
                            uint32_t framework_revision,
                            const struct canopus_sup_platform_v1 *platform,
                            void *cookie);
/* Handles one 16-byte command; returns the result state. */
uint32_t canopus_supervisor_handle_command(struct canopus_supervisor_v1 *sup,
                                           const uint8_t command[CANOPUS_SUP_COMMAND_SIZE]);
/* Renders the 384-byte status ABI into out. Returns 0 on success. */
int canopus_supervisor_render_status(const struct canopus_supervisor_v1 *sup,
                                     uint8_t out[CANOPUS_SUP_STATUS_SIZE]);

/* Character-device transfer helpers. A successful operation returns the
 * number of bytes consumed/produced, as required by the NuttX read/write ABI. */
int32_t canopus_supervisor_device_read(struct canopus_supervisor_v1 *sup,
                                       void *buffer, uint32_t count);
int32_t canopus_supervisor_device_write(struct canopus_supervisor_v1 *sup,
                                        const void *buffer, uint32_t count);
int32_t canopus_supervisor_installer_read(struct canopus_supervisor_v1 *sup,
                                         void *buffer, uint32_t count);
int32_t canopus_supervisor_installer_write(struct canopus_supervisor_v1 *sup,
                                          const void *buffer, uint32_t count);

/* ABI helpers for the char-device front end (host test uses them too). */
int canopus_supervisor_validate_command(const uint8_t command[CANOPUS_SUP_COMMAND_SIZE]);

/* CAN-P0-008: handle one v2 request envelope against the same core
 * dispatch as the legacy path. Tracks the request in the supervisor's
 * pending table and fills `resp` (result_state + request id echo). `opcode`
 * is set to the request's command so a v2 response can echo it. Returns 0
 * on success (a response is always produced, including REJECTED). */
int canopus_supervisor_handle_v2_request(struct canopus_supervisor_v1 *sup,
                                         const struct canopus_proto_request_v1 *req,
                                         const void *payload,
                                         struct canopus_proto_response_v1 *resp,
                                         uint32_t *opcode);
/* In-process manager transport for an embedded/native UI in the same module.
 * It validates the portable request envelope and dispatches directly to the
 * same v2 supervisor handler without registering/opening a character device.
 * Query payloads are still obtained through the target's snapshot refresh. */
int canopus_supervisor_local_transport(
    const struct canopus_proto_request_v1 *req, const void *payload,
    struct canopus_proto_response_v1 *resp, void *cookie);

/* Handles an incoming WearPacket L2 packet payload on CANOPUS_L2_CHANNEL (0x43).
 * `l2_payload` points to the CPC2 request wire bytes (after L2 decryption if
 * WriteEnc was used).
 * Processes the request through the supervisor state machine and encodes the
 * complete CPC2 response into `resp_buf`.
 * Returns the number of bytes written to `resp_buf` (>= 0), or -1 if the frame
 * is malformed, unrecognized, or the output buffer is insufficient. */
int32_t canopus_supervisor_handle_l2_packet(struct canopus_supervisor_v1 *sup,
                                            const uint8_t *l2_payload,
                                            uint32_t l2_payload_len,
                                            uint8_t *resp_buf,
                                            uint32_t resp_cap);

/* The module glue owns the singleton; the platform's read/write handlers use
 * it to render status / dispatch commands. */
struct canopus_supervisor_v1 *canopus_supervisor_get(void);

/* Activate enabled slots after metadata was restored by the supervisor's own
 * constructor. This must run from a regular miwear UI callback (Manager or
 * installer watchface command), never from the loader's restricted stack. It
 * is idempotent for one boot. */
int canopus_supervisor_restore_after_boot(void);

/* Host convenience: record a newly installed module into a free slot.
 * `module_id` is copied (bounded) and becomes the stable identity used by
 * v2 per-module commands. Returns the slot index or -1 when the table is
 * full / class invalid / id too long. */
int canopus_supervisor_add_module(struct canopus_supervisor_v1 *sup,
                                  uint32_t lifecycle_class,
                                  uint32_t version,
                                  const char *module_id);

/* ---- Supervisor service table (ADR-CAN-006) ------------------------- */

/* Descriptor target id of portable modules; accepted on every Supervisor. */
#define CANOPUS_PORTABLE_TARGET_ID "portable-thumbv8m"

/* Per-module context handed to descriptor callbacks and to the module's init
 * entry (as its first argument on custom-loader targets). */
const struct canopus_context_v1 *canopus_supervisor_context(
    struct canopus_supervisor_v1 *sup, uint32_t slot);
/* Binds the service table to the Supervisor (called by init). */
void canopus_supervisor_services_bind(struct canopus_supervisor_v1 *sup);
/* Dispatches due module timers, including during sleep_ms. UI posts wait for
 * the platform's separate foreground heartbeat callback. */
void canopus_supervisor_timers_poll(struct canopus_supervisor_v1 *sup);
/* Starts the heartbeat that runs canopus_supervisor_stability_poll. Non-zero
 * when the platform has no event source, in which case a module counts as
 * stable once it is up. */
int canopus_supervisor_watch_stability(struct canopus_supervisor_v1 *sup);
/* Registers the Supervisor's work on the owner timer that keeps ticking while
 * the screen is off; safe to call from any task. */
int canopus_supervisor_start_background(struct canopus_supervisor_v1 *sup);
void canopus_supervisor_boot_ready(struct canopus_supervisor_v1 *sup);
void canopus_supervisor_boot_notify(struct canopus_supervisor_v1 *sup);
/* Closes the slot's open files, wake locks, vibration, brightness override and
 * L2 receive registrations; called when its artifacts are removed. */
void canopus_supervisor_release_services(struct canopus_supervisor_v1 *sup,
                                         uint32_t slot);
/* ctx->bt->l2_register_rx support for the L2 backend. registered reports whether
 * a live module claimed the channel (so the backend keeps the frame instead of
 * handing it to the stock handler); dispatch delivers one decrypted frame on
 * the module/UI task and returns 1 when a module took it. */
int canopus_supervisor_l2_rx_registered(uint8_t channel);
int canopus_supervisor_l2_rx_dispatch(struct canopus_supervisor_v1 *sup, uint8_t channel,
                                      const void *payload, uint16_t len);
/* CanopusConnect (ADR-CAN-009). bind resets the listener state and returns the
 * module-facing object, or 0 when the platform has no L2 event hooks. request
 * handles one CONNECT_SEND/CONNECT_LISTEN L2 request on the module/UI task:
 * it returns the CANOPUS_RESULT_* and the CANOPUS_CONNECT_STATUS_* byte, and
 * touches none of the Manager's state. release drops a slot's receiver. */
const struct canopus_connect_obj *canopus_supervisor_connect_bind(struct canopus_supervisor_v1 *sup);
uint32_t canopus_supervisor_connect_request(const struct canopus_proto_request_v1 *req,
                                            const uint8_t *payload, uint8_t *status);
void canopus_supervisor_connect_release(uint32_t slot);
/* Copies the page entry module `slot` registered through ctx->ui->set_entry; its
 * label points to storage that outlives the call. Returns 0, or -1 when the slot
 * has no entry or no loaded module. */
int canopus_supervisor_ui_entry(struct canopus_supervisor_v1 *sup, uint32_t slot,
                                struct canopus_ui_entry_v1 *out);
/* Owner-task services: posts run only on the next foreground UI heartbeat,
 * after synchronous bus callbacks unwind. Release cancels pending work. */
int32_t canopus_supervisor_ui_post(const struct canopus_context_v1 *ctx,
                                  canopus_ui_task_fn callback, void *user_data);
int32_t canopus_supervisor_ui_pump_ensure(struct canopus_supervisor_v1 *sup);
int canopus_supervisor_bus_dispatching(void);
void canopus_supervisor_module_changed(struct canopus_supervisor_v1 *sup, uint32_t slot);
void canopus_supervisor_module_events_flush(struct canopus_supervisor_v1 *sup);

/* Log ring (newest last). Returns the message or 0 past the end. */
uint32_t canopus_supervisor_log_count(void);
const char *canopus_supervisor_log_entry(uint32_t index, uint32_t *level,
                                         uint32_t *slot);

/* Stages the package a miwear client wrote to the inbox for `token`
 * (<token>.cmi.next / <token>.ko.next). A token without a slot is installed
 * (disabled) into generation A and registered: returns 0. A token with a slot
 * and a strictly higher module version is moved into the inactive generation
 * and marked pending: returns 1 (reboot required). Same or lower versions are
 * rejected. Returns -1 with error_code set on failure. The caller persists. */
int canopus_supervisor_stage_token(struct canopus_supervisor_v1 *sup,
                                   const char *token);

/* Imports every completed native Canopus staging pair through the
 * platform's list/import hooks and stages each (fresh install or update).
 * Same-or-older versions are already current and are not failures. Returns the
 * result state (COMPLETED, REBOOT_REQUIRED when an update was staged, FAILED
 * when nothing could be imported and something was rejected). */
uint32_t canopus_supervisor_scan_packages(struct canopus_supervisor_v1 *sup);

/* CAN-P0-005 revision (next-boot): persist the whole slot table (module id,
 * class, version, flags, boot intent) through the platform `persist` hook.
 * Returns 0 on success; a failure leaves the in-memory table authoritative
 * for the current session but the change will not survive reboot. */
int canopus_supervisor_save_registry(struct canopus_supervisor_v1 *sup);
/* Restore the slot table from the platform `restore` hook. Absence of a
 * registry (fresh install) is not an error. Enabled intents are loaded via
 * the platform `load_module` hook; remove intents have their artifacts
 * deleted through `remove_artifact` and are not re-registered. Returns 0. */
int canopus_supervisor_restore_registry(struct canopus_supervisor_v1 *sup);
/* Restore only persistent slot metadata. This preserves module count and boot
 * intents while keeping nested module loading out of the stock `insmod` stack. */
int canopus_supervisor_restore_registry_metadata(struct canopus_supervisor_v1 *sup);
/* Load and activate enabled slots previously restored as metadata. */
int canopus_supervisor_activate_restored_modules(struct canopus_supervisor_v1 *sup);
/* Reserve and persist one module's native identity on the serialized owner.
 * Returns its app ID, or a negative service error. All native registration
 * paths use this allocator; the firmware's existing IDs are also reserved. */
int32_t canopus_supervisor_native_app_id(struct canopus_supervisor_v1 *sup,
                                        uint32_t slot);
/* Publish native apps for loaded modules from a caller-owned UI-process
 * bootstrap transaction. Explicit activation uses the foreground pump below;
 * neither path may publish from boot restore or a worker. */
int canopus_supervisor_publish_native_apps(struct canopus_supervisor_v1 *sup,
                                           uint32_t stage);
/* Advance one explicitly activated app on a foreground UI heartbeat. */
void canopus_supervisor_native_apps_poll(struct canopus_supervisor_v1 *sup);

int canopus_supervisor_register_descriptor(
    struct canopus_supervisor_v1 *sup,
    const char *module_id,
    const struct canopus_module_descriptor_v1 *descriptor);

/* CAN-P0-006: boot markers, persisted in the registry file. boot_begin records
 * BOOTING (before any third-party module runs); boot_ok commits OK once every
 * module started this boot has stayed up for CANOPUS_SUP_STABLE_MS. A boot that
 * never commits OK counts as unfinished; CANOPUS_SUP_UNFINISHED_LIMIT of them in
 * a row (one, right after EXIT_SAFE_MODE), or CANOPUS_SUP_CRASH_THRESHOLD module
 * quarantines without a good boot in between, put the next start in safe mode.
 * Safe mode is sticky: it survives reboots until EXIT_SAFE_MODE. */
void canopus_supervisor_boot_begin(struct canopus_supervisor_v1 *sup,
                                   uint32_t boot_id);
void canopus_supervisor_boot_ok(struct canopus_supervisor_v1 *sup);
/* Clears the start count of every watched module that has now run for
 * CANOPUS_SUP_STABLE_MS. Called from the service heartbeat. */
void canopus_supervisor_stability_poll(struct canopus_supervisor_v1 *sup,
                                       uint32_t now_ms);
/* CAN-P2-016: saturating increment of the crash counter. */
void canopus_supervisor_record_crash(struct canopus_supervisor_v1 *sup);
/* Returns non-zero when the supervisor should start this boot in safe mode
 * (the previous boot was not marked OK, a crash counter threshold was hit,
 * or store recovery demands it). */
int canopus_supervisor_boot_should_safe_mode(const struct canopus_supervisor_v1 *sup);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SUPERVISOR_H */
