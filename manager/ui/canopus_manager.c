/* canopus_manager.c — Manager UI model, pages and operations (CAN-UI-001..004).
 *
 * All rendering is bounded and NUL-terminated; all operation availability is
 * derived from the lifecycle class so the UI can never offer a removable
 * unload path to a resident module.
 */
#include "canopus_manager.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"

#ifdef CANOPUS_HOST
/* Legacy text rendering is host-test-only; the device Manager uses the
 * semantic native UI (canopus_manager_native.c). Kept out of the supervisor
 * module so the boot-resident footprint stays below the loader limit. */
#define CLASS_REMOVABLE_ONLY(c) ((c) == CANOPUS_LIFECYCLE_REMOVABLE)

static void writer_append_u32(struct canopus_text_writer_v1 *w, uint32_t value)
{
    char reverse[10];
    char text[11];
    uint32_t used = 0;
    uint32_t i;
    do {
        reverse[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (i = 0; i < used; i++) {
        text[i] = reverse[used - i - 1u];
    }
    text[used] = '\0';
    canopus_text_writer_append(w, text);
}
#endif /* CANOPUS_HOST */

static int mod_id_eq(const char *a, const char *b)
{
    unsigned int i;
    for (i = 0; i < CANOPUS_MANAGER_MODULE_ID_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

void canopus_manager_init(struct canopus_manager_model_v1 *m,
                          int (*transport)(const struct canopus_proto_request_v1 *,
                                           const void *,
                                           struct canopus_proto_response_v1 *,
                                           void *),
                          void *cookie)
{
    canopus_memset(m, 0, sizeof(*m));
    m->transport = transport;
    m->transport_cookie = cookie;
    m->view = CANOPUS_MANAGER_VIEW_DEVICE;
    m->next_request_id = 1u; /* 0 is reserved (CAN-P1-002) */
}

void canopus_manager_set_identity(struct canopus_manager_model_v1 *m,
                                  const char *target_id,
                                  const char *fw_version,
                                  const char *fw_build,
                                  uint32_t framework_revision)
{
    canopus_buf_copy(m->target_id, sizeof(m->target_id), target_id);
    canopus_buf_copy(m->firmware_version, sizeof(m->firmware_version), fw_version);
    canopus_buf_copy(m->firmware_build, sizeof(m->firmware_build), fw_build);
    m->framework_revision = framework_revision;
}

int canopus_manager_upsert_module(struct canopus_manager_model_v1 *m,
                                  const struct canopus_manager_module_v1 *mod)
{
    uint32_t i;
    for (i = 0; i < m->module_count; i++) {
        if (mod_id_eq(m->modules[i].module_id, mod->module_id)) {
            canopus_memcpy(&m->modules[i], mod, sizeof(*mod));
            return (int)i;
        }
    }
    if (m->module_count >= CANOPUS_MANAGER_MAX_MODULES) {
        return -1;
    }
    canopus_memcpy(&m->modules[m->module_count], mod, sizeof(*mod));
    m->module_count++;
    return (int)(m->module_count - 1);
}

int canopus_manager_goto(struct canopus_manager_model_v1 *m, uint32_t view,
                         uint32_t selected)
{
    if (view != CANOPUS_MANAGER_VIEW_DEVICE &&
        view != CANOPUS_MANAGER_VIEW_MODULE_LIST &&
        view != CANOPUS_MANAGER_VIEW_MODULE_DETAIL) {
        return -1;
    }
    if (view == CANOPUS_MANAGER_VIEW_MODULE_DETAIL &&
        selected >= m->module_count) {
        return -1;
    }
    m->view = view;
    m->selected = selected;
    return 0;
}

#ifdef CANOPUS_HOST
/* ---- pages (host-test only) ---------------------------------------- */

int canopus_manager_render_device(const struct canopus_manager_model_v1 *m,
                                  char *out, uint32_t cap)
{
    struct canopus_text_writer_v1 w;
    if (canopus_text_writer_init(&w, out, cap) != 0) {
        return -1;
    }
    canopus_text_writer_append(&w, "== Canopus Manager ==\n");
    canopus_text_writer_append(&w, "target   : ");
    canopus_text_writer_append(&w, m->target_id);
    canopus_text_writer_append(&w, "\nfirmware : ");
    canopus_text_writer_append(&w, m->firmware_version);
    canopus_text_writer_append(&w, " (");
    canopus_text_writer_append(&w, m->firmware_build);
    canopus_text_writer_append(&w, ")\nframework: v");
    writer_append_u32(&w, m->framework_revision);
    canopus_text_writer_append(&w, "\n");
    if (m->safe_mode) {
        canopus_text_writer_append(&w, "** SAFE MODE **\n");
    }
    return w.truncated ? CANOPUS_TEXT_TRUNCATED : 0;
}

int canopus_manager_render_module_list(const struct canopus_manager_model_v1 *m,
                                       char *out, uint32_t cap)
{
    uint32_t i;
    struct canopus_text_writer_v1 w;
    if (canopus_text_writer_init(&w, out, cap) != 0) {
        return -1;
    }
    canopus_text_writer_append(&w, "== modules ==\n");
    for (i = 0; i < m->module_count; i++) {
        const struct canopus_manager_module_v1 *mod = &m->modules[i];
        canopus_text_writer_append(&w, " ");
        canopus_text_writer_append(&w, i == m->selected ? "* " : "  ");
        canopus_text_writer_append(&w,
                                   mod->module_id[0] ? mod->module_id : "(unnamed)");
        canopus_text_writer_append(&w, "  ");
        canopus_text_writer_append(&w, canopus_state_name(mod->state));
        canopus_text_writer_append(&w, " v");
        writer_append_u32(&w, mod->version);
        canopus_text_writer_append(&w, "\n");
    }
    return w.truncated ? CANOPUS_TEXT_TRUNCATED : 0;
}

int canopus_manager_render_module_detail(const struct canopus_manager_model_v1 *m,
                                         char *out, uint32_t cap)
{
    const struct canopus_manager_module_v1 *mod;
    struct canopus_text_writer_v1 w;
    if (canopus_text_writer_init(&w, out, cap) != 0 ||
        m->selected >= m->module_count) {
        return -1;
    }
    mod = &m->modules[m->selected];
    canopus_text_writer_append(&w, "== module ==\n");
    canopus_text_writer_append(&w,
                               mod->module_id[0] ? mod->module_id : "(unnamed)");
    canopus_text_writer_append(&w, "\nstate    : ");
    canopus_text_writer_append(&w, canopus_state_name(mod->state));
    canopus_text_writer_append(&w, "\nclass    : ");
    canopus_text_writer_append(
        &w, mod->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ? "removable" :
            mod->lifecycle_class == CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION ? "resident-after-activation" :
            mod->lifecycle_class == CANOPUS_LIFECYCLE_ALWAYS_RESIDENT ? "always-resident" :
            "patch-reboot-required");
    canopus_text_writer_append(&w, "\nsignature: ");
    canopus_text_writer_append(&w,
                               mod->signature_ok ? "verified" : "unsigned/dev");
    canopus_text_writer_append(&w, "\nrisk     : ");
    writer_append_u32(&w, mod->risk);
    canopus_text_writer_append(&w, "\n");

    /* available operations for THIS class — never a fake unload. */
    canopus_text_writer_append(&w, "ops: ");
    if (canopus_manager_can_update(m, m->selected)) {
        canopus_text_writer_append(&w, "[update] ");
    }
    if (canopus_manager_can_rollback(m, m->selected)) {
        canopus_text_writer_append(&w, "[rollback] ");
    }
    if (canopus_manager_can_disable(m, m->selected)) {
        canopus_text_writer_append(&w,
                                   CLASS_REMOVABLE_ONLY(mod->lifecycle_class) ?
                                   "[disable] " : "[disable-next-boot] ");
    }
    if (canopus_manager_can_remove(m, m->selected)) {
        canopus_text_writer_append(&w,
                                   CLASS_REMOVABLE_ONLY(mod->lifecycle_class) ?
                                   "[remove] " : "[remove+reboot] ");
    }
    canopus_text_writer_append(&w, "\n");
    return w.truncated ? CANOPUS_TEXT_TRUNCATED : 0;
}
#endif /* CANOPUS_HOST */

/* ---- availability helpers ------------------------------------------ */

int canopus_manager_can_enable(const struct canopus_manager_model_v1 *m,
                               uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    /* CAN-P2-006: activation is never offered in safe mode. */
    if (m->safe_mode) {
        return 0;
    }
    mod = &m->modules[index];
    /* next-boot: every non-resident state can be armed to load at reboot. */
    switch (mod->state) {
    case CANOPUS_STATE_INSTALLED:
    case CANOPUS_STATE_DISABLED:
    case CANOPUS_STATE_UNLOADED:
    case CANOPUS_STATE_DISABLED_NEXT_BOOT:
        return 1;
    default:
        return 0;
    }
}

int canopus_manager_can_disable(const struct canopus_manager_model_v1 *m,
                                uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    mod = &m->modules[index];
    /* next-boot: a module can only be armed to stop if its code is resident,
     * or a pending enable can be cancelled. */
    switch (mod->state) {
    case CANOPUS_STATE_ACTIVE:
    case CANOPUS_STATE_READY:
    case CANOPUS_STATE_BOOT_RESIDENT:
    case CANOPUS_STATE_ENABLED:
        return 1;
    default:
        return 0;
    }
}

int canopus_manager_can_remove(const struct canopus_manager_model_v1 *m,
                               uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    mod = &m->modules[index];
    return mod->state != CANOPUS_STATE_UNLOADED &&
           mod->state != CANOPUS_STATE_REMOVE_PENDING;
}

int canopus_manager_can_update(const struct canopus_manager_model_v1 *m,
                               uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    mod = &m->modules[index];
    /* CAN-P2-006: activation is never offered in safe mode. */
    if (m->safe_mode) {
        return 0;
    }
    return mod->state != CANOPUS_STATE_UNLOADED;
}

int canopus_manager_can_rollback(const struct canopus_manager_model_v1 *m,
                                 uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    mod = &m->modules[index];
    return mod->has_previous != 0;
}

/* ---- transport helper ---------------------------------------------- */

static uint32_t send_command(struct canopus_manager_model_v1 *m,
                             uint32_t command,
                             const void *payload,
                             uint32_t payload_size)
{
    struct canopus_proto_request_v1 req;
    struct canopus_proto_response_v1 resp;
    uint32_t request_id;
    if (m->transport == 0) {
        return CANOPUS_RESULT_REJECTED;
    }
    /* CAN-P1-002: request ids are client-monotonic, never a fixed value per
     * opcode, and never 0. Wrap around skips 0 so the reserved id is not
     * reused. */
    request_id = m->next_request_id;
    m->next_request_id += 1u;
    if (m->next_request_id == 0u) {
        m->next_request_id = 1u;
    }
    canopus_memset(&req, 0, sizeof(req));
    req.magic = CANOPUS_PROTO_MAGIC;
    req.struct_size = CANOPUS_PROTO_REQUEST_SIZE;
    req.abi_major = CANOPUS_ABI_MAJOR;
    req.abi_minor = CANOPUS_ABI_MINOR;
    req.command = command;
    req.request_id = request_id;
    req.payload_size = payload_size;
    req.flags = 0;
    canopus_memset(&resp, 0, sizeof(resp));
    if (m->transport(&req, payload, &resp, m->transport_cookie) != 0) {
        return CANOPUS_RESULT_REJECTED;
    }
    m->pending_op = command;
    m->pending_state = resp.result_state;
    return resp.result_state;
}

/* ---- operations ----------------------------------------------------- */

uint32_t canopus_manager_op_install(struct canopus_manager_model_v1 *m,
                                    const char *package_ref)
{
    /* CAN-P2-006: install is an activation and is not offered in safe mode. */
    if (m->safe_mode) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_INSTALL, package_ref,
                        (uint32_t)(package_ref ? canopus_strlen(package_ref) + 1 : 0));
}

uint32_t canopus_manager_op_activate(struct canopus_manager_model_v1 *m,
                                     uint32_t index)
{
    if (index >= m->module_count || m->safe_mode ||
        m->modules[index].state != CANOPUS_STATE_READY) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_ACTIVATE, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_enable(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (index >= m->module_count) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_ENABLE, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_disable(struct canopus_manager_model_v1 *m,
                                    uint32_t index)
{
    if (!canopus_manager_can_disable(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    /* removable: unload now. resident: next-boot only. The protocol command
     * is the same; the supervisor interprets it by lifecycle class. */
    return send_command(m, CANOPUS_CMD_DISABLE, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_remove(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (!canopus_manager_can_remove(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_REMOVE, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_update(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (!canopus_manager_can_update(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_UPDATE, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_rollback(struct canopus_manager_model_v1 *m,
                                     uint32_t index)
{
    if (!canopus_manager_can_rollback(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_ROLLBACK, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_safe_mode(struct canopus_manager_model_v1 *m)
{
    uint32_t rc = send_command(m, CANOPUS_CMD_ENTER_SAFE_MODE, 0, 0);
    if (rc == CANOPUS_RESULT_ACCEPTED || rc == CANOPUS_RESULT_COMPLETED) {
        m->safe_mode = 1;
    }
    return rc;
}
