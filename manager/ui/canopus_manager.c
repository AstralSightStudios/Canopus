/* canopus_manager.c — Manager UI model, pages and operations (CAN-UI-001..004).
 *
 * All rendering is bounded and NUL-terminated; all operation availability is
 * derived from the lifecycle class so the UI can never offer a removable
 * unload path to a resident module.
 */
#include "canopus_manager.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <stdio.h> /* snprintf: host tests + the Manager native app have libc */

#define CLASS_REMOVABLE_ONLY(c) ((c) == CANOPUS_LIFECYCLE_REMOVABLE)

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
            m->modules[i] = *mod;
            return (int)i;
        }
    }
    if (m->module_count >= CANOPUS_MANAGER_MAX_MODULES) {
        return -1;
    }
    m->modules[m->module_count] = *mod;
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
    if (view != CANOPUS_MANAGER_VIEW_DEVICE && selected >= m->module_count) {
        return -1;
    }
    m->view = view;
    m->selected = selected;
    return 0;
}

/* ---- pages -------------------------------------------------------- */

int canopus_manager_render_device(const struct canopus_manager_model_v1 *m,
                                  char *out, uint32_t cap)
{
    int n = 0;
    if (cap == 0 || out == 0) {
        return -1;
    }
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "== Canopus Manager ==\n");
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "target   : ");
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, m->target_id);
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "\nfirmware : ");
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, m->firmware_version);
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, " (");
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, m->firmware_build);
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, ")\nframework: v");
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n), "%u\n", m->framework_revision);
    if (m->safe_mode) {
        n += canopus_buf_copy(out + n, cap - (uint32_t)n, "** SAFE MODE **\n");
    }
    return 0;
}

int canopus_manager_render_module_list(const struct canopus_manager_model_v1 *m,
                                       char *out, uint32_t cap)
{
    uint32_t i;
    int n = 0;
    if (cap == 0 || out == 0) {
        return -1;
    }
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "== modules ==\n");
    for (i = 0; i < m->module_count; i++) {
        const struct canopus_manager_module_v1 *mod = &m->modules[i];
        n += snprintf(out + n, (size_t)(cap - (uint32_t)n), " %c %s  %s v%u\n",
                      i == m->selected ? '*' : ' ',
                      mod->module_id[0] ? mod->module_id : "(unnamed)",
                      canopus_state_name(mod->state), mod->version);
    }
    return 0;
}

int canopus_manager_render_module_detail(const struct canopus_manager_model_v1 *m,
                                         char *out, uint32_t cap)
{
    const struct canopus_manager_module_v1 *mod;
    int n = 0;
    if (cap == 0 || out == 0 || m->selected >= m->module_count) {
        return -1;
    }
    mod = &m->modules[m->selected];
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "== module ==\n");
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n), "%s\n",
                  mod->module_id[0] ? mod->module_id : "(unnamed)");
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n),
                  "state    : %s\n", canopus_state_name(mod->state));
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n),
                  "class    : %s\n",
                  mod->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ? "removable" :
                  mod->lifecycle_class == CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION ? "resident-after-activation" :
                  mod->lifecycle_class == CANOPUS_LIFECYCLE_ALWAYS_RESIDENT ? "always-resident" :
                  "patch-reboot-required");
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n),
                  "signature: %s\n", mod->signature_ok ? "verified" : "unsigned/dev");
    n += snprintf(out + n, (size_t)(cap - (uint32_t)n),
                  "risk     : %u\n", mod->risk);

    /* available operations for THIS class — never a fake unload. */
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "ops: ");
    if (canopus_manager_can_update(m, m->selected)) {
        n += canopus_buf_copy(out + n, cap - (uint32_t)n, "[update] ");
    }
    if (canopus_manager_can_rollback(m, m->selected)) {
        n += canopus_buf_copy(out + n, cap - (uint32_t)n, "[rollback] ");
    }
    if (canopus_manager_can_disable(m, m->selected)) {
        n += canopus_buf_copy(out + n, cap - (uint32_t)n,
                              CLASS_REMOVABLE_ONLY(mod->lifecycle_class) ?
                              "[disable] " : "[disable-next-boot] ");
    }
    if (canopus_manager_can_remove(m, m->selected)) {
        n += canopus_buf_copy(out + n, cap - (uint32_t)n,
                              CLASS_REMOVABLE_ONLY(mod->lifecycle_class) ?
                              "[remove] " : "[remove+reboot] ");
    }
    n += canopus_buf_copy(out + n, cap - (uint32_t)n, "\n");
    return 0;
}

/* ---- availability helpers ------------------------------------------ */

int canopus_manager_can_disable(const struct canopus_manager_model_v1 *m,
                                uint32_t index)
{
    const struct canopus_manager_module_v1 *mod;
    if (index >= m->module_count) {
        return 0;
    }
    mod = &m->modules[index];
    /* removable: DISABLED may be unloaded. resident: only next-boot. */
    return mod->state != CANOPUS_STATE_DISABLED &&
           mod->state != CANOPUS_STATE_UNLOADED;
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
                             uint32_t request_id,
                             const void *payload,
                             uint32_t payload_size)
{
    struct canopus_proto_request_v1 req;
    struct canopus_proto_response_v1 resp;
    if (m->transport == 0) {
        return CANOPUS_RESULT_REJECTED;
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
    return send_command(m, CANOPUS_CMD_INSTALL, 1, package_ref,
                        (uint32_t)(package_ref ? canopus_strlen(package_ref) + 1 : 0));
}

uint32_t canopus_manager_op_enable(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (index >= m->module_count) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_ENABLE, 2, m->modules[index].module_id,
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
    return send_command(m, CANOPUS_CMD_DISABLE, 3, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_remove(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (!canopus_manager_can_remove(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_REMOVE, 4, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_update(struct canopus_manager_model_v1 *m,
                                   uint32_t index)
{
    if (!canopus_manager_can_update(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_UPDATE, 5, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_rollback(struct canopus_manager_model_v1 *m,
                                     uint32_t index)
{
    if (!canopus_manager_can_rollback(m, index)) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    return send_command(m, CANOPUS_CMD_ROLLBACK, 6, m->modules[index].module_id,
                        CANOPUS_MANAGER_MODULE_ID_MAX);
}

uint32_t canopus_manager_op_safe_mode(struct canopus_manager_model_v1 *m)
{
    uint32_t rc = send_command(m, CANOPUS_CMD_ENTER_SAFE_MODE, 7, 0, 0);
    if (rc == CANOPUS_RESULT_ACCEPTED || rc == CANOPUS_RESULT_COMPLETED) {
        m->safe_mode = 1;
    }
    return rc;
}
