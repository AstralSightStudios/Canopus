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

__attribute__((weak)) void canopus_manager_target_render_diagnostics(
    uint8_t out[36])
{
    canopus_memset(out, 0, 36u);
}

static int module_lifecycle_ok(uint32_t lifecycle_class)
{
    return lifecycle_class <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED;
}

/* CAN-P0-005 revision (next-boot): a module is *loaded* only when its code
 * is actually resident this session. INSTALLED/DISABLED/ENABLED are all
 * non-resident bookkeeping states. */
static int sup_module_loaded(uint32_t state)
{
    return state == CANOPUS_STATE_ACTIVE || state == CANOPUS_STATE_READY ||
           state == CANOPUS_STATE_BOOT_RESIDENT;
}

/* CAN-P1-007: reclaim a slot (used after a remove intent is applied at
 * boot restore). */
static void sup_clear_slot(struct canopus_supervisor_v1 *sup, int slot)
{
    if (sup->modules[slot].state != 0) {
        sup->module_count--;
    }
    canopus_memset(&sup->modules[slot], 0, sizeof(sup->modules[slot]));
}

static int sup_fixed_string_equal(const uint8_t *fixed, uint32_t capacity,
                                  const char *text)
{
    uint32_t i;
    if (fixed == 0 || text == 0) return 0;
    for (i = 0u; i < capacity; i++) {
        uint8_t value = (uint8_t)text[i];
        if (fixed[i] != value) return 0;
        if (value == 0u) return 1;
    }
    return 0;
}

static uint32_t sup_registry_error_encode(uint32_t stored_error)
{
    int32_t error = (int32_t)stored_error;
    uint64_t magnitude;
    uint64_t encoded;
    if (error < 0) {
        magnitude = (uint64_t)(-(int64_t)error);
        encoded = (magnitude << 1) - 1u;
    } else {
        encoded = (uint64_t)(uint32_t)error << 1;
    }
    if (encoded > 0x7fffffffu) {
        encoded = ((uint32_t)(-CANOPUS_SUP_ERR_ACTIVATE) << 1) - 1u;
    }
    return (uint32_t)encoded;
}

static uint32_t sup_registry_error_decode(uint32_t registry_flags)
{
    uint32_t encoded = registry_flags >> CANOPUS_SUP_REGISTRY_ERROR_SHIFT;
    uint32_t magnitude = (encoded >> 1) + (encoded & 1u);
    int32_t error = (encoded & 1u) != 0u
                        ? -(int32_t)magnitude : (int32_t)magnitude;
    return (uint32_t)error;
}

static uint32_t sup_activate_module(struct canopus_supervisor_v1 *sup,
                                    struct canopus_sup_module_v1 *module)
{
    int activate_rc;
    if (module->state != CANOPUS_STATE_READY ||
        module->intent != CANOPUS_SUP_INTENT_ENABLED) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
        return CANOPUS_RESULT_DISALLOWED;
    }
    if (module->descriptor == 0 || module->descriptor->activate == 0) {
        module->state = CANOPUS_STATE_FAILED;
        module->activation_error =
            (uint32_t)CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
        sup->error_code = CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
        return CANOPUS_RESULT_FAILED;
    }
    activate_rc = module->descriptor->activate(0);
    if (activate_rc != 0) {
        module->state = CANOPUS_STATE_FAILED;
        module->activation_error = (uint32_t)activate_rc;
        sup->error_code = CANOPUS_SUP_ERR_ACTIVATE;
        return CANOPUS_RESULT_FAILED;
    }
    module->state =
        module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE
            ? CANOPUS_STATE_ACTIVE : CANOPUS_STATE_BOOT_RESIDENT;
    module->activation_error = 0u;
    return CANOPUS_RESULT_COMPLETED;
}

int canopus_supervisor_register_descriptor(
    struct canopus_supervisor_v1 *sup, const char *module_id,
    const struct canopus_module_descriptor_v1 *descriptor)
{
    uint32_t i;
    if (sup == 0 || module_id == 0 || descriptor == 0 ||
        sup->loading_slot < 0 ||
        (uint32_t)sup->loading_slot >= CANOPUS_SUP_MODULE_SLOTS ||
        canopus_module_descriptor_check(descriptor) != 0 ||
        sup->platform == 0 || sup->platform->target_id == 0 ||
        !sup_fixed_string_equal(descriptor->target_id,
                                sizeof(descriptor->target_id),
                                sup->platform->target_id)) {
        return -1;
    }
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        if ((int32_t)i == sup->loading_slot && module->state != 0u &&
            sup_fixed_string_equal(module->module_id,
                                   sizeof(module->module_id), module_id) &&
            sup_fixed_string_equal(descriptor->module_id,
                                   sizeof(descriptor->module_id), module_id)) {
            module->descriptor = descriptor;
            return 0;
        }
    }
    return -1;
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
    sup->loading_slot = -1;
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
 * operations are allowed: QUERY, ROLLBACK, ENTER_SAFE_MODE, DISABLE and
 * REMOVE (which are next-boot only for every lifecycle class, so they never
 * run third-party code in safe mode). INSTALL, ENABLE and UPDATE — anything
 * that activates code — are rejected. */
static int sup_safe_mode_allows(const struct canopus_supervisor_v1 *sup,
                                uint32_t op, int slot)
{
    (void)sup;
    (void)slot;
    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
    case CANOPUS_SUP_CMD_ROLLBACK:
    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
        return 1;
    case CANOPUS_SUP_CMD_INSTALL:
    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_UPDATE:
    case CANOPUS_SUP_CMD_ACTIVATE:
    case CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT:
        return 0; /* activation is never allowed in safe mode */
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
        uint32_t previous_occupied = 0u;
        uint32_t previous_count = sup->module_count;
        uint32_t i;
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            if (sup->modules[i].state != 0u) previous_occupied |= 1u << i;
        }
        if (stage_arg == 0 && arg0 > 2u) {
            rc = CANOPUS_RESULT_FAILED;
        } else if (sup->platform && sup->platform->stage_package) {
            rc = sup->platform->stage_package(sup->platform_cookie, stage_arg,
                                              arg0) == 0
                     ? CANOPUS_RESULT_COMPLETED
                     : CANOPUS_RESULT_FAILED;
        } else {
            rc = CANOPUS_RESULT_FAILED;
        }
        /* §16.4: the registry is written on every slot change, INSTALL
         * included. A freshly installed (disabled) module must survive a
         * reboot even before it is ever enabled; otherwise "install then
         * reboot" silently loses it. Persist immediately after staging. */
        if (rc == CANOPUS_RESULT_COMPLETED) {
            int persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
                    if ((previous_occupied & (1u << i)) == 0u &&
                        sup->modules[i].state != 0u) {
                        canopus_memset(&sup->modules[i], 0,
                                       sizeof(sup->modules[i]));
                    }
                }
                sup->module_count = previous_count;
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        if (rc == CANOPUS_RESULT_FAILED &&
            sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE;
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
    case CANOPUS_SUP_CMD_UPDATE:
    case CANOPUS_SUP_CMD_ROLLBACK: {
        struct canopus_sup_module_v1 previous;
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0) {
            rc = CANOPUS_RESULT_DISALLOWED;
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            break;
        }
        /* CAN-P0-005 revision (next-boot): no lifecycle class hot-loads or
         * hot-unloads anymore. Every op records a boot intent and reports
         * REBOOT_REQUIRED; the next supervisor load applies the intent.
         * A remove-pending slot is committed and not re-targetable. */
        if (sup->modules[slot].state == CANOPUS_STATE_REMOVE_PENDING &&
            op != CANOPUS_SUP_CMD_REMOVE) {
            rc = CANOPUS_RESULT_DISALLOWED;
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            break;
        }
        canopus_memcpy(&previous, &sup->modules[slot], sizeof(previous));
        switch (op) {
        case CANOPUS_SUP_CMD_ENABLE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_ENABLED;
            sup->modules[slot].state = CANOPUS_STATE_ENABLED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        case CANOPUS_SUP_CMD_DISABLE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_DISABLED;
            sup->modules[slot].state = sup_module_loaded(sup->modules[slot].state)
                ? CANOPUS_STATE_DISABLED_NEXT_BOOT : CANOPUS_STATE_DISABLED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        case CANOPUS_SUP_CMD_REMOVE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_REMOVE;
            sup->modules[slot].state = CANOPUS_STATE_REMOVE_PENDING;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        default: /* UPDATE / ROLLBACK */
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_ENABLED;
            sup->modules[slot].state = CANOPUS_STATE_UPDATE_STAGED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        }
        if (rc == CANOPUS_RESULT_REBOOT_REQUIRED) {
            int persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                canopus_memcpy(&sup->modules[slot], &previous, sizeof(previous));
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        break;
    }

    case CANOPUS_SUP_CMD_ACTIVATE: {
        struct canopus_sup_module_v1 *module;
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0u) {
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            rc = CANOPUS_RESULT_DISALLOWED;
            break;
        }
        module = &sup->modules[slot];
        if ((module->state == CANOPUS_STATE_INSTALLED ||
             module->state == CANOPUS_STATE_ENABLED) &&
            module->intent == CANOPUS_SUP_INTENT_ENABLED) {
            int st = sup->platform != 0 && sup->platform->load_module != 0
                         ? sup->platform->load_module(
                               sup->platform_cookie, (uint32_t)slot,
                               (const char *)module->module_id,
                               module->lifecycle_class)
                         : -1;
            if (st < 0) {
                module->state = CANOPUS_STATE_FAILED;
                module->activation_error = (uint32_t)st;
                sup->error_code = CANOPUS_SUP_ERR_LOAD;
                rc = CANOPUS_RESULT_FAILED;
            } else {
                module->state = (uint32_t)st;
                rc = st == CANOPUS_STATE_READY
                         ? sup_activate_module(sup, module)
                         : CANOPUS_RESULT_COMPLETED;
            }
        } else {
            rc = sup_activate_module(sup, module);
        }
        if (rc == CANOPUS_RESULT_COMPLETED ||
            rc == CANOPUS_RESULT_FAILED) {
            int persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        break;
    }

    case CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT:
        rc = canopus_supervisor_activate_restored_modules(sup) == 0
                 ? CANOPUS_RESULT_COMPLETED
                 : CANOPUS_RESULT_FAILED;
        if (rc == CANOPUS_RESULT_FAILED &&
            sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_LOAD;
        }
        break;

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
    if (op == CANOPUS_SUP_CMD_QUERY && arg0 == CANOPUS_SUP_DIAG_QUERY_MAGIC) {
        /* Read the last mutation result without erasing its failure code. */
        sup->pending_state = CANOPUS_RESULT_COMPLETED;
        canopus_snapshot_commit(&sup->snap);
        return sup->pending_state;
    }
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
    case CANOPUS_CMD_ACTIVATE:
        return CANOPUS_SUP_CMD_ACTIVATE;
    default:
        return 0; /* unknown command */
    }
}

static int v2_op_needs_slot(uint32_t cmd)
{
    return cmd == CANOPUS_CMD_ENABLE || cmd == CANOPUS_CMD_DISABLE ||
           cmd == CANOPUS_CMD_REMOVE || cmd == CANOPUS_CMD_UPDATE ||
           cmd == CANOPUS_CMD_ROLLBACK || cmd == CANOPUS_CMD_ACTIVATE;
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
    if (req->command != CANOPUS_CMD_QUERY_DEVICE &&
        req->command != CANOPUS_CMD_QUERY_MODULE &&
        req->command != CANOPUS_CMD_ECHO) {
        sup->error_code = CANOPUS_SUP_ERR_NONE;
    }
    rc = sup_dispatch(sup, op,
                      req->command == CANOPUS_CMD_INSTALL ? 0u : slot_arg, 0,
                      req->command == CANOPUS_CMD_INSTALL
                          ? (const char *)payload : 0);
    sup->pending_state = rc;
    canopus_snapshot_commit(&sup->snap);
    if (track_request) {
        canopus_pending_set_error(&sup->pending, req->request_id,
                                  sup->error_code);
        (void)canopus_pending_finish(&sup->pending, req->request_id, rc);
        /* CPC2 operations are synchronous today: the final result is returned
         * in this response, so retaining the terminal record would consume
         * one of the bounded request slots forever. Future genuinely async
         * operations may retain their record until an explicit ACK path. */
        (void)canopus_pending_ack(&sup->pending, req->request_id);
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
        put_wire_u32(out, 60u, module->activation_error);
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
    uint32_t module_error = 0u;
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
        if (module_error == 0u && sup->modules[i].activation_error != 0u) {
            module_error = sup->modules[i].activation_error;
        }
    }
    PUT32(CANOPUS_SUP_STATUS_MODULE_ERROR_OFF, module_error);
    canopus_manager_target_render_diagnostics(out + 48u);
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
            /* Disabled by default: nothing is ever loaded at install. */
            sup->modules[i].intent = CANOPUS_SUP_INTENT_DISABLED;
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

int canopus_supervisor_publish_native_apps(struct canopus_supervisor_v1 *sup,
                                           uint32_t stage)
{
    uint32_t i;
    int failed = 0;
    if (sup == 0 || (stage != 1u && stage != 2u)) return -1;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        uint32_t legacy_end;
        uint32_t staged_end;
        int rc;
        if (!sup_module_loaded(module->state) || module->descriptor == 0 ||
            (module->descriptor->flags & CANOPUS_FLAG_HAS_NATIVE_APP) == 0u) {
            continue;
        }
        legacy_end =
            (uint32_t)offsetof(struct canopus_module_descriptor_v1,
                               publish_native_app) +
            (uint32_t)sizeof(module->descriptor->publish_native_app);
        staged_end =
            (uint32_t)offsetof(struct canopus_module_descriptor_v1,
                               publish_native_app_stage) +
            (uint32_t)sizeof(module->descriptor->publish_native_app_stage);
        if (module->descriptor->abi_minor >= 2u &&
            module->descriptor->struct_size >= staged_end &&
            module->descriptor->publish_native_app_stage != 0) {
            rc = module->descriptor->publish_native_app_stage(0, stage);
        } else if (stage == 1u && module->descriptor->abi_minor >= 1u &&
                   module->descriptor->struct_size >= legacy_end &&
                   module->descriptor->publish_native_app != 0) {
            rc = module->descriptor->publish_native_app(0);
        } else {
            continue;
        }
        if (rc != 0) {
            module->activation_error = (uint32_t)rc;
            failed = -1;
        } else {
            module->activation_error = 0u;
        }
    }
    if (canopus_supervisor_save_registry(sup) != 0) return -1;
    return failed;
}

/* ---- CAN-P0-005 revision: next-boot registry persistence ------------ */

int canopus_supervisor_save_registry(struct canopus_supervisor_v1 *sup)
{
    uint8_t buf[CANOPUS_SUP_REGISTRY_SIZE];
    uint32_t i;
    if (sup == 0 || sup->platform == 0 || sup->platform->persist == 0) {
        return -1;
    }
    canopus_memset(buf, 0, sizeof(buf));
    put_wire_u32(buf, 0u, CANOPUS_SUP_REGISTRY_MAGIC);
    put_wire_u32(buf, 4u, CANOPUS_SUP_REGISTRY_VERSION);
    put_wire_u32(buf, 8u, sup->module_count);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        const struct canopus_sup_module_v1 *m = &sup->modules[i];
        uint32_t o = CANOPUS_SUP_REGISTRY_HEADER +
                     i * CANOPUS_SUP_REGISTRY_SLOT_SIZE;
        if (m->state != 0u) {
            canopus_memcpy(buf + o, m->module_id, CANOPUS_SUP_MODULE_ID_MAX);
            put_wire_u32(buf, o + 32u, m->lifecycle_class);
            put_wire_u32(buf, o + 36u, m->version);
            put_wire_u32(buf, o + 40u,
                         (m->flags & CANOPUS_SUP_FLAG_PUBLIC_MASK) |
                         (sup_registry_error_encode(m->activation_error) <<
                          CANOPUS_SUP_REGISTRY_ERROR_SHIFT));
            put_wire_u32(buf, o + 44u, m->intent);
        }
    }
    return sup->platform->persist(sup->platform_cookie, buf, sizeof(buf));
}

/* Restore the slot table after a fresh load. Absent registry == fresh
 * install (not an error). Enabled intents are loaded through the platform;
 * remove intents delete their artifacts and never re-register. */
static int sup_restore_registry(struct canopus_supervisor_v1 *sup,
                                int load_enabled_modules)
{
    uint8_t buf[CANOPUS_SUP_REGISTRY_SIZE];
    uint32_t i;
    int rc;
    int restored_enabled = 0;
    if (sup == 0 || sup->platform == 0 || sup->platform->restore == 0) {
        return -1;
    }
    rc = sup->platform->restore(sup->platform_cookie, buf, sizeof(buf));
    if (rc != 0) {
        return rc > 0 ? 0 : -1; /* absent (1) is fine; hard error fails */
    }
    if (wire_u32(buf) != CANOPUS_SUP_REGISTRY_MAGIC ||
        wire_u32(buf + 4u) != CANOPUS_SUP_REGISTRY_VERSION) {
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        uint32_t o = CANOPUS_SUP_REGISTRY_HEADER +
                     i * CANOPUS_SUP_REGISTRY_SLOT_SIZE;
        const uint8_t *id = buf + o;
        uint32_t lifecycle_class = wire_u32(buf + o + 32u);
        uint32_t version = wire_u32(buf + o + 36u);
        uint32_t flags = wire_u32(buf + o + 40u);
        uint32_t intent = wire_u32(buf + o + 44u);
        int slot;

        if (id[0] == 0u) {
            continue; /* empty entry */
        }
        if (lifecycle_class > CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED) {
            continue;
        }
        if (intent == CANOPUS_SUP_INTENT_REMOVE) {
            /* commit the removal: register briefly so the platform hook can
             * resolve the module id, delete the artifacts, then reclaim */
            slot = canopus_supervisor_add_module(
                sup, lifecycle_class, version, 1u, (const char *)id);
            if (slot >= 0) {
                if (sup->platform->remove_artifact != 0) {
                    (void)sup->platform->remove_artifact(
                        sup->platform_cookie, (uint32_t)slot);
                }
                sup_clear_slot(sup, slot);
            }
            continue;
        }
        slot = canopus_supervisor_add_module(
            sup, lifecycle_class, version,
            (flags & CANOPUS_SUP_FLAG_SIGNATURE_OK) != 0u,
            (const char *)id);
        if (slot < 0) {
            continue;
        }
        sup->modules[slot].intent =
            intent == CANOPUS_SUP_INTENT_ENABLED
                ? CANOPUS_SUP_INTENT_ENABLED : CANOPUS_SUP_INTENT_DISABLED;
        sup->modules[slot].activation_error =
            sup_registry_error_decode(flags);
        sup->modules[slot].flags &= CANOPUS_SUP_FLAG_PUBLIC_MASK;
        /* Full restore is deliberately two-phase: import every slot before
         * loading code, so an intermediate save can never truncate trailing
         * metadata. The production constructor uses the metadata-only phase
         * for the same low-stack reason. */
    }
    if (load_enabled_modules && sup->platform->load_module != 0) {
        for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            struct canopus_sup_module_v1 *module = &sup->modules[i];
            int st;
            if (module->intent != CANOPUS_SUP_INTENT_ENABLED ||
                module->state != CANOPUS_STATE_INSTALLED) {
                continue;
            }
            restored_enabled = 1;
            st = sup->platform->load_module(
                sup->platform_cookie, i, (const char *)module->module_id,
                module->lifecycle_class);
            if (st < 0) {
                module->state = CANOPUS_STATE_FAILED;
                module->activation_error = (uint32_t)st;
                sup->error_code = CANOPUS_SUP_ERR_LOAD;
                continue;
            }
            module->state = (uint32_t)st;
            if (st == CANOPUS_STATE_READY) {
                (void)sup_activate_module(sup, module);
            }
        }
        if (restored_enabled && canopus_supervisor_save_registry(sup) != 0) {
            sup->error_code = CANOPUS_SUP_ERR_REGISTRY;
        }
    }
    return 0;
}

int canopus_supervisor_restore_registry(struct canopus_supervisor_v1 *sup)
{
    return sup_restore_registry(sup, 1);
}

int canopus_supervisor_restore_registry_metadata(struct canopus_supervisor_v1 *sup)
{
    return sup_restore_registry(sup, 0);
}

int canopus_supervisor_activate_restored_modules(struct canopus_supervisor_v1 *sup)
{
    uint32_t i;
    int changed = 0;
    int failed = 0;

    if (sup == 0 || sup->platform == 0 || sup->platform->load_module == 0) {
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        int st;

        if (module->intent != CANOPUS_SUP_INTENT_ENABLED ||
            module->state != CANOPUS_STATE_INSTALLED) {
            continue;
        }
        changed = 1;
        st = sup->platform->load_module(sup->platform_cookie, i,
                                        (const char *)module->module_id,
                                        module->lifecycle_class);
        if (st < 0) {
            module->state = CANOPUS_STATE_FAILED;
            module->activation_error = (uint32_t)st;
            sup->error_code = CANOPUS_SUP_ERR_LOAD;
            failed = -1;
            continue;
        }
        module->state = (uint32_t)st;
        if (st == CANOPUS_STATE_READY &&
            sup_activate_module(sup, module) != CANOPUS_RESULT_COMPLETED) {
            failed = -1;
        }
    }
    if (changed && canopus_supervisor_save_registry(sup) != 0) {
        return -1;
    }
    return failed;
}
