/*
 * canopus_runtime.h — portable Canopus module runtime API v1.
 *
 * Host-testable C implementation of lifecycle, resource tracking, callback
 * generation guards and diagnostics. All code here is target-independent;
 * the host fake target provides the platform hooks.
 */
#ifndef CANOPUS_RUNTIME_H
#define CANOPUS_RUNTIME_H

#include <stdint.h>
#include "canopus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================== */
/* Lifecycle state machine                                             */
/*                                                                     */
/* See architecture §10.3. Illegal transitions return -1. State changes */
/* bump a generation so callbacks/timers can detect staleness.          */
/* ================================================================== */

enum canopus_state {
    CANOPUS_STATE_DISCOVERED = 1,
    CANOPUS_STATE_VERIFIED,
    CANOPUS_STATE_INSTALLED,
    CANOPUS_STATE_DISABLED,
    CANOPUS_STATE_ENABLED,
    CANOPUS_STATE_LOADING,
    CANOPUS_STATE_PREPARING,
    CANOPUS_STATE_READY,
    CANOPUS_STATE_ACTIVE,
    CANOPUS_STATE_STOPPING,
    CANOPUS_STATE_DRAINING,
    CANOPUS_STATE_UNLOADED,
    CANOPUS_STATE_BOOT_RESIDENT,
    CANOPUS_STATE_DISABLED_NEXT_BOOT,
    CANOPUS_STATE_FAILED,
    CANOPUS_STATE_FAIL_STOP,
    CANOPUS_STATE_QUARANTINED_NEXT_BOOT,
    CANOPUS_STATE_UPDATE_STAGED,
    CANOPUS_STATE_REBOOT_REQUIRED,
    CANOPUS_STATE_REMOVE_PENDING,
};

struct canopus_lifecycle_v1 {
    uint32_t state;
    uint32_t lifecycle_class; /* CANOPUS_LIFECYCLE_* */
    uint32_t generation;
    uint32_t reserved;
};

/* Returns 0 on success; -1 on illegal transition. */
int canopus_lifecycle_init(struct canopus_lifecycle_v1 *lc,
                           uint32_t lifecycle_class);
int canopus_lifecycle_transition(struct canopus_lifecycle_v1 *lc,
                                 uint32_t to_state);
int canopus_lifecycle_allow(uint32_t from, uint32_t to,
                            uint32_t lifecycle_class);
const char *canopus_state_name(uint32_t state);

/* ================================================================== */
/* Resource tracker                                                    */
/*                                                                     */
/* A resource is a handle + ownership state. Releasing twice is an      */
/* error (double free). DETACHED means the underlying namespace entry   */
/* is gone (e.g. unregister returned EBUSY but the name was unlinked)    */
/* and must NOT be retried blindly.                                     */
/* ================================================================== */

#define CANOPUS_RESOURCE_MAX 32u

enum canopus_resource_kind {
    CANOPUS_RESOURCE_CHAR_DEVICE = 1,
    CANOPUS_RESOURCE_HEAP,
    CANOPUS_RESOURCE_CALLBACK_TABLE,
    CANOPUS_RESOURCE_TIMER,
    CANOPUS_RESOURCE_WORKER,
    CANOPUS_RESOURCE_SERVICE,
    CANOPUS_RESOURCE_PROTOCOL,
    CANOPUS_RESOURCE_FD,
    CANOPUS_RESOURCE_HOOK,
    CANOPUS_RESOURCE_OPEN_REF,
    CANOPUS_RESOURCE_INFLIGHT_CALLBACK,
};

enum canopus_resource_state {
    CANOPUS_RES_ACTIVE = 1,
    CANOPUS_RES_DRAINING,
    CANOPUS_RES_DETACHED,
    CANOPUS_RES_RELEASED,
    CANOPUS_RES_RETAINED_UNTIL_REBOOT,
};

struct canopus_resource_v1 {
    uint32_t kind;
    uint32_t state;
    uint32_t generation; /* bumped on every state change */
    void *handle;
    void (*on_release)(struct canopus_resource_v1 *res);
};

struct canopus_resource_tracker_v1 {
    uint32_t count;
    uint32_t generation;
    struct canopus_resource_v1 slots[CANOPUS_RESOURCE_MAX];
};

void canopus_tracker_init(struct canopus_resource_tracker_v1 *t);
/* Adds a resource (copies the slot). Fails if the table is full or the
 * resource is already tracked by handle. */
int canopus_tracker_add(struct canopus_resource_tracker_v1 *t,
                        const struct canopus_resource_v1 *res);
/* Marks ACTIVE -> DRAINING. */
int canopus_tracker_drain(struct canopus_resource_tracker_v1 *t, void *handle);
/* Marks namespace unlinked; must not be retried. */
int canopus_tracker_detach(struct canopus_resource_tracker_v1 *t, void *handle);
/* Runs on_release and marks RELEASED. Double release fails. */
int canopus_tracker_release(struct canopus_resource_tracker_v1 *t, void *handle);
/* Marks RETAINED_UNTIL_REBOOT (not releasable this boot). */
int canopus_tracker_retain_until_reboot(struct canopus_resource_tracker_v1 *t,
                                        void *handle);
/* Releases everything still releasable (rollback on init failure). */
void canopus_tracker_release_all(struct canopus_resource_tracker_v1 *t);
uint32_t canopus_tracker_generation(const struct canopus_resource_tracker_v1 *t);

/* ================================================================== */
/* Callback generation guards                                          */
/*                                                                     */
/* A retained callback captures the generation at registration. When it */
/* fires, a mismatched generation makes it a harmless no-op.            */
/* ================================================================== */

struct canopus_generation_v1 {
    uint32_t value;
};

void canopus_generation_init(struct canopus_generation_v1 *g);
void canopus_generation_bump(struct canopus_generation_v1 *g);
uint32_t canopus_generation_get(const struct canopus_generation_v1 *g);
/* Returns 1 when the callback's captured generation is still current. */
int canopus_generation_valid(const struct canopus_generation_v1 *g,
                             uint32_t captured);

/* ================================================================== */
/* Diagnostics / event writer                                          */
/*                                                                     */
/* Bounded append-only record stream. When full, oldest entries are     */
/* dropped and `dropped` counts them (never blocks).                    */
/* ================================================================== */

#define CANOPUS_EVENT_LOG_ENTRIES 16u

struct canopus_event_v1 {
    uint32_t sequence;     /* monotonic, wraps at UINT32_MAX */
    uint32_t boot_id;      /* from identity guard */
    uint32_t module_gen;   /* module generation */
    uint32_t state_before;
    uint32_t state_after;
    uint32_t result;
    uint32_t flags;
};

struct canopus_event_log_v1 {
    uint32_t head;                 /* next write index */
    uint32_t stored_count;         /* entries currently stored (<= ENTRIES) */
    uint32_t next_sequence;        /* sequence to assign next (monotonic) */
    uint32_t dropped;              /* saturating count of overwritten entries */
    struct canopus_event_v1 entries[CANOPUS_EVENT_LOG_ENTRIES];
};

void canopus_event_log_init(struct canopus_event_log_v1 *log, uint32_t boot_id);
/* Appends an event, returning the assigned sequence number. When the ring
 * is full the oldest entry is overwritten and `dropped` saturating-
 * increments; the writer never blocks. */
uint32_t canopus_event_log_append(struct canopus_event_log_v1 *log,
                                  uint32_t module_gen,
                                  uint32_t state_before,
                                  uint32_t state_after,
                                  uint32_t result);
/* Number of entries currently stored (bounded by CANOPUS_EVENT_LOG_ENTRIES). */
uint32_t canopus_event_log_count(const struct canopus_event_log_v1 *log);
/* Next sequence number to be assigned (monotonic; total appended so far). */
uint32_t canopus_event_log_next_sequence(const struct canopus_event_log_v1 *log);
/* Saturated count of entries evicted by overwrite (0 before the ring fills). */
uint32_t canopus_event_log_dropped(const struct canopus_event_log_v1 *log);
/* Non-zero when `candidate` is not the immediate successor of `after` in
 * the monotonic sequence (modular, so a wrap at UINT32_MAX is not a gap).
 * Readers use this to detect eviction/drop between two reads. */
int canopus_event_log_is_gap(uint32_t after, uint32_t candidate);

/* ================================================================== */
/* Bounded string/buffer helper                                        */
/* ================================================================== */

/* Copies src to dst (nul-terminated) bounded by capacity. Returns the
 * number of bytes copied excluding the NUL; truncates and returns -1 if
 * src does not fit. */
int canopus_buf_copy(char *dst, uint32_t capacity, const char *src);

/* ================================================================== */
/* Bounded text writer (CAN-P0-002)                                    */
/*                                                                     */
/* NUL-terminated, truncation-aware append-only writer. Every append    */
/* uses at most `cap - used` bytes and the buffer is always left NUL-   */
/* terminated. A truncated append returns CANOPUS_TEXT_TRUNCATED, pins  */
/* `used` at cap-1 and puts the writer in a failed state; it never      */
/* reports success for a partial record and never lets the offset go    */
/* negative or past the end. Renderers must surface truncation to the   */
/* caller instead of treating the buffer as a complete record.          */
/* ================================================================== */

#define CANOPUS_TEXT_TRUNCATED (-2)

struct canopus_text_writer_v1 {
    char *buf;
    uint32_t cap;        /* capacity in bytes, including the NUL */
    uint32_t used;       /* bytes used, excluding the NUL */
    uint32_t truncated;  /* 1 once any append ran out of room */
};

/* Initializes the writer. Requires w/buf non-NULL and cap > 0; writes
 * buf[0] = '\0' immediately. Returns 0 on success, -1 on invalid input. */
int canopus_text_writer_init(struct canopus_text_writer_v1 *w,
                             char *buf, uint32_t cap);
/* Appends a NUL-terminated string. Returns 0 when fully appended,
 * CANOPUS_TEXT_TRUNCATED when the source does not fit (buffer is left
 * NUL-terminated at cap-1 and the writer enters the truncated state), or
 * -1 on invalid arguments. Once truncated, further appends are no-ops
 * returning CANOPUS_TEXT_TRUNCATED. */
int canopus_text_writer_append(struct canopus_text_writer_v1 *w,
                               const char *s);

/* ================================================================== */
/* Module descriptor validation                                        */
/* ================================================================== */

/* Validates descriptor header + ABI version. Returns 0 when well-formed. */
int canopus_module_descriptor_check(const struct canopus_module_descriptor_v1 *d);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_RUNTIME_H */
