/*
 * canopus_supervisor.h — device-side Canopus supervisor module (Phase 5).
 *
 * A native module (boot-resident, like btpatch_phase5) that registers the
 * /dev/canopus char device and implements the installer control ABI that the
 * Lua installer watchface drives. This is the concrete Phase 5 supervisor:
 *   - a fixed 384-byte status ABI (read from /dev/canopus);
 *   - a fixed 16-byte command ABI (written to /dev/canopus);
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
    CANOPUS_SUP_CMD_UPDATE,
    CANOPUS_SUP_CMD_ROLLBACK,
    CANOPUS_SUP_CMD_ENTER_SAFE_MODE,
    CANOPUS_SUP_CMD_ACTIVATE,
    /* Apply enabled boot intents after the supervisor's own insmod has returned.
     * The installer watchface invokes this from a Lua/LVGL event turn before
     * staged native-app publication. */
    CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT,
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
    /* Exact-target installer diagnostics. These stay in the CPS1 status record
     * so a constrained watchface can surface a deterministic failing stage. */
    CANOPUS_SUP_ERR_STAGE_PATH = -101,
    CANOPUS_SUP_ERR_STAGE_RECEIPT = -102,
    CANOPUS_SUP_ERR_STAGE_SIGNATURE = -103,
    CANOPUS_SUP_ERR_STAGE_ARTIFACT = -104,
    CANOPUS_SUP_ERR_STAGE_DUPLICATE = -105,
    CANOPUS_SUP_ERR_STAGE_REGISTER = -106,
    CANOPUS_SUP_ERR_STAGE_RECEIPT_OPEN = -107,
    CANOPUS_SUP_ERR_STAGE_RECEIPT_READ = -108,
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

/* Consecutive crash threshold that forces the next boot into safe mode. */
#define CANOPUS_SUP_CRASH_THRESHOLD 3u

/* CAN-P0-003: maximum length of a staged-object token. INSTALL never
 * accepts an arbitrary path; only a bounded basename token is allowed. */
#define CANOPUS_SUP_STAGE_TOKEN_MAX 128u
/* Legacy CPC1 QUERY arg0 value that reads the last failure without clearing
 * it. It is diagnostic-only and has no state-changing interpretation. */
#define CANOPUS_SUP_DIAG_QUERY_MAGIC 0x43514431u /* "CQD1" */

#define CANOPUS_SUP_MODULE_ID_MAX 32u

/* Slot flag bits. bit0 is the legacy signature_ok bit (also rendered in the
 * 384-byte CPS1 status). The registry uses the otherwise-unused upper 31 bits
 * to retain a zigzag-encoded activation error without changing the version-1
 * 784-byte file shape. In-memory flags never contain the encoded error. */
#define CANOPUS_SUP_FLAG_SIGNATURE_OK  (1u << 0)
#define CANOPUS_SUP_FLAG_PUBLIC_MASK   CANOPUS_SUP_FLAG_SIGNATURE_OK
#define CANOPUS_SUP_REGISTRY_ERROR_SHIFT 1u

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
 *                  u32 intent
 * Registry flags bit 0 is signature_ok; bits 1..31 retain a zigzag-encoded
 * activation error. Old files used zero in those bits and remain valid.
 * The slot table is the in-memory source of truth; the registry is the
 * boot-time restore source. */
#define CANOPUS_SUP_REGISTRY_MAGIC   0x31524443u /* "CRD1" */
#define CANOPUS_SUP_REGISTRY_VERSION 1u
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
    /* CAN-P0-008: v2 transport state. `last_kind` is 0 for a legacy CPC1
     * command, 1 for a v2 CPC2 request; a read returns the stored v2
     * response when the last write was v2, else the legacy CPS1 status. */
    uint8_t  last_kind;
    uint8_t  v2_response_buf[CANOPUS_TRANSPORT_V2_HEADER_SIZE +
                             CANOPUS_PROTO_MAX_PAYLOAD];
    uint32_t v2_response_len;
    /* v2 pending-request tracking (CAN-P1-002) */
    struct canopus_pending_table_v1 pending;
    /* platform hooks (see canopus_supervisor_platform.h) */
    const struct canopus_sup_platform_v1 *platform;
    void *platform_cookie;
};

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
                                  uint32_t signature_ok,
                                  const char *module_id);

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
/* Publish native apps for loaded modules from a caller-owned UI-process
 * bootstrap transaction. Never call this from boot restore or a worker. */
int canopus_supervisor_publish_native_apps(struct canopus_supervisor_v1 *sup,
                                           uint32_t stage);

int canopus_supervisor_register_descriptor(
    struct canopus_supervisor_v1 *sup,
    const char *module_id,
    const struct canopus_module_descriptor_v1 *descriptor);

/* CAN-P0-006: boot markers. boot_begin records BOOTING (before loading any
 * third-party module); boot_ok commits BOOT_OK once READY. A boot that never
 * commits BOOT_OK is the signal for the next boot to auto-enter safe mode. */
void canopus_supervisor_boot_begin(struct canopus_supervisor_v1 *sup,
                                   uint32_t boot_id);
void canopus_supervisor_boot_ok(struct canopus_supervisor_v1 *sup);
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
