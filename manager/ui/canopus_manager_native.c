/* canopus_manager_native.c — Manager model rendered as semantic native UI. */
#include "canopus_manager_native.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

#define UI_KEY_ROOT             1u
#define UI_KEY_MAIN_SECTION    10u
#define UI_KEY_TARGET          11u
#define UI_KEY_FIRMWARE        12u
#define UI_KEY_BUILD           13u
#define UI_KEY_FRAMEWORK       14u
#define UI_KEY_MODULES         15u
#define UI_KEY_INSTALL         16u
#define UI_KEY_SAFE_MODE       17u
#define UI_KEY_MODULE_SECTION  20u
#define UI_KEY_MODULE_EMPTY    21u
#define UI_KEY_MODULE_BACK     22u
#define UI_KEY_MODULE_BASE    100u
#define UI_KEY_DETAIL_SECTION 200u
#define UI_KEY_DETAIL_STATE   201u
#define UI_KEY_DETAIL_CLASS   202u
#define UI_KEY_DETAIL_SIGN    203u
#define UI_KEY_DETAIL_VERSION 204u
#define UI_KEY_DETAIL_UPDATE  210u
#define UI_KEY_DETAIL_ROLLBACK 211u
#define UI_KEY_DETAIL_ENABLE  212u
#define UI_KEY_DETAIL_DISABLE 213u
#define UI_KEY_DETAIL_REMOVE  214u
#define UI_KEY_DETAIL_BACK    215u

static uint32_t bounded_len(const char *value, uint32_t cap)
{
    uint32_t len = 0;
    if (value == 0) {
        return 0;
    }
    while (len < cap && value[len] != '\0') {
        len++;
    }
    return len;
}

static int stage_token_ok(const char *token, uint32_t len)
{
    uint32_t i;
    if (len == 0u || token[0] == '.' || token[0] == '-' ||
        token[len - 1u] == '.' || token[len - 1u] == '-') {
        return 0;
    }
    for (i = 0; i < len; i++) {
        char ch = token[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' ||
              ch == '.')) {
            return 0;
        }
    }
    return 1;
}

static void format_u32(uint32_t value, char out[11])
{
    char reverse[10];
    uint32_t used = 0;
    uint32_t i;
    do {
        reverse[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (i = 0; i < used; i++) {
        out[i] = reverse[used - i - 1u];
    }
    out[used] = '\0';
}

static const char *lifecycle_name(uint32_t lifecycle_class)
{
    switch (lifecycle_class) {
    case CANOPUS_LIFECYCLE_REMOVABLE:
        return "Removable";
    case CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION:
        return "Resident after activation";
    case CANOPUS_LIFECYCLE_ALWAYS_RESIDENT:
        return "Always resident";
    default:
        return "Patch / reboot required";
    }
}

static int32_t append_status(struct canopus_ui_tree_v1 *tree,
                             canopus_ui_node_id key,
                             const char *label, const char *value,
                             uint32_t value_cap)
{
    struct canopus_ui_status_row_props_v1 props;
    props.struct_size = sizeof(props);
    props.label = label;
    props.label_len = bounded_len(label, 32u);
    props.value = value;
    props.value_len = bounded_len(value, value_cap);
    return canopus_ui_status_row(tree, key, &props);
}

static int32_t append_button(struct canopus_ui_tree_v1 *tree,
                             canopus_ui_node_id key, const char *label,
                             uint32_t label_cap, uint32_t event_id,
                             int enabled)
{
    struct canopus_ui_button_props_v1 props;
    props.struct_size = sizeof(props);
    props.label = label;
    props.label_len = bounded_len(label, label_cap);
    props.event_id = event_id;
    props.enabled = enabled ? 1u : 0u;
    return canopus_ui_button(tree, key, &props);
}

static int32_t render_device(struct canopus_manager_native_v1 *native,
                             struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    char revision[11];
    int32_t rc;
    format_u32(model->framework_revision, revision);

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_MAIN_SECTION, "Device");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_TARGET, "Target", model->target_id,
                       sizeof(model->target_id));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_FIRMWARE, "Firmware",
                       model->firmware_version, sizeof(model->firmware_version));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_BUILD, "Build", model->firmware_build,
                       sizeof(model->firmware_build));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_FRAMEWORK, "Framework", revision,
                       sizeof(revision));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = CANOPUS_UI_BUTTON(tree, UI_KEY_MODULES, "Modules",
                           CANOPUS_MANAGER_EVENT_SHOW_MODULES, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = CANOPUS_UI_BUTTON(tree, UI_KEY_INSTALL, "Install staged package",
                           CANOPUS_MANAGER_EVENT_INSTALL,
                           native->stage_token[0] != '\0' && !model->safe_mode);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = CANOPUS_UI_BUTTON(tree, UI_KEY_SAFE_MODE, "Enter safe mode",
                           CANOPUS_MANAGER_EVENT_SAFE_MODE, !model->safe_mode);
    if (rc != CANOPUS_UI_OK) return rc;
    return CANOPUS_UI_END(tree);
}

static int32_t render_module_list(struct canopus_manager_native_v1 *native,
                                  struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    struct canopus_ui_text_props_v1 empty;
    uint32_t i;
    int32_t rc;

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_MODULE_SECTION, "Modules");
    if (rc != CANOPUS_UI_OK) return rc;
    if (model->module_count == 0u) {
        empty.struct_size = sizeof(empty);
        empty.text = "No modules installed";
        empty.text_len = 20u;
        empty.style = 0;
        rc = canopus_ui_text(tree, UI_KEY_MODULE_EMPTY, &empty);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    for (i = 0; i < model->module_count; i++) {
        const struct canopus_manager_module_v1 *module = &model->modules[i];
        const char *label = module->module_id[0] ? module->module_id : "(unnamed)";
        uint32_t cap = module->module_id[0] ? sizeof(module->module_id) : 10u;
        rc = append_button(tree, UI_KEY_MODULE_BASE + i, label, cap,
                           CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE + i, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    rc = CANOPUS_UI_BUTTON(tree, UI_KEY_MODULE_BACK, "Device",
                           CANOPUS_MANAGER_EVENT_SHOW_DEVICE, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    return CANOPUS_UI_END(tree);
}

static int32_t render_module_detail(struct canopus_manager_native_v1 *native,
                                    struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    const struct canopus_manager_module_v1 *module;
    char version[11];
    int32_t rc;
    if (model->selected >= model->module_count) {
        return CANOPUS_UI_ERR_STATE;
    }
    module = &model->modules[model->selected];
    format_u32(module->version, version);

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_DETAIL_SECTION, "Module");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_STATE, "State",
                       canopus_state_name(module->state), 32u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_CLASS, "Lifecycle",
                       lifecycle_name(module->lifecycle_class), 32u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_SIGN, "Signature",
                       module->signature_ok ? "Verified" : "Unsigned / dev", 16u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_VERSION, "Version", version,
                       sizeof(version));
    if (rc != CANOPUS_UI_OK) return rc;

    if (module->state == CANOPUS_STATE_DISABLED ||
        module->state == CANOPUS_STATE_UNLOADED) {
        rc = CANOPUS_UI_BUTTON(tree, UI_KEY_DETAIL_ENABLE, "Enable",
                               CANOPUS_MANAGER_EVENT_ENABLE, !model->safe_mode);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_update(model, model->selected)) {
        rc = CANOPUS_UI_BUTTON(tree, UI_KEY_DETAIL_UPDATE, "Update",
                               CANOPUS_MANAGER_EVENT_UPDATE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_rollback(model, model->selected)) {
        rc = CANOPUS_UI_BUTTON(tree, UI_KEY_DETAIL_ROLLBACK, "Rollback",
                               CANOPUS_MANAGER_EVENT_ROLLBACK, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_disable(model, model->selected)) {
        rc = append_button(tree, UI_KEY_DETAIL_DISABLE,
                           module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
                               "Disable" : "Disable next boot",
                           module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
                               8u : 18u,
                           CANOPUS_MANAGER_EVENT_DISABLE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_remove(model, model->selected)) {
        rc = append_button(tree, UI_KEY_DETAIL_REMOVE,
                           module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
                               "Remove" : "Remove and reboot",
                           module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
                               7u : 18u,
                           CANOPUS_MANAGER_EVENT_REMOVE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    rc = CANOPUS_UI_BUTTON(tree, UI_KEY_DETAIL_BACK, "Modules",
                           CANOPUS_MANAGER_EVENT_SHOW_MODULES, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    return CANOPUS_UI_END(tree);
}

int32_t canopus_manager_native_render(struct canopus_manager_native_v1 *native)
{
    struct canopus_ui_tree_v1 *tree = 0;
    int32_t rc;
    if (native == 0 || native->model == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    rc = canopus_ui_tree_begin(&native->ui, &tree);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    rc = CANOPUS_UI_NAVIGATION_PAGE(tree, UI_KEY_ROOT, "Canopus Manager");
    if (rc == CANOPUS_UI_OK) {
        switch (native->model->view) {
        case CANOPUS_MANAGER_VIEW_DEVICE:
            rc = render_device(native, tree);
            break;
        case CANOPUS_MANAGER_VIEW_MODULE_LIST:
            rc = render_module_list(native, tree);
            break;
        case CANOPUS_MANAGER_VIEW_MODULE_DETAIL:
            rc = render_module_detail(native, tree);
            break;
        default:
            rc = CANOPUS_UI_ERR_STATE;
            break;
        }
    }
    if (rc == CANOPUS_UI_OK) {
        rc = CANOPUS_UI_END(tree);
    }
    if (rc == CANOPUS_UI_OK) {
        return canopus_ui_tree_commit(tree);
    }
    canopus_ui_tree_abort(tree);
    return rc;
}

static int32_t manager_event(void *cookie, uint32_t generation,
                             canopus_ui_node_id key, uint32_t event_id)
{
    struct canopus_manager_native_v1 *native =
        (struct canopus_manager_native_v1 *)cookie;
    struct canopus_manager_model_v1 *model;
    uint32_t index;
    (void)generation;
    (void)key;
    if (native == 0 || native->model == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    model = native->model;
    if (event_id >= CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE &&
        event_id < CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE +
                   CANOPUS_MANAGER_MAX_MODULES) {
        index = event_id - CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE;
        if (canopus_manager_goto(model, CANOPUS_MANAGER_VIEW_MODULE_DETAIL,
                                 index) != 0) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        return canopus_manager_native_render(native);
    }
    switch (event_id) {
    case CANOPUS_MANAGER_EVENT_SHOW_DEVICE:
        if (canopus_manager_goto(model, CANOPUS_MANAGER_VIEW_DEVICE, 0) != 0)
            return CANOPUS_UI_ERR_STATE;
        break;
    case CANOPUS_MANAGER_EVENT_SHOW_MODULES:
        if (canopus_manager_goto(model, CANOPUS_MANAGER_VIEW_MODULE_LIST, 0) != 0)
            return CANOPUS_UI_ERR_STATE;
        break;
    case CANOPUS_MANAGER_EVENT_INSTALL:
        if (native->stage_token[0] == '\0') return CANOPUS_UI_ERR_DISABLED;
        (void)canopus_manager_op_install(model, native->stage_token);
        break;
    case CANOPUS_MANAGER_EVENT_SAFE_MODE:
        (void)canopus_manager_op_safe_mode(model);
        break;
    case CANOPUS_MANAGER_EVENT_ENABLE:
        (void)canopus_manager_op_enable(model, model->selected);
        break;
    case CANOPUS_MANAGER_EVENT_DISABLE:
        (void)canopus_manager_op_disable(model, model->selected);
        break;
    case CANOPUS_MANAGER_EVENT_UPDATE:
        (void)canopus_manager_op_update(model, model->selected);
        break;
    case CANOPUS_MANAGER_EVENT_ROLLBACK:
        (void)canopus_manager_op_rollback(model, model->selected);
        break;
    case CANOPUS_MANAGER_EVENT_REMOVE:
        (void)canopus_manager_op_remove(model, model->selected);
        break;
    default:
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return canopus_manager_native_render(native);
}

int32_t canopus_manager_native_init(
    struct canopus_manager_native_v1 *native,
    struct canopus_manager_model_v1 *model,
    const struct canopus_ui_backend_v1 *backend,
    void *backend_cookie)
{
    int32_t rc;
    if (native == 0 || model == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memset(native, 0, sizeof(*native));
    native->model = model;
    rc = canopus_ui_context_init(&native->ui, backend, backend_cookie,
                                 manager_event, native);
    if (rc != CANOPUS_UI_OK) {
        native->model = 0;
        return rc;
    }
    return canopus_manager_native_render(native);
}

int32_t canopus_manager_native_set_stage_token(
    struct canopus_manager_native_v1 *native, const char *token)
{
    uint32_t len;
    if (native == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (token == 0) {
        native->stage_token[0] = '\0';
        return CANOPUS_UI_OK;
    }
    len = bounded_len(token, sizeof(native->stage_token));
    if (len >= sizeof(native->stage_token) || !stage_token_ok(token, len)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memcpy(native->stage_token, token, len);
    native->stage_token[len] = '\0';
    return CANOPUS_UI_OK;
}
