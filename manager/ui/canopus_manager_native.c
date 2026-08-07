/* canopus_manager_native.c — wearable Manager rendered as semantic native UI. */
#include "canopus_manager_native.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

#define UI_KEY_ROOT                1u
#define UI_KEY_FRAMEWORK          11u
#define UI_KEY_MODULES            12u
#define UI_KEY_INSTALL            14u
#define UI_KEY_SYSTEM_SECTION     20u
#define UI_KEY_MANAGER            21u
#define UI_KEY_FIRMWARE           22u
#define UI_KEY_TARGET             23u
#define UI_KEY_BUILD              24u
#define UI_KEY_ACTIONS_SECTION    25u
#define UI_KEY_ERROR              26u
#define UI_KEY_PERSIST_STAGE      27u
#define UI_KEY_PERSIST_ERRNO      28u
#define UI_KEY_PERSIST_SAVES      29u
#define UI_KEY_MODULE_QUERY_ERROR 32u
#define UI_KEY_MODULE_SECTION     30u
#define UI_KEY_MODULE_EMPTY       31u
#define UI_KEY_MODULE_BASE       100u
#define UI_KEY_DETAIL_SECTION    200u
#define UI_KEY_DETAIL_STATE      201u
#define UI_KEY_DETAIL_CLASS      202u
#define UI_KEY_DETAIL_SIGN       203u
#define UI_KEY_DETAIL_ERROR      204u
#define UI_KEY_DETAIL_UPDATE     210u
#define UI_KEY_DETAIL_ROLLBACK   211u
#define UI_KEY_DETAIL_ENABLE     212u
#define UI_KEY_DETAIL_DISABLE    213u
#define UI_KEY_DETAIL_REMOVE     214u
#define UI_KEY_CONFIRM_SECTION   300u
#define UI_KEY_CONFIRM_MESSAGE   301u
#define UI_KEY_CONFIRM_ACCEPT    302u
#define UI_KEY_CONFIRM_CANCEL    303u

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

static uint32_t append_string(char *out, uint32_t used, uint32_t cap,
                              const char *value, uint32_t value_cap)
{
    uint32_t i = 0;
    if (cap == 0u || used >= cap) {
        return used;
    }
    while (i < value_cap && value != 0 && value[i] != '\0' && used + 1u < cap) {
        out[used++] = value[i++];
    }
    out[used] = '\0';
    return used;
}

static uint32_t append_u32(char *out, uint32_t used, uint32_t cap,
                           uint32_t value)
{
    char reverse[10];
    uint32_t digits = 0;
    uint32_t i;
    do {
        reverse[digits++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (i = 0; i < digits && used + 1u < cap; i++) {
        out[used++] = reverse[digits - i - 1u];
    }
    if (cap != 0u) {
        out[used < cap ? used : cap - 1u] = '\0';
    }
    return used;
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

static int module_is_active(const struct canopus_manager_module_v1 *module)
{
    return module->state == CANOPUS_STATE_ACTIVE ||
           module->state == CANOPUS_STATE_BOOT_RESIDENT;
}

static void format_overview(const struct canopus_manager_model_v1 *model,
                            char out[48])
{
    uint32_t active = 0;
    uint32_t i;
    uint32_t used = 0;
    for (i = 0; i < model->module_count; i++) {
        if (module_is_active(&model->modules[i])) {
            active++;
        }
    }
    out[0] = '\0';
    used = append_u32(out, used, 48u, model->module_count);
    used = append_string(out, used, 48u, " installed / ", 13u);
    used = append_u32(out, used, 48u, active);
    (void)append_string(out, used, 48u, " active", 7u);
}

static void format_framework(uint32_t revision, char out[32])
{
    uint32_t used = 0;
    out[0] = '\0';
    used = append_string(out, used, 32u, "Version 1 / revision ", 21u);
    (void)append_u32(out, used, 32u, revision);
}

/* Render a non-zero supervisor error_code so a silently-failed persistence
 * write or boot restore is visible on the overview instead of looking like a
 * clean REBOOT_REQUIRED. The values mirror canopus_supervisor.h; the wearable
 * UI intentionally does not include the supervisor-private header. */
static void format_error(int32_t code, char out[48])
{
    const char *what = "";
    uint32_t used = 0;
    out[0] = '\0';
    if (code == -5)      what = "package stage failed";
    else if (code == -6) what = "module load failed";
    else if (code == -11) what = "registry corrupt";
    else if (code == -12) what = "registry open failed";
    else if (code == -13) what = "registry write failed";
    else if (code == -14) what = "registry close failed";
    else if (code == -15) what = "temp verify failed";
    else if (code == -16) what = "registry rename failed";
    else if (code == -17) what = "final verify failed";
    else if (code == -18) what = "descriptor missing";
    else if (code == -19) what = "descriptor invalid";
    else if (code == -20) what = "activation failed";
    used = append_string(out, used, 48u, "err ", 4u);
    if (code < 0) {
        out[used++] = '-';
    } else {
        out[used++] = '+';
    }
    used = append_u32(out, used, 48u, (uint32_t)(code < 0 ? -code : code));
    if (what[0] != '\0') {
        used = append_string(out, used, 48u, " ", 1u);
        (void)append_string(out, used, 48u, what, 22u);
    }
}

static const char *persistence_stage_name(uint32_t flags)
{
    switch (flags & 0xFFu) {
    case 0: return "Ready";
    case 1: return "Open temporary file";
    case 2: return "Write temporary file";
    case 3: return "Close temporary file";
    case 4: return "Verify temporary file";
    case 5: return "Rename registry";
    case 6: return "Verify final registry";
    case 7: return "Open registry at boot";
    default: return "Read registry at boot";
    }
}

static void format_diag_u32(uint32_t value, char out[16])
{
    out[0] = '\0';
    (void)append_u32(out, 0u, 16u, value);
}

static void format_module_detail(const struct canopus_manager_module_v1 *module,
                                 char out[64])
{
    uint32_t used = 0;
    /* INSTALLED is the disabled-by-default state; say so explicitly so a
     * freshly installed module is never mistaken for an enabled one. */
    const char *state = module->state == CANOPUS_STATE_INSTALLED
                            ? "installed (disabled)"
                            : canopus_state_name(module->state);
    out[0] = '\0';
    used = append_string(out, used, 64u, state, 32u);
    used = append_string(out, used, 64u, " / version ", 11u);
    (void)append_u32(out, used, 64u, module->version);
}

static int32_t append_status(struct canopus_ui_tree_v1 *tree,
                             canopus_ui_node_id key,
                             const char *label, uint32_t label_cap,
                             const char *value, uint32_t value_cap)
{
    struct canopus_ui_status_row_props_v1 props;
    props.struct_size = sizeof(props);
    props.label = label;
    props.label_len = bounded_len(label, label_cap);
    props.value = value;
    props.value_len = bounded_len(value, value_cap);
    return canopus_ui_status_row(tree, key, &props);
}

static int32_t append_action(struct canopus_ui_tree_v1 *tree,
                             canopus_ui_node_id key,
                             const char *label, uint32_t label_cap,
                             const char *detail, uint32_t detail_cap,
                             uint32_t event_id, int enabled)
{
    struct canopus_ui_action_row_props_v1 props;
    props.struct_size = sizeof(props);
    props.label = label;
    props.label_len = bounded_len(label, label_cap);
    props.detail = detail;
    props.detail_len = bounded_len(detail, detail_cap);
    props.event_id = event_id;
    props.enabled = enabled ? 1u : 0u;
    return canopus_ui_action_row(tree, key, &props);
}

static int32_t append_text(struct canopus_ui_tree_v1 *tree,
                           canopus_ui_node_id key, const char *text,
                           uint32_t text_cap, uint32_t style)
{
    struct canopus_ui_text_props_v1 props;
    props.struct_size = sizeof(props);
    props.text = text;
    props.text_len = bounded_len(text, text_cap);
    props.style = style;
    return canopus_ui_text(tree, key, &props);
}

static int32_t render_device(struct canopus_manager_native_v1 *native,
                             struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    char framework[32];
    char modules[48];
    int32_t rc;
    format_framework(model->framework_revision, framework);
    format_overview(model, modules);

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_SYSTEM_SECTION, "System information");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_FRAMEWORK, "Framework", 10u,
                       framework, sizeof(framework));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_MANAGER, "Manager", 8u,
                       "Native UI ABI 1.4", 18u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_FIRMWARE, "Firmware", 9u,
                       model->firmware_version, sizeof(model->firmware_version));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_BUILD, "Build", 6u,
                       model->firmware_build, sizeof(model->firmware_build));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_TARGET, "Target", 7u,
                       model->target_id, sizeof(model->target_id));
    if (rc != CANOPUS_UI_OK) return rc;
    if (model->error_code != 0 || model->supervisor_flags != 0u) {
        char errtext[48];
        char errno_text[16];
        char saves_text[16];
        uint32_t error = (model->supervisor_flags >> 8) & 0xFFFFu;
        uint32_t saves = model->supervisor_flags >> 24;
        if (model->error_code != 0) {
            format_error(model->error_code, errtext);
            rc = append_status(tree, UI_KEY_ERROR, "Error", 6u,
                               errtext, sizeof(errtext));
            if (rc != CANOPUS_UI_OK) return rc;
        }
        rc = append_status(tree, UI_KEY_PERSIST_STAGE, "Registry", 9u,
                           persistence_stage_name(model->supervisor_flags), 28u);
        if (rc != CANOPUS_UI_OK) return rc;
        format_diag_u32(error, errno_text);
        rc = append_status(tree, UI_KEY_PERSIST_ERRNO, "Filesystem errno", 17u,
                           errno_text, sizeof(errno_text));
        if (rc != CANOPUS_UI_OK) return rc;
        format_diag_u32(saves, saves_text);
        rc = append_status(tree, UI_KEY_PERSIST_SAVES, "Verified saves", 15u,
                           saves_text, sizeof(saves_text));
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (model->module_query_error != 0) {
        char query_error[48];
        uint32_t used = 0u;
        query_error[0] = '\0';
        used = append_string(query_error, used, sizeof(query_error),
                             "slot ", 5u);
        used = append_u32(query_error, used, sizeof(query_error),
                          model->module_query_error_slot);
        used = append_string(query_error, used, sizeof(query_error),
                             " query failed ", 14u);
        (void)append_u32(query_error, used, sizeof(query_error),
                         (uint32_t)(-model->module_query_error));
        rc = append_status(tree, UI_KEY_MODULE_QUERY_ERROR,
                           "Module query", 13u,
                           query_error, sizeof(query_error));
        if (rc != CANOPUS_UI_OK) return rc;
    }
    rc = CANOPUS_UI_END(tree);
    if (rc != CANOPUS_UI_OK) return rc;

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_ACTIONS_SECTION, "Manage");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_action(tree, UI_KEY_MODULES, "Modules", 8u,
                       modules, sizeof(modules),
                       CANOPUS_MANAGER_EVENT_SHOW_MODULES, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_action(tree, UI_KEY_INSTALL, "Install package", 16u,
                       native->stage_token[0] != '\0' ?
                           "Verified staged package" : "No staged package",
                       24u, CANOPUS_MANAGER_EVENT_INSTALL,
                       native->stage_token[0] != '\0' && !model->safe_mode);
    if (rc != CANOPUS_UI_OK) return rc;
    return CANOPUS_UI_END(tree);
}

static int32_t render_module_list(struct canopus_manager_native_v1 *native,
                                  struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    uint32_t i;
    int32_t rc;

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_MODULE_SECTION, "Installed modules");
    if (rc != CANOPUS_UI_OK) return rc;
    if (model->module_count == 0u) {
        rc = append_text(tree, UI_KEY_MODULE_EMPTY,
                         "No external modules installed", 30u, 0u);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    for (i = 0; i < model->module_count; i++) {
        const struct canopus_manager_module_v1 *module = &model->modules[i];
        const char *label = module->module_id[0] ? module->module_id : "Unnamed module";
        uint32_t label_cap = module->module_id[0] ? sizeof(module->module_id) : 15u;
        char detail[64];
        format_module_detail(module, detail);
        rc = append_action(tree, UI_KEY_MODULE_BASE + i, label, label_cap,
                           detail, sizeof(detail),
                           CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE + i, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    return CANOPUS_UI_END(tree);
}

static int32_t render_module_detail(struct canopus_manager_native_v1 *native,
                                    struct canopus_ui_tree_v1 *tree)
{
    struct canopus_manager_model_v1 *model = native->model;
    const struct canopus_manager_module_v1 *module;
    char state[64];
    int32_t rc;
    if (model->selected >= model->module_count) {
        return CANOPUS_UI_ERR_STATE;
    }
    module = &model->modules[model->selected];
    format_module_detail(module, state);

    rc = CANOPUS_UI_SECTION(tree, UI_KEY_DETAIL_SECTION, "Module status");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_STATE, "Status", 7u,
                       state, sizeof(state));
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_CLASS, "Lifecycle", 10u,
                       lifecycle_name(module->lifecycle_class), 32u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_status(tree, UI_KEY_DETAIL_SIGN, "Signature", 10u,
                       module->signature_ok ? "Verified" : "Unsigned / developer",
                       21u);
    if (rc != CANOPUS_UI_OK) return rc;
    if (module->activation_error != 0) {
        char error[48];
        format_error(module->activation_error, error);
        rc = append_status(tree, UI_KEY_DETAIL_ERROR, "Module error", 13u,
                           error, sizeof(error));
        if (rc != CANOPUS_UI_OK) return rc;
    }

    if (canopus_manager_can_enable(model, model->selected)) {
        rc = append_action(tree, UI_KEY_DETAIL_ENABLE, "Enable", 7u,
                           "Applies after reboot", 21u,
                           CANOPUS_MANAGER_EVENT_ENABLE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_update(model, model->selected)) {
        rc = append_action(tree, UI_KEY_DETAIL_UPDATE, "Update", 7u,
                           "Install verified replacement", 29u,
                           CANOPUS_MANAGER_EVENT_UPDATE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_rollback(model, model->selected)) {
        rc = append_action(tree, UI_KEY_DETAIL_ROLLBACK, "Rollback", 9u,
                           "Restore previous version", 25u,
                           CANOPUS_MANAGER_EVENT_ROLLBACK, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    /* CAN-P0-005 revision: every lifecycle operation is next-boot. */
    if (canopus_manager_can_disable(model, model->selected)) {
        rc = append_action(tree, UI_KEY_DETAIL_DISABLE, "Disable", 8u,
                           "Applies after reboot", 21u,
                           CANOPUS_MANAGER_EVENT_DISABLE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    if (canopus_manager_can_remove(model, model->selected)) {
        rc = append_action(tree, UI_KEY_DETAIL_REMOVE, "Remove", 7u,
                           "Deleted after reboot", 21u,
                           CANOPUS_MANAGER_EVENT_REMOVE, 1);
        if (rc != CANOPUS_UI_OK) return rc;
    }
    return CANOPUS_UI_END(tree);
}

static const char *confirmation_message(uint32_t event_id)
{
    switch (event_id) {
    case CANOPUS_MANAGER_EVENT_ACTIVATE:
        return "Activate this module now? This runs third-party code.";
    case CANOPUS_MANAGER_EVENT_INSTALL:
        return "Install the verified staged package?";
    case CANOPUS_MANAGER_EVENT_ENABLE:
        return "Enable this module on next boot?";
    case CANOPUS_MANAGER_EVENT_DISABLE:
        return "Disable this module after next reboot?";
    case CANOPUS_MANAGER_EVENT_ROLLBACK:
        return "Restore the previous verified module version?";
    case CANOPUS_MANAGER_EVENT_REMOVE:
        return "Remove this module after reboot? Cannot be undone.";
    default:
        return "Continue with this operation?";
    }
}

static int32_t render_confirmation(struct canopus_manager_native_v1 *native,
                                   struct canopus_ui_tree_v1 *tree)
{
    const char *message = confirmation_message(native->confirm_event);
    int32_t rc;
    rc = CANOPUS_UI_SECTION(tree, UI_KEY_CONFIRM_SECTION, "Confirm action");
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_text(tree, UI_KEY_CONFIRM_MESSAGE, message, 64u, 2u);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_action(tree, UI_KEY_CONFIRM_ACCEPT, "Confirm", 8u,
                       "Apply this operation", 21u,
                       CANOPUS_MANAGER_EVENT_CONFIRM, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    rc = append_action(tree, UI_KEY_CONFIRM_CANCEL, "Cancel", 7u,
                       "Return without changes", 23u,
                       CANOPUS_MANAGER_EVENT_CANCEL, 1);
    if (rc != CANOPUS_UI_OK) return rc;
    return CANOPUS_UI_END(tree);
}

int32_t canopus_manager_native_render(struct canopus_manager_native_v1 *native)
{
    struct canopus_ui_navigation_page_props_v1 page;
    struct canopus_ui_style_v1 title_style;
    struct canopus_ui_tree_v1 *tree = 0;
    const char *title = "Canopus";
    uint32_t title_cap = 8u;
    int32_t rc;
    if (native == 0 || native->model == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (native->confirm_event != 0u) {
        title = "Confirm";
        title_cap = 8u;
    } else if (native->model->view == CANOPUS_MANAGER_VIEW_MODULE_LIST) {
        title = "Modules";
        title_cap = 8u;
    } else if (native->model->view == CANOPUS_MANAGER_VIEW_MODULE_DETAIL &&
               native->model->selected < native->model->module_count) {
        title = native->model->modules[native->model->selected].module_id;
        title_cap = sizeof(native->model->modules[0].module_id);
    }
    rc = canopus_ui_tree_begin(&native->ui, &tree);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    page.struct_size = sizeof(page);
    page.title = title;
    page.title_len = bounded_len(title, title_cap);
    rc = canopus_ui_navigation_page(tree, UI_KEY_ROOT, &page);
    if (rc == CANOPUS_UI_OK) {
        canopus_memset(&title_style, 0, sizeof(title_style));
        title_style.text_style = CANOPUS_UI_TEXT_TITLE;
        title_style.corner_radius = -1;
        title_style.border_width = -1;
        rc = canopus_ui_node_set_style(tree, UI_KEY_ROOT, &title_style);
    }
    if (rc == CANOPUS_UI_OK) {
        if (native->confirm_event != 0u) {
            rc = render_confirmation(native, tree);
        } else {
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

static uint32_t execute_event(struct canopus_manager_native_v1 *native,
                              uint32_t event_id)
{
    struct canopus_manager_model_v1 *model = native->model;
    switch (event_id) {
    case CANOPUS_MANAGER_EVENT_ACTIVATE:
        return canopus_manager_op_activate(model, model->selected);
    case CANOPUS_MANAGER_EVENT_INSTALL:
        if (native->stage_token[0] == '\0') return CANOPUS_RESULT_DISALLOWED;
        return canopus_manager_op_install(model, native->stage_token);
    case CANOPUS_MANAGER_EVENT_ENABLE:
        return canopus_manager_op_enable(model, model->selected);
    case CANOPUS_MANAGER_EVENT_DISABLE:
        return canopus_manager_op_disable(model, model->selected);
    case CANOPUS_MANAGER_EVENT_UPDATE:
        return canopus_manager_op_update(model, model->selected);
    case CANOPUS_MANAGER_EVENT_ROLLBACK:
        return canopus_manager_op_rollback(model, model->selected);
    case CANOPUS_MANAGER_EVENT_REMOVE:
        return canopus_manager_op_remove(model, model->selected);
    default:
        return CANOPUS_RESULT_FAILED;
    }
}

static int event_needs_confirmation(uint32_t event_id)
{
    return event_id == CANOPUS_MANAGER_EVENT_ACTIVATE ||
           event_id == CANOPUS_MANAGER_EVENT_INSTALL ||
           event_id == CANOPUS_MANAGER_EVENT_ENABLE ||
           event_id == CANOPUS_MANAGER_EVENT_DISABLE ||
           event_id == CANOPUS_MANAGER_EVENT_ROLLBACK ||
           event_id == CANOPUS_MANAGER_EVENT_REMOVE;
}

static int32_t refresh_model(struct canopus_manager_native_v1 *native)
{
    if (native->refresh == 0) {
        return CANOPUS_UI_OK;
    }
    return native->refresh(native->refresh_cookie);
}

static int32_t route_or_render(struct canopus_manager_native_v1 *native,
                               uint32_t route)
{
    if (native->route != 0) {
        if (route == 0u) {
            return CANOPUS_UI_ERR_STATE;
        }
        return native->route(native->route_cookie, native, route);
    }
    return canopus_manager_native_render(native);
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
        return route_or_render(native, CANOPUS_MANAGER_ROUTE_MODULE_DETAIL);
    }
    switch (event_id) {
    case CANOPUS_MANAGER_EVENT_SHOW_DEVICE:
        native->confirm_event = 0u;
        if (canopus_manager_goto(model, CANOPUS_MANAGER_VIEW_DEVICE, 0) != 0)
            return CANOPUS_UI_ERR_STATE;
        return route_or_render(native, CANOPUS_MANAGER_ROUTE_OVERVIEW);
    case CANOPUS_MANAGER_EVENT_SHOW_MODULES:
        native->confirm_event = 0u;
        if (canopus_manager_goto(model, CANOPUS_MANAGER_VIEW_MODULE_LIST, 0) != 0)
            return CANOPUS_UI_ERR_STATE;
        return route_or_render(native, CANOPUS_MANAGER_ROUTE_MODULES);
    case CANOPUS_MANAGER_EVENT_CANCEL:
        if (native->confirm_event == 0u) return CANOPUS_UI_ERR_STATE;
        native->confirm_event = 0u;
        if (canopus_manager_goto(model, native->confirm_return_view,
                                 model->selected) != 0)
            return CANOPUS_UI_ERR_STATE;
        return canopus_manager_native_render(native);
    case CANOPUS_MANAGER_EVENT_CONFIRM:
        if (native->confirm_event == 0u) return CANOPUS_UI_ERR_STATE;
        event_id = native->confirm_event;
        native->confirm_event = 0u;
        (void)execute_event(native, event_id);
        /* Re-read the device model so the page reflects the committed state
         * (e.g. enabled/disabled-next-boot) instead of the stale snapshot. */
        (void)refresh_model(native);
        if (canopus_manager_goto(model, native->confirm_return_view,
                                 model->selected) != 0)
            return CANOPUS_UI_ERR_STATE;
        return canopus_manager_native_render(native);
    case CANOPUS_MANAGER_EVENT_INSTALL:
        if (native->stage_token[0] == '\0') return CANOPUS_UI_ERR_DISABLED;
        /* fall through */
    case CANOPUS_MANAGER_EVENT_ACTIVATE:
    case CANOPUS_MANAGER_EVENT_ENABLE:
    case CANOPUS_MANAGER_EVENT_DISABLE:
    case CANOPUS_MANAGER_EVENT_ROLLBACK:
    case CANOPUS_MANAGER_EVENT_REMOVE:
        native->confirm_return_view = model->view;
        native->confirm_event = event_id;
        return canopus_manager_native_render(native);
    case CANOPUS_MANAGER_EVENT_UPDATE:
        (void)execute_event(native, event_id);
        (void)refresh_model(native);
        return canopus_manager_native_render(native);
    default:
        if (!event_needs_confirmation(event_id)) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        break;
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

void canopus_manager_native_set_router(
    struct canopus_manager_native_v1 *native,
    canopus_manager_native_route_v1 route,
    void *route_cookie)
{
    if (native == 0) {
        return;
    }
    native->route = route;
    native->route_cookie = route_cookie;
}

void canopus_manager_native_set_refresh(
    struct canopus_manager_native_v1 *native,
    canopus_manager_native_refresh_v1 refresh,
    void *refresh_cookie)
{
    if (native == 0) {
        return;
    }
    native->refresh = refresh;
    native->refresh_cookie = refresh_cookie;
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
