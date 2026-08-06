/*
 * canopus_manager.h — Manager UI model, pages and operations (CAN-UI-001..004).
 *
 * Host-testable view/controller layer for the device Manager:
 *   - device page   (identity / target / framework / firmware metadata)
 *   - module list   (id + lifecycle state, one line each)
 *   - module detail (lifecycle, class, capability, signature, risk, the
 *                    operations that are ACTUALLY available for that class)
 *
 * Operations go through the versioned supervisor protocol (canopus_protocol)
 * over a caller-supplied transport. Operation availability is lifecycle-
 * aware: a resident/always/patch module has NO unload path, so the UI never
 * offers a fake disable/remove for it — only next-boot/reboot semantics
 * (CAN-UI-004).
 */
#ifndef CANOPUS_MANAGER_H
#define CANOPUS_MANAGER_H

#include <stdint.h>
#include "canopus_abi.h"
#include "canopus_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_MANAGER_MAX_MODULES 16u
#define CANOPUS_MANAGER_MODULE_ID_MAX 32u

/* Views. */
enum canopus_manager_view {
    CANOPUS_MANAGER_VIEW_DEVICE = 1,
    CANOPUS_MANAGER_VIEW_MODULE_LIST,
    CANOPUS_MANAGER_VIEW_MODULE_DETAIL,
};

/* Risk labels for the module detail page. */
enum canopus_manager_risk {
    CANOPUS_MANAGER_RISK_HARMLESS = 0,
    CANOPUS_MANAGER_RISK_MODERATE,
    CANOPUS_MANAGER_RISK_RESIDENT_CRITICAL,
};

struct canopus_manager_module_v1 {
    char module_id[CANOPUS_MANAGER_MODULE_ID_MAX];
    uint32_t lifecycle_class; /* CANOPUS_LIFECYCLE_* */
    uint32_t state;           /* CANOPUS_STATE_* */
    uint32_t flags;           /* CANOPUS_FLAG_* */
    uint32_t version;
    uint32_t signature_ok;    /* 1 = signature verified, 0 = unsigned/dev */
    uint32_t risk;
    uint32_t has_previous;    /* rollback target exists */
};

struct canopus_manager_model_v1 {
    /* device identity (from QUERY_DEVICE) */
    char target_id[CANOPUS_MANAGER_MODULE_ID_MAX];
    char firmware_version[16];
    char firmware_build[48];
    uint32_t framework_revision;

    struct canopus_manager_module_v1 modules[CANOPUS_MANAGER_MAX_MODULES];
    uint32_t module_count;
    uint32_t selected;        /* index into modules */
    uint32_t view;            /* canopus_manager_view */
    uint32_t safe_mode;       /* 1 = safe mode requested/active */

    uint32_t pending_op;      /* last canopus_command issued, 0 = none */
    uint32_t pending_state;   /* CANOPUS_RESULT_* of pending_op */
    /* CAN-P1-002: the client's monotonic request-id source. Starts at 1,
     * increments on every command, never 0, wraps without reusing 0. */
    uint32_t next_request_id;

    /* transport: send a request envelope plus an opaque payload (may be
     * NULL when payload_size == 0) and await a response. Return 0 on
     * success, -1 on transport failure. */
    int (*transport)(const struct canopus_proto_request_v1 *req,
                     const void *payload,
                     struct canopus_proto_response_v1 *resp, void *cookie);
    void *transport_cookie;
};

/* ---- model ------------------------------------------------------ */

/* Zeroes the model; caller supplies transport. */
void canopus_manager_init(struct canopus_manager_model_v1 *m,
                          int (*transport)(const struct canopus_proto_request_v1 *,
                                           const void *,
                                           struct canopus_proto_response_v1 *,
                                           void *),
                          void *cookie);
/* Sets device identity fields. */
void canopus_manager_set_identity(struct canopus_manager_model_v1 *m,
                                  const char *target_id,
                                  const char *fw_version,
                                  const char *fw_build,
                                  uint32_t framework_revision);
/* Adds/replaces a module record by id. Returns index or -1 when full. */
int canopus_manager_upsert_module(struct canopus_manager_model_v1 *m,
                                  const struct canopus_manager_module_v1 *mod);
/* Navigation. Returns 0 on success. */
int canopus_manager_goto(struct canopus_manager_model_v1 *m, uint32_t view,
                         uint32_t selected);

/* ---- pages (render into a caller buffer; return 0 on success) ---- */

int canopus_manager_render_device(const struct canopus_manager_model_v1 *m,
                                  char *out, uint32_t cap);
int canopus_manager_render_module_list(const struct canopus_manager_model_v1 *m,
                                       char *out, uint32_t cap);
int canopus_manager_render_module_detail(const struct canopus_manager_model_v1 *m,
                                         char *out, uint32_t cap);

/* ---- operations ------------------------------------------------- */
/* Each op returns a CANOPUS_RESULT_* state. DISALLOWED means the module's
 * lifecycle class forbids the operation for this module. The operation is
 * only sent over the transport when it is allowed. */

uint32_t canopus_manager_op_install(struct canopus_manager_model_v1 *m,
                                    const char *package_ref);
uint32_t canopus_manager_op_enable(struct canopus_manager_model_v1 *m,
                                   uint32_t index);
uint32_t canopus_manager_op_disable(struct canopus_manager_model_v1 *m,
                                    uint32_t index);
uint32_t canopus_manager_op_remove(struct canopus_manager_model_v1 *m,
                                   uint32_t index);
uint32_t canopus_manager_op_update(struct canopus_manager_model_v1 *m,
                                   uint32_t index);
uint32_t canopus_manager_op_rollback(struct canopus_manager_model_v1 *m,
                                     uint32_t index);
uint32_t canopus_manager_op_safe_mode(struct canopus_manager_model_v1 *m);

/* ---- availability helpers (for the detail page) ------------------ */
/* Returns non-zero when the operation is available for the module. */
int canopus_manager_can_enable(const struct canopus_manager_model_v1 *m,
                               uint32_t index);
int canopus_manager_can_disable(const struct canopus_manager_model_v1 *m,
                                uint32_t index);
int canopus_manager_can_remove(const struct canopus_manager_model_v1 *m,
                               uint32_t index);
int canopus_manager_can_update(const struct canopus_manager_model_v1 *m,
                               uint32_t index);
int canopus_manager_can_rollback(const struct canopus_manager_model_v1 *m,
                                 uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_H */
