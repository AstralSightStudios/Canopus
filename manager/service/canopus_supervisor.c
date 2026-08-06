/* canopus_supervisor.c — device-side supervisor (Phase 5) implementation.
 *
 * Host-testable: the command/status ABI and module-slot lifecycle semantics
 * live here; the platform hooks (device registration, actual load/unload,
 * package staging) are behind canopus_supervisor_platform.h.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

static uint32_t sup_result_state(int rc)
{
    if (rc < 0) {
        return CANOPUS_RESULT_FAILED;
    }
    return CANOPUS_RESULT_COMPLETED;
}

static int module_lifecycle_ok(uint32_t lifecycle_class)
{
    return lifecycle_class <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED;
}

/* CAN-P0-005: a module with running code (as opposed to INSTALLED/DISABLED
 * where nothing is resident) needs a real stop/drain/unload to disable. */
static int sup_module_running(uint32_t state)
{
    return state == CANOPUS_STATE_ACTIVE || state == CANOPUS_STATE_READY ||
           state == CANOPUS_STATE_ENABLED || state == CANOPUS_STATE_BOOT_RESIDENT;
}

/* Removable disable: reject-new-work barrier -> drain tracked resources ->
 * never unload while refs/retained resources exist -> platform unload.
 * Only on a successful unload does the slot become UNLOADED; a failure
 * keeps the real loaded state so the UI never reports DISABLED for code
 * that is still resident. */
static uint32_t sup_disable_removable(struct canopus_supervisor_v1 *sup, int slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    uint32_t was_state = m->state;
    uint32_t retained = 0;
    uint32_t i;
    uint32_t rc;

    if (sup->platform && sup->platform->deactivate) {
        (void)sup->platform->deactivate(sup->platform_cookie, (uint32_t)slot);
    }
    if (sup->platform && sup->platform->stop) {
        (void)sup->platform->stop(sup->platform_cookie, (uint32_t)slot);
    }
    /* reject-new-work barrier + drain */
    m->state = CANOPUS_STATE_STOPPING;
    m->flags |= CANOPUS_SUP_FLAG_DISABLING;
    if (m->tracker != 0) {
        canopus_tracker_release_all(m->tracker);
        for (i = 0; i < CANOPUS_RESOURCE_MAX; i++) {
            if (m->tracker->slots[i].state == CANOPUS_RES_RETAINED_UNTIL_REBOOT) {
                retained++;
            }
        }
    }
    if (retained > 0 || m->open_refs > 0) {
        /* retained/detached resources or open refs: never unload, fail-stop */
        m->state = CANOPUS_STATE_FAIL_STOP;
        m->flags &= ~CANOPUS_SUP_FLAG_DISABLING;
        sup->error_code = CANOPUS_SUP_ERR_BUSY;
        return CANOPUS_RESULT_REBOOT_REQUIRED;
    }
    if (sup->platform && sup->platform->unload_module) {
        rc = sup_result_state(sup->platform->unload_module(
            sup->platform_cookie, (uint32_t)slot));
    } else {
        rc = CANOPUS_RESULT_FAILED;
    }
    if (rc == CANOPUS_RESULT_COMPLETED) {
        m->state = CANOPUS_STATE_UNLOADED;
        m->flags &= ~CANOPUS_SUP_FLAG_DISABLING;
        m->flags &= ~CANOPUS_SUP_FLAG_SIGNATURE_OK;
        return CANOPUS_RESULT_COMPLETED;
    }
    /* unload failed: keep the real loaded state, never DISABLED/UNLOADED */
    m->state = was_state;
    m->flags &= ~CANOPUS_SUP_FLAG_DISABLING;
    sup->error_code = CANOPUS_SUP_ERR_UNLOAD;
    return CANOPUS_RESULT_FAILED;
}

/* CAN-P1-007: reclaim a slot after a successful REMOVE. */
static void sup_clear_slot(struct canopus_supervisor_v1 *sup, int slot)
{
    if (sup->modules[slot].state != 0) {
        sup->module_count--;
    }
    canopus_memset(&sup->modules[slot], 0, sizeof(sup->modules[slot]));
}

int canopus_supervisor_init(struct canopus_supervisor_v1 *sup,
                            uint32_t framework_revision,
                            const struct canopus_sup_platform_v1 *platform,
                            void *cookie)
{
    if (sup == 0) {
        return -1; /* CAN-P2-001: never dereference a NULL supervisor */
    }
    canopus_memset(sup, 0, sizeof(*sup));
    sup->abi = CANOPUS_SUP_ABI;
    sup->framework_revision = framework_revision;
    sup->platform = platform;
    sup->platform_cookie = cookie;
    sup->selected = -1;
    sup->last_kind = 0; /* default: legacy CPS1 status read */
    canopus_pending_init(&sup->pending);
    return 0;
}

int canopus_supervisor_validate_command(const uint8_t command[CANOPUS_SUP_COMMAND_SIZE])
{
    uint32_t magic;
    if (command == 0) {
        return -1;
    }
    magic = (uint32_t)command[0] | ((uint32_t)command[1] << 8) |
            ((uint32_t)command[2] << 16) | ((uint32_t)command[3] << 24);
    if (magic != CANOPUS_SUP_CMD_MAGIC) {
        return -1;
    }
    return 0;
}

static uint32_t cmd_word(const uint8_t command[CANOPUS_SUP_COMMAND_SIZE],
                         unsigned int idx)
{
    unsigned int o = 4 + idx * 4;
    return (uint32_t)command[o] | ((uint32_t)command[o + 1] << 8) |
           ((uint32_t)command[o + 2] << 16) | ((uint32_t)command[o + 3] << 24);
}

/* Core opcode dispatch shared by the legacy CPC1 path and the v2 path
 * (CAN-P0-008). `stage_arg` is the optional package path for INSTALL (v2
 * passes the request payload; the legacy path passes 0). */
/* CAN-P0-006: safe-mode command policy. Only read/diagnostic and next-boot
 * operations are allowed: QUERY, ROLLBACK, ENTER_SAFE_MODE, and for resident
 * classes DISABLE/REMOVE (which are next-boot only anyway). INSTALL, ENABLE,
 * UPDATE, and any immediate removable unload are rejected. */
static int sup_safe_mode_allows(const struct canopus_supervisor_v1 *sup,
                                uint32_t op, int slot)
{
    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
    case CANOPUS_SUP_CMD_ROLLBACK:
    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
        return 1;
    case CANOPUS_SUP_CMD_INSTALL:
    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_UPDATE:
        return 0; /* activation is never allowed in safe mode */
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
        /* only the next-boot (resident) semantics are read-only enough */
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0) {
            return 0;
        }
        return sup->modules[slot].lifecycle_class != CANOPUS_LIFECYCLE_REMOVABLE;
    default:
        return 0;
    }
}

static uint32_t sup_dispatch(struct canopus_supervisor_v1 *sup, uint32_t op,
                             uint32_t arg0, uint32_t arg1,
                             const char *stage_arg)
{
    uint32_t rc = CANOPUS_RESULT_REJECTED;
    int slot;

    (void)arg1;

    if (sup->safe_mode && !sup_safe_mode_allows(sup, op, (int)arg0)) {
        sup->error_code = CANOPUS_SUP_ERR_SAFE_MODE;
        return CANOPUS_RESULT_DISALLOWED;
    }
    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
        rc = CANOPUS_RESULT_COMPLETED;
        break;

    case CANOPUS_SUP_CMD_INSTALL: {
        if (sup->platform && sup->platform->stage_package) {
            rc = sup->platform->stage_package(sup->platform_cookie, stage_arg) == 0
                     ? CANOPUS_RESULT_COMPLETED
                     : CANOPUS_RESULT_FAILED;
        } else {
            rc = CANOPUS_RESULT_FAILED;
        }
        if (rc == CANOPUS_RESULT_FAILED) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE;
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
    case CANOPUS_SUP_CMD_UPDATE:
    case CANOPUS_SUP_CMD_ROLLBACK: {
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0) {
            rc = CANOPUS_RESULT_DISALLOWED;
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            break;
        }
        if (op == CANOPUS_SUP_CMD_ENABLE ||
            op == CANOPUS_SUP_CMD_DISABLE ||
            op == CANOPUS_SUP_CMD_REMOVE) {
            /* Lifecycle-aware: a resident class has no unload path, so only
             * removable classes get real disable/remove. Next-boot semantics
             * are reported as REBOOT_REQUIRED for resident classes. */
            if (sup->modules[slot].lifecycle_class != CANOPUS_LIFECYCLE_REMOVABLE) {
                if (op == CANOPUS_SUP_CMD_REMOVE) {
                    sup->modules[slot].state = CANOPUS_STATE_REMOVE_PENDING;
                    rc = CANOPUS_RESULT_REBOOT_REQUIRED;
                } else {
                    sup->modules[slot].state = CANOPUS_STATE_DISABLED_NEXT_BOOT;
                    rc = CANOPUS_RESULT_REBOOT_REQUIRED;
                }
                break;
            }
        }
        if (op == CANOPUS_SUP_CMD_REMOVE) {
            /* CAN-P0-005 point 12: REMOVE reuses the same disable
             * transaction (stop/drain/unload), then reclaims the slot. */
            rc = sup_module_running(sup->modules[slot].state)
                     ? sup_disable_removable(sup, slot)
                     : CANOPUS_RESULT_COMPLETED;
            if (rc == CANOPUS_RESULT_COMPLETED) {
                sup_clear_slot(sup, slot);
            }
        } else if (op == CANOPUS_SUP_CMD_DISABLE) {
            /* CAN-P0-005: a removable disable really stops, drains and
             * unloads; it never reports DISABLED while the code is still
             * resident or while refs/retained resources exist. */
            if (sup_module_running(sup->modules[slot].state)) {
                rc = sup_disable_removable(sup, slot);
            } else {
                sup->modules[slot].state = CANOPUS_STATE_DISABLED;
                rc = CANOPUS_RESULT_COMPLETED;
            }
        } else if (op == CANOPUS_SUP_CMD_ENABLE) {
            if (sup->platform && sup->platform->load_module) {
                int st = sup->platform->load_module(
                    sup->platform_cookie, (uint32_t)slot, 0,
                    sup->modules[slot].lifecycle_class);
                rc = sup_result_state(st);
                if (rc == CANOPUS_RESULT_COMPLETED) {
                    sup->modules[slot].state =
                        (uint32_t)(st < 0 ? CANOPUS_STATE_FAILED : st);
                }
            } else {
                rc = CANOPUS_RESULT_FAILED;
            }
            if (rc == CANOPUS_RESULT_FAILED) {
                sup->error_code = CANOPUS_SUP_ERR_LOAD;
            }
        } else { /* UPDATE / ROLLBACK */
            sup->modules[slot].state = CANOPUS_STATE_UPDATE_STAGED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
        sup->safe_mode = 1u;
        sup->safe_mode_reason = CANOPUS_SAFE_MODE_USER_REQUESTED;
        rc = CANOPUS_RESULT_COMPLETED;
        break;

    default:
        rc = CANOPUS_RESULT_REJECTED;
        sup->error_code = CANOPUS_SUP_ERR_UNKNOWN_OP;
        break;
    }
    return rc;
}

/* ---- CAN-P0-006: boot markers -------------------------------------- */

void canopus_supervisor_boot_begin(struct canopus_supervisor_v1 *sup,
                                   uint32_t boot_id)
{
    if (sup == 0) {
        return;
    }
    sup->boot_state = CANOPUS_BOOT_BOOTING;
    sup->safe_mode_boot_id = boot_id;
}

void canopus_supervisor_boot_ok(struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return;
    }
    sup->boot_state = CANOPUS_BOOT_OK;
}

int canopus_supervisor_boot_should_safe_mode(
    const struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return 1; /* a missing supervisor is never a healthy boot */
    }
    if (sup->boot_state == CANOPUS_BOOT_BOOTING) {
        return 1; /* previous boot never committed BOOT_OK */
    }
    if (sup->crash_counter >= CANOPUS_SUP_CRASH_THRESHOLD) {
        return 1;
    }
    return 0;
}

/* CAN-P2-016: saturating crash counter — a long crash loop reports the
 * threshold, never a wrapped counter that silently looks healthy. */
void canopus_supervisor_record_crash(struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return;
    }
    if (sup->crash_counter < 0xFFFFFFFFu) {
        sup->crash_counter++;
    }
}

uint32_t canopus_supervisor_handle_command(struct canopus_supervisor_v1 *sup,
                                           const uint8_t command[CANOPUS_SUP_COMMAND_SIZE])
{
    uint32_t op, arg0, arg1;
    uint32_t rc;

    if (sup == 0) {
        return CANOPUS_RESULT_REJECTED;
    }
    if (canopus_supervisor_validate_command(command) != 0) {
        canopus_snapshot_begin(&sup->snap);
        sup->pending_op = 0;
        sup->pending_state = CANOPUS_RESULT_REJECTED;
        sup->error_code = CANOPUS_SUP_ERR_BAD_COMMAND;
        canopus_snapshot_commit(&sup->snap);
        return sup->pending_state;
    }
    op = cmd_word(command, 0);
    arg0 = cmd_word(command, 1);
    arg1 = cmd_word(command, 2);

    /* CAN-P1-003: all state mutation runs under the snapshot protocol so a
     * reader never observes a torn record (sequence odd or begin != end). */
    canopus_snapshot_begin(&sup->snap);
    sup->pending_op = op;
    sup->selected = (int32_t)arg0;
    /* CAN-P1-008: clear this command's error at the start; failure paths
     * set a stable code and it is never cleared again at the tail. */
    sup->error_code = CANOPUS_SUP_ERR_NONE;
    rc = sup_dispatch(sup, op, arg0, arg1, 0);
    sup->pending_state = rc;
    canopus_snapshot_commit(&sup->snap);
    return rc;
}

/* ---- CAN-P0-008: v2 transport handling ----------------------------- */

static uint32_t v2_to_sup_cmd(uint32_t cmd)
{
    switch (cmd) {
    case CANOPUS_CMD_ECHO:
    case CANOPUS_CMD_QUERY_MODULE:
    case CANOPUS_CMD_QUERY_DEVICE:
        return CANOPUS_SUP_CMD_QUERY;
    case CANOPUS_CMD_INSTALL:
        return CANOPUS_SUP_CMD_INSTALL;
    case CANOPUS_CMD_ENABLE:
        return CANOPUS_SUP_CMD_ENABLE;
    case CANOPUS_CMD_DISABLE:
        return CANOPUS_SUP_CMD_DISABLE;
    case CANOPUS_CMD_REMOVE:
        return CANOPUS_SUP_CMD_REMOVE;
    case CANOPUS_CMD_UPDATE:
        return CANOPUS_SUP_CMD_UPDATE;
    case CANOPUS_CMD_ROLLBACK:
        return CANOPUS_SUP_CMD_ROLLBACK;
    case CANOPUS_CMD_ENTER_SAFE_MODE:
        return CANOPUS_SUP_CMD_ENTER_SAFE_MODE;
    default:
        return 0; /* unknown command */
    }
}

static int v2_op_needs_slot(uint32_t cmd)
{
    return cmd == CANOPUS_CMD_ENABLE || cmd == CANOPUS_CMD_DISABLE ||
           cmd == CANOPUS_CMD_REMOVE || cmd == CANOPUS_CMD_UPDATE ||
           cmd == CANOPUS_CMD_ROLLBACK;
}

static uint32_t wire_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* NUL-aware bounded compare of a slot's stored module id against a request
 * payload (the Manager pads to CANOPUS_SUP_MODULE_ID_MAX with NULs). */
static int sup_id_eq(const uint8_t *a, const uint8_t *b, uint32_t b_len)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_SUP_MODULE_ID_MAX; i++) {
        uint8_t av = a[i];
        uint8_t bv = (i < b_len) ? b[i] : 0u;
        if (av != bv) {
            return 0;
        }
        if (av == 0) {
            return 1;
        }
    }
    return 0; /* stored id not NUL-terminated within the window */
}

static int sup_slot_from_module_id(const struct canopus_supervisor_v1 *sup,
                                   const void *payload, uint32_t payload_len)
{
    uint32_t i;
    if (payload == 0 || payload_len == 0 || payload_len > CANOPUS_SUP_MODULE_ID_MAX) {
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state != 0 &&
            sup_id_eq(sup->modules[i].module_id, (const uint8_t *)payload,
                      payload_len)) {
            return (int)i;
        }
    }
    return -1;
}

/* CAN-P0-003: an INSTALL stage token is a bounded basename, never an
 * arbitrary path. Only [a-z0-9._-], NUL-terminated within the bound, and
 * never ".", ".." or a leading/trailing "."/"-" (a "." component could
 * escape a future open-at-root resolution). */
static int sup_stage_token_ok(const void *payload, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)payload;
    uint32_t i, n = 0;
    if (payload == 0 || len == 0 || len > CANOPUS_SUP_STAGE_TOKEN_MAX) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (p[i] == 0) {
            n = i;
            break;
        }
    }
    if (i == len) {
        return 0; /* no NUL within the bound */
    }
    if (n == 0) {
        return 0; /* empty token */
    }
    /* "." and ".." are traversal components, never valid basenames */
    if ((n == 1 && p[0] == '.') ||
        (n == 2 && p[0] == '.' && p[1] == '.')) {
        return 0;
    }
    /* leading/trailing separators and hidden names are not valid boundaries */
    if (p[0] == '.' || p[0] == '-' || p[n - 1] == '.' || p[n - 1] == '-') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

int canopus_supervisor_handle_v2_request(struct canopus_supervisor_v1 *sup,
                                         const struct canopus_proto_request_v1 *req,
                                         const void *payload,
                                         struct canopus_proto_response_v1 *resp,
                                         uint32_t *opcode)
{
    uint32_t op, slot_arg;
    uint32_t rc;
    int track_request;

    if (sup == 0 || req == 0 || resp == 0 || opcode == 0) {
        return -1;
    }
    *opcode = req->command;
    op = v2_to_sup_cmd(req->command);
    if (op == 0) {
        canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_REJECTED, 0);
        return 0;
    }
    /* CAN-P0-003: INSTALL must carry a bounded stage token, never a path */
    if (req->command == CANOPUS_CMD_INSTALL &&
        !sup_stage_token_ok(payload, req->payload_size)) {
        canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_DISALLOWED, 0);
        return 0;
    }
    if (req->command == CANOPUS_CMD_QUERY_MODULE) {
        if (payload == 0 || req->payload_size != 4u) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
        slot_arg = wire_u32((const uint8_t *)payload);
        if (slot_arg >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot_arg].state == 0u) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
    } else if (v2_op_needs_slot(req->command)) {
        int found = sup_slot_from_module_id(sup, payload, req->payload_size);
        if (found < 0) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
        slot_arg = (uint32_t)found;
    } else {
        slot_arg = 0;
    }
    /* Read-only queries complete synchronously and do not consume retained
     * pending slots. Mutations remain observable until an explicit ACK path is
     * negotiated. */
    track_request = req->command != CANOPUS_CMD_ECHO &&
                    req->command != CANOPUS_CMD_QUERY_DEVICE &&
                    req->command != CANOPUS_CMD_QUERY_MODULE;
    if (track_request &&
        canopus_pending_accept(&sup->pending, req->request_id,
                               req->command) != 0) {
        canopus_proto_response_init(resp, req->request_id,
                                    CANOPUS_RESULT_REJECTED, 0);
        return 0;
    }
    /* run under the snapshot protocol; INSTALL receives the request payload
     * as the package path, other ops use the resolved slot */
    canopus_snapshot_begin(&sup->snap);
    sup->pending_op = op;
    sup->selected = (int32_t)slot_arg;
    sup->error_code = CANOPUS_SUP_ERR_NONE;
    rc = sup_dispatch(sup, op, slot_arg, 0,
                      req->command == CANOPUS_CMD_INSTALL
                          ? (const char *)payload : 0);
    sup->pending_state = rc;
    canopus_snapshot_commit(&sup->snap);
    if (track_request) {
        canopus_pending_set_error(&sup->pending, req->request_id,
                                  sup->error_code);
        (void)canopus_pending_finish(&sup->pending, req->request_id, rc);
    }
    canopus_proto_response_init(resp, req->request_id, rc, 0);
    return 0;
}

static void put_wire_u32(uint8_t *out, uint32_t offset, uint32_t value)
{
    out[offset] = (uint8_t)value;
    out[offset + 1u] = (uint8_t)(value >> 8);
    out[offset + 2u] = (uint8_t)(value >> 16);
    out[offset + 3u] = (uint8_t)(value >> 24);
}

static uint32_t render_v2_query_payload(
    const struct canopus_supervisor_v1 *sup,
    const struct canopus_proto_request_v1 *req,
    const struct canopus_proto_response_v1 *resp,
    uint8_t out[CANOPUS_PROTO_MAX_PAYLOAD])
{
    if (resp->result_state != CANOPUS_RESULT_COMPLETED) {
        return 0u;
    }
    if (req->command == CANOPUS_CMD_QUERY_DEVICE) {
        canopus_memset(out, 0, CANOPUS_QUERY_DEVICE_SIZE);
        put_wire_u32(out, 0u, CANOPUS_QUERY_DEVICE_MAGIC);
        put_wire_u32(out, 4u, CANOPUS_QUERY_DEVICE_SIZE);
        put_wire_u32(out, 8u, sup->framework_revision);
        put_wire_u32(out, 12u, sup->safe_mode);
        put_wire_u32(out, 16u, sup->module_count);
        put_wire_u32(out, 20u, sup->safe_mode_reason);
        put_wire_u32(out, 24u, (uint32_t)sup->error_code);
        put_wire_u32(out, 28u, sup->flags);
        return CANOPUS_QUERY_DEVICE_SIZE;
    }
    if (req->command == CANOPUS_CMD_QUERY_MODULE &&
        sup->selected >= 0 &&
        (uint32_t)sup->selected < CANOPUS_SUP_MODULE_SLOTS) {
        const struct canopus_sup_module_v1 *module =
            &sup->modules[(uint32_t)sup->selected];
        canopus_memset(out, 0, CANOPUS_QUERY_MODULE_SIZE);
        put_wire_u32(out, 0u, CANOPUS_QUERY_MODULE_MAGIC);
        put_wire_u32(out, 4u, CANOPUS_QUERY_MODULE_SIZE);
        put_wire_u32(out, 8u, (uint32_t)sup->selected);
        put_wire_u32(out, 12u, module->state);
        put_wire_u32(out, 16u, module->lifecycle_class);
        put_wire_u32(out, 20u, module->version);
        put_wire_u32(out, 24u, module->flags);
        canopus_memcpy(out + 28u, module->module_id,
                       CANOPUS_SUP_MODULE_ID_MAX);
        return CANOPUS_QUERY_MODULE_SIZE;
    }
    return 0u;
}

static uint32_t status_word(const uint8_t *b, unsigned int o)
{
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
           ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

int canopus_supervisor_render_status(const struct canopus_supervisor_v1 *sup,
                                     uint8_t out[CANOPUS_SUP_STATUS_SIZE])
{
    uint32_t i;
    uint32_t seq_begin;
    if (sup == 0 || out == 0) {
        return -1;
    }
    canopus_memset(out, 0, CANOPUS_SUP_STATUS_SIZE);
#define PUT32(o, v) \
    do { \
        uint32_t _v = (uint32_t)(v); \
        out[o] = (uint8_t)(_v & 0xff); \
        out[(o) + 1] = (uint8_t)((_v >> 8) & 0xff); \
        out[(o) + 2] = (uint8_t)((_v >> 16) & 0xff); \
        out[(o) + 3] = (uint8_t)((_v >> 24) & 0xff); \
    } while (0)
    PUT32(0, CANOPUS_SUP_STATUS_MAGIC);
    PUT32(4, sup->abi);
    PUT32(8, sup->framework_revision);
    PUT32(12, sup->safe_mode);
    PUT32(16, sup->module_count);
    PUT32(20, sup->pending_op);
    PUT32(24, sup->pending_state);
    PUT32(28, sup->flags);
    PUT32(32, sup->error_code);
    /* CAN-P1-003: snapshot begin read once; the end is re-read after the
     * payload so a concurrent mutation leaves begin != end (or odd). */
    seq_begin = sup->snap.sequence;
    PUT32(CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF, seq_begin);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        uint32_t o = 128u + i * CANOPUS_SUP_MODULE_SLOT_STRIDE;
        const struct canopus_sup_module_v1 *m = &sup->modules[i];
        PUT32(o + 0, m->state);
        PUT32(o + 4, m->lifecycle_class);
        PUT32(o + 8, m->version);
        PUT32(o + 12, m->flags);
    }
    PUT32(CANOPUS_SUP_STATUS_SEQ_END_OFF, sup->snap.sequence);
#undef PUT32
    return 0;
}

int32_t canopus_supervisor_device_read(struct canopus_supervisor_v1 *sup,
                                       void *buffer, uint32_t count)
{
    uint8_t staging[CANOPUS_SUP_STATUS_SIZE];
    uint32_t attempt, begin, end;
    if (sup == 0 || buffer == 0) {
        return -1;
    }
    /* CAN-P0-008: after a v2 write, the read returns the v2 response record
     * (echoing request id / result); otherwise the legacy 384-byte CPS1
     * status. The two are never length-guessed. */
    if (sup->last_kind == 1) {
        if (count < sup->v2_response_len || sup->v2_response_len == 0) {
            return -1;
        }
        canopus_memcpy(buffer, sup->v2_response_buf, sup->v2_response_len);
        return (int32_t)sup->v2_response_len;
    }
    if (count < CANOPUS_SUP_STATUS_SIZE) {
        return -1;
    }
    /* CAN-P1-003: interrupt-safe copy via a staging buffer. Accept the
     * record only when begin == end and even; retry a bounded number of
     * times and otherwise report an error — never publish a torn record. */
    for (attempt = 0; attempt < CANOPUS_SUP_STATUS_RETRIES; attempt++) {
        if (canopus_supervisor_render_status(sup, staging) != 0) {
            return -1;
        }
        begin = status_word(staging, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF);
        end = status_word(staging, CANOPUS_SUP_STATUS_SEQ_END_OFF);
        if ((begin & 1u) == 0u && begin == end) {
            canopus_memcpy(buffer, staging, CANOPUS_SUP_STATUS_SIZE);
            return (int32_t)CANOPUS_SUP_STATUS_SIZE;
        }
    }
    return -1; /* still torn: report retry/error, never a partial record */
}

int32_t canopus_supervisor_device_write(struct canopus_supervisor_v1 *sup,
                                        const void *buffer, uint32_t count)
{
    const uint8_t *b;
    uint32_t magic;
    if (sup == 0 || buffer == 0) {
        return -1;
    }
    b = (const uint8_t *)buffer;
    /* CAN-P0-008: dispatch by magic. Exactly 16 bytes with "CPC1" is the
     * legacy command; a self-describing "CPC2" record is v2. Unknown magic
     * or a wrong-size frame is a negative error and never consumed. */
    if (count == CANOPUS_SUP_COMMAND_SIZE &&
        b[0] == (uint8_t)CANOPUS_SUP_CMD_MAGIC &&
        b[1] == (uint8_t)(CANOPUS_SUP_CMD_MAGIC >> 8) &&
        b[2] == (uint8_t)(CANOPUS_SUP_CMD_MAGIC >> 16) &&
        b[3] == (uint8_t)(CANOPUS_SUP_CMD_MAGIC >> 24)) {
        sup->last_kind = 0;
        (void)canopus_supervisor_handle_command(sup, b);
        return (int32_t)count;
    }
    if (count >= CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        struct canopus_proto_request_v1 req;
        struct canopus_proto_response_v1 resp;
        uint8_t response_payload[CANOPUS_PROTO_MAX_PAYLOAD];
        uint32_t response_payload_len;
        uint32_t poff = 0;
        int rc;
        magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        if (magic == CANOPUS_TRANSPORT_V2_MAGIC) {
            if (canopus_transport_v2_decode_request(b, count, &req, &poff) != 0) {
                return -1; /* malformed v2 record: reject, consume nothing */
            }
            sup->last_kind = 1;
            rc = canopus_supervisor_handle_v2_request(sup, &req, b + poff,
                                                      &resp, &magic);
            if (rc != 0) {
                return -1;
            }
            response_payload_len = render_v2_query_payload(
                sup, &req, &resp, response_payload);
            resp.payload_size = response_payload_len;
            sup->v2_response_len = (uint32_t)canopus_transport_v2_encode_response(
                &resp, magic,
                response_payload_len != 0u ? response_payload : 0,
                response_payload_len, sup->v2_response_buf,
                sizeof(sup->v2_response_buf));
            if (sup->v2_response_len == (uint32_t)-1) {
                return -1;
            }
            return (int32_t)count; /* whole record consumed */
        }
    }
    return -1; /* unknown magic / malformed frame */
}

/* Host convenience: record a newly installed module into a slot.
 * `module_id` becomes the stable identity for v2 per-module commands. */
int canopus_supervisor_add_module(struct canopus_supervisor_v1 *sup,
                                  uint32_t lifecycle_class,
                                  uint32_t version,
                                  uint32_t signature_ok,
                                  const char *module_id)
{
    uint32_t i;
    uint32_t n;
    if (sup == 0) {
        return -1;
    }
    if (!module_lifecycle_ok(lifecycle_class)) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_CLASS;
        return -1;
    }
    if (module_id == 0) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
        return -1;
    }
    n = (uint32_t)canopus_strnlen(module_id, CANOPUS_SUP_MODULE_ID_MAX);
    if (n >= CANOPUS_SUP_MODULE_ID_MAX || n == 0) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT; /* too long or empty */
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state == 0) {
            sup->modules[i].state = CANOPUS_STATE_INSTALLED;
            sup->modules[i].lifecycle_class = lifecycle_class;
            sup->modules[i].version = version;
            sup->modules[i].flags = signature_ok ? 1u : 0u;
            canopus_memset(sup->modules[i].module_id, 0,
                           sizeof(sup->modules[i].module_id));
            canopus_memcpy(sup->modules[i].module_id, module_id, n);
            sup->modules[i].module_id[n] = 0;
            sup->module_count++;
            return (int)i;
        }
    }
    sup->error_code = CANOPUS_SUP_ERR_TABLE_FULL;
    return -1;
}

int canopus_supervisor_attach_tracker(struct canopus_supervisor_v1 *sup,
                                      uint32_t index,
                                      struct canopus_resource_tracker_v1 *tracker,
                                      uint32_t open_refs)
{
    if (sup == 0 || index >= CANOPUS_SUP_MODULE_SLOTS) {
        return -1;
    }
    sup->modules[index].tracker = tracker;
    sup->modules[index].open_refs = open_refs;
    return 0;
}
