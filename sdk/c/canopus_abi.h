/*
 * canopus_abi.h — public Canopus module ABI v1.
 *
 * Only stable Canopus types live here. Firmware-private structs, addresses
 * and calling constraints belong to the target pack and generated code, not
 * to this header (architecture §3.5).
 *
 * Layout notes:
 *  - all fields are fixed-width, natural 4-byte alignment (ARM AAPCS).
 *  - no #pragma pack; sizes are validated with CANOPUS_STATIC_ASSERT.
 */
#ifndef CANOPUS_ABI_H
#define CANOPUS_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Fixed-width ABI types                                               */
/* ------------------------------------------------------------------ */

#define CANOPUS_ABI_MAJOR 1u
#define CANOPUS_ABI_MINOR 2u

/* Module descriptor flags. */
#define CANOPUS_FLAG_HAS_NATIVE_APP        (1u << 0)
#define CANOPUS_FLAG_NATIVE_APP_INTEGRATED  (1u << 1)
#define CANOPUS_FLAG_NATIVE_APP_STANDALONE  (1u << 2)
#define CANOPUS_FLAG_REGISTERS_LAUNCHER_ENTRY (1u << 3)
#define CANOPUS_FLAG_REQUIRES_UI_DISPATCHER (1u << 4)
#define CANOPUS_FLAG_APP_UNREGISTER_REBOOT_REQUIRED (1u << 5)

/* Lifecycle classes. */
#define CANOPUS_LIFECYCLE_REMOVABLE            0
#define CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION 1
#define CANOPUS_LIFECYCLE_ALWAYS_RESIDENT      2
#define CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED 3

/* Upper bound for the module descriptor's append-only struct_size. A
 * descriptor advertising more than this is treated as a different/unknown
 * layout and fails closed. */
#define CANOPUS_MODULE_DESCRIPTOR_MAX_SIZE 256u

/* Every flag bit the current ABI knows; any bit outside this mask is
 * rejected by canopus_module_descriptor_check. */
#define CANOPUS_MODULE_FLAGS_KNOWN 0x3Fu /* CANOPUS_FLAG_* bits 0..5 */

/* ------------------------------------------------------------------ */
/* Module descriptor                                                   */
/* ------------------------------------------------------------------ */

struct canopus_context_v1;
struct canopus_status_writer_v1;

struct canopus_module_descriptor_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint8_t module_id[32];
    uint8_t module_version[16];
    uint8_t build_id[32];
    uint8_t target_id[32];

    int32_t (*prepare)(const struct canopus_context_v1 *context);
    int32_t (*activate)(const struct canopus_context_v1 *context);
    int32_t (*deactivate)(const struct canopus_context_v1 *context);
    int32_t (*stop)(const struct canopus_context_v1 *context);
    int32_t (*query)(struct canopus_status_writer_v1 *writer);
    /* ABI 1.1 append-only callback. The supervisor invokes this only from a
     * caller-owned UI-process bootstrap transaction, never during boot
     * activation or from a Bluetooth worker. */
    int32_t (*publish_native_app)(const struct canopus_context_v1 *context);
    /* ABI 1.2 append-only callback. `stage` is 1 for app/page registration and
     * 2 for Launcher publication; each call runs in a separate UI event turn. */
    int32_t (*publish_native_app_stage)(
        const struct canopus_context_v1 *context, uint32_t stage);
};

#define CANOPUS_MODULE_DESCRIPTOR_V1_0_SIZE \
    ((uint32_t)offsetof(struct canopus_module_descriptor_v1, \
                        publish_native_app))
#define CANOPUS_MODULE_DESCRIPTOR_V1_1_SIZE \
    ((uint32_t)offsetof(struct canopus_module_descriptor_v1, \
                        publish_native_app_stage))

/* ------------------------------------------------------------------ */
/* Control plane                                                       */
/* ------------------------------------------------------------------ */

struct canopus_control_header_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t command;
    uint32_t request_id;
    uint32_t payload_size;
    uint32_t flags;
};

/* Response states — the device must distinguish queued/run/complete. */
enum canopus_result_state {
    CANOPUS_RESULT_REJECTED = 1,
    CANOPUS_RESULT_ACCEPTED,
    CANOPUS_RESULT_QUEUED,
    CANOPUS_RESULT_RUNNING,
    CANOPUS_RESULT_COMPLETED,
    CANOPUS_RESULT_FAILED,
    CANOPUS_RESULT_DISALLOWED,
    CANOPUS_RESULT_REBOOT_REQUIRED,
};

/* ------------------------------------------------------------------ */
/* Sequence snapshot                                                   */
/*                                                                     */
/* Writer: set sequence to odd, write fields, then publish the same    */
/* even begin/end sequence. Reader: accept only when begin == end and   */
/* even. Protects readers from partial snapshots on a single-threaded   */
/* event loop; not a memory barrier by itself.                          */
/* ------------------------------------------------------------------ */

struct canopus_snapshot_v1 {
    volatile uint32_t sequence; /* odd while writing, even when valid */
};

#define CANOPUS_SNAPSHOT_READY(snap) \
    (((snap)->sequence & 1u) == 0u)

void canopus_snapshot_begin(struct canopus_snapshot_v1 *snap);
void canopus_snapshot_commit(struct canopus_snapshot_v1 *snap);

/* ------------------------------------------------------------------ */
/* Status writer (fixed-width append-only record)                       */
/* ------------------------------------------------------------------ */

/* Max payload bytes per status record; reader must accept <= this. */
#define CANOPUS_STATUS_RECORD_MAX 128u

/* Saturation bound for the writer's dropped counter (no wrap). */
#define CANOPUS_STATUS_WRITER_DROPPED_MAX 0xFFFFFFFFu

/* Writer lifecycle. init enters WRITING for the first record; begin starts
 * a new record after publish; publish makes the current record valid. */
enum canopus_status_writer_state {
    CANOPUS_STATUS_WRITER_IDLE = 0,
    CANOPUS_STATUS_WRITER_WRITING = 1,
    CANOPUS_STATUS_WRITER_PUBLISHED = 2,
};

struct canopus_status_writer_v1 {
    uint8_t *buf;
    uint32_t capacity;
    uint32_t used;         /* bytes written so far */
    uint32_t dropped;      /* saturating; increments if writer ran out of space */
    uint32_t state;        /* canopus_status_writer_state */
    struct canopus_snapshot_v1 snap;
};

int canopus_status_writer_init(struct canopus_status_writer_v1 *w,
                               uint8_t *buf, uint32_t capacity);
/* Starts a NEW record after a publish (resets used). Fails while a record
 * is already being written. */
int canopus_status_writer_begin(struct canopus_status_writer_v1 *w);
/* Appends a fixed-width field. Returns 0 on success, -1 on overflow
 * (and saturating-increments dropped) or when not in the WRITING state. */
int canopus_status_put_u8(struct canopus_status_writer_v1 *w, uint8_t v);
int canopus_status_put_u16(struct canopus_status_writer_v1 *w, uint16_t v);
int canopus_status_put_u32(struct canopus_status_writer_v1 *w, uint32_t v);
int canopus_status_put_bytes(struct canopus_status_writer_v1 *w,
                             const void *src, uint32_t len);
/* Marks the record valid (publishes even sequence). Requires WRITING
 * state; a double publish fails. Returns 0 on success. */
int canopus_status_writer_publish(struct canopus_status_writer_v1 *w);

/* ------------------------------------------------------------------ */
/* Capability query                                                     */
/* ------------------------------------------------------------------ */

#define CANOPUS_CAP_MAGIC 0x43415031u /* "CAP1" */

struct canopus_capability_query_v1 {
    uint32_t magic;
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    /* Returns true when the named capability is available on this target. */
    int (*has)(const struct canopus_capability_query_v1 *q, const char *name);
    void *private_data;
};

/* ------------------------------------------------------------------ */
/* Compile-time ABI checks                                              */
/* ------------------------------------------------------------------ */

#ifdef __cplusplus
#define CANOPUS_STATIC_ASSERT(c, m) static_assert((c), m)
#else
#define CANOPUS_STATIC_ASSERT_CONCAT_(a, b) a##b
#define CANOPUS_STATIC_ASSERT_CONCAT(a, b) CANOPUS_STATIC_ASSERT_CONCAT_(a, b)
#define CANOPUS_STATIC_ASSERT(c, m) \
    typedef char CANOPUS_STATIC_ASSERT_CONCAT(canopus_static_assert_, __LINE__)[(c) ? 1 : -1]
#endif

CANOPUS_STATIC_ASSERT(sizeof(struct canopus_control_header_v1) == 24,
                      "canopus_control_header_v1 size");
CANOPUS_STATIC_ASSERT(sizeof(struct canopus_snapshot_v1) == 4,
                      "canopus_snapshot_v1 size");

/* The fixed-width prefix of the module descriptor is pointer-independent. */
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1, module_id) == 12,
                      "canopus_module_descriptor_v1 module_id offset");
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1, target_id) == 92,
                      "canopus_module_descriptor_v1 target_id offset");

/* The callbacks follow the prefix; their start offset and the total size
 * depend on pointer width (4-byte on ARM32 target, 8-byte on 64-bit host). */
#if UINTPTR_MAX == 0xffffffffu
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1, prepare) == 124,
                      "canopus_module_descriptor_v1 prepare offset (32-bit)");
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1,
                              publish_native_app_stage) == 148,
                      "canopus_module_descriptor_v1 staged callback offset (32-bit)");
CANOPUS_STATIC_ASSERT(sizeof(struct canopus_module_descriptor_v1) == 152,
                      "canopus_module_descriptor_v1 32-bit size");
#else
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1, prepare) == 128,
                      "canopus_module_descriptor_v1 prepare offset (64-bit host)");
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_module_descriptor_v1,
                              publish_native_app_stage) == 176,
                      "canopus_module_descriptor_v1 staged callback offset (64-bit host)");
CANOPUS_STATIC_ASSERT(sizeof(struct canopus_module_descriptor_v1) == 184,
                      "canopus_module_descriptor_v1 64-bit host size");
#endif

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_ABI_H */
