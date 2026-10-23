/*
 * ui_gallery.c — one sub-page per widget kind of ctx->ui (ADR-CAN-010), for the
 * renderer: form controls, a progress bar, text, image, a dialog,
 * per-node style and layout, and a page that closes itself. The title's back
 * returns from a sub-page to the list. Every event leaves one line in
 * ui_probe.txt for the emulator harness.
 */
#include "canopus_module_entry.h"
#include "canopus_ui.h"

#define GALLERY_MODULE_ID "ui_gallery"

enum route { R_MAIN, R_FORM, R_PROGRESS, R_TEXT, R_IMAGE, R_DIALOG, R_STYLE };
enum event {
    EV_GO_FORM = 1, EV_GO_PROGRESS, EV_GO_TEXT, EV_GO_IMAGE, EV_GO_DIALOG, EV_GO_STYLE,
    EV_SELF_CLOSE, EV_BACK,
    EV_CHECK_A = 20, EV_CHECK_B, EV_RADIO_0 = 30, EV_RADIO_1, EV_RADIO_2, EV_BUTTON = 40, EV_SWITCH,
    EV_PROGRESS_UP = 50, EV_PROGRESS_DOWN,
    EV_DIALOG_OPEN = 60, EV_DIALOG_OK, EV_DIALOG_CANCEL,
    EV_STYLE_TAP = 70
};

static struct {
    uint32_t route;
    uint32_t check_a, check_b, radio, sw;
    int32_t progress;
    uint32_t dialog;
} s_state;

static uint32_t text_len(const char *text)
{
    uint32_t n = 0u;
    while (text[n] != '\0') n++;
    return n;
}

static uint32_t put_u32(char *out, uint32_t value)
{
    char digits[12];
    uint32_t n = 0u, i;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (i = 0u; i < n; i++) out[i] = digits[n - 1u - i];
    return n;
}

/* Appends "<what> <route>/<value>" to ui_probe.txt. */
static void probe(const struct canopus_context_v1 *ctx, const char *what, uint32_t value)
{
    char line[48];
    uint32_t n = 0u, i;
    int32_t fd;
    if (ctx->fs == 0) return;
    for (i = 0u; what[i] != '\0' && n < 24u; i++) line[n++] = what[i];
    line[n++] = ' ';
    n += put_u32(line + n, s_state.route);
    line[n++] = '/';
    n += put_u32(line + n, value);
    line[n++] = '\n';
    fd = ctx->fs->open(ctx, "ui_probe.txt", CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | CANOPUS_FS_CREATE);
    if (fd < 0) return;
    (void)ctx->fs->write(ctx, fd, line, n);
    (void)ctx->fs->close(ctx, fd);
}

#define OK CANOPUS_UI_OK

static int32_t action(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                      uint32_t key, const char *label, const char *detail, uint32_t event)
{
    struct canopus_ui_action_row_props_v1 props = {
        sizeof(props), label, text_len(label), detail, text_len(detail), event, 1u };
    return ctx->ui->action_row(tree, key, &props);
}

static int32_t status(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                      uint32_t key, const char *label, const char *value)
{
    struct canopus_ui_status_row_props_v1 props = {
        sizeof(props), label, text_len(label), value, text_len(value) };
    return ctx->ui->status_row(tree, key, &props);
}

static int32_t text(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                    uint32_t key, const char *body, uint32_t style)
{
    struct canopus_ui_text_props_v1 props = { sizeof(props), body, text_len(body), style };
    return ctx->ui->text(tree, key, &props);
}

static int32_t component(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                         uint32_t key, uint16_t type, const char *primary, const char *secondary,
                         uint32_t event, uint32_t flags, int32_t value, int32_t minimum,
                         int32_t maximum, uint32_t resource)
{
    struct canopus_ui_component_props_v1 props = {
        sizeof(props), primary, text_len(primary), secondary, text_len(secondary), event, flags,
        value, minimum, maximum, 1, resource };
    return ctx->ui->component(tree, key, type, &props);
}

static int32_t back_row(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                        uint32_t key)
{
    return action(ctx, tree, key, "返回", "回到列表", EV_BACK);
}

static int32_t build_form(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    const uint32_t on = CANOPUS_UI_NODE_FLAG_ENABLED | CANOPUS_UI_NODE_FLAG_VISIBLE;
    struct canopus_ui_button_props_v1 button = { sizeof(button), "按钮", text_len("按钮"), EV_BUTTON, 1u };
    struct canopus_ui_switch_row_props_v1 toggle = {
        sizeof(toggle), "开关", text_len("开关"), "开关行", text_len("开关行"), EV_SWITCH, s_state.sw, 1u };
    int32_t rc = status(ctx, tree, 10u, "状态行", "值");
    if (rc == OK) rc = ctx->ui->switch_row(tree, 11u, &toggle);
    if (rc == OK) rc = component(ctx, tree, 12u, CANOPUS_UI_NODE_CHECKBOX, "复选 A", "", EV_CHECK_A,
                                 on | (s_state.check_a ? CANOPUS_UI_NODE_FLAG_CHECKED : 0u), 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 13u, CANOPUS_UI_NODE_CHECKBOX, "复选 B", "", EV_CHECK_B,
                                 on | (s_state.check_b ? CANOPUS_UI_NODE_FLAG_CHECKED : 0u), 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 14u, CANOPUS_UI_NODE_RADIO_ROW, "单选 0", "第一项", EV_RADIO_0,
                                 on | (s_state.radio == 0u ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u), 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 15u, CANOPUS_UI_NODE_RADIO_ROW, "单选 1", "第二项", EV_RADIO_1,
                                 on | (s_state.radio == 1u ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u), 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 16u, CANOPUS_UI_NODE_RADIO_ROW, "单选 2", "第三项", EV_RADIO_2,
                                 on | (s_state.radio == 2u ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u), 0, 0, 0, 0u);
    if (rc == OK) rc = ctx->ui->button(tree, 17u, &button);
    return rc;
}

static int32_t build_progress(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    char number[12];
    int32_t rc;
    number[put_u32(number, (uint32_t)s_state.progress)] = '\0';
    rc = status(ctx, tree, 10u, "进度值", number);
    if (rc == OK) rc = component(ctx, tree, 11u, CANOPUS_UI_NODE_PROGRESS, "", "", 0u,
                                 CANOPUS_UI_NODE_FLAG_VISIBLE, s_state.progress, 0, 10, 0u);
    if (rc == OK) rc = action(ctx, tree, 12u, "加一", "进度 +1", EV_PROGRESS_UP);
    if (rc == OK) rc = action(ctx, tree, 13u, "减一", "进度 -1", EV_PROGRESS_DOWN);
    return rc;
}

static int32_t build_text(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    int32_t rc = text(ctx, tree, 10u, "正文 BODY", CANOPUS_UI_TEXT_BODY);
    if (rc == OK) rc = text(ctx, tree, 11u, "标题 TITLE", CANOPUS_UI_TEXT_TITLE);
    if (rc == OK) rc = text(ctx, tree, 12u, "说明 DESCRIPTION", CANOPUS_UI_TEXT_DESCRIPTION);
    if (rc == OK) rc = text(ctx, tree, 13u, "警告 WARNING", CANOPUS_UI_TEXT_WARNING);
    if (rc == OK) rc = component(ctx, tree, 14u, CANOPUS_UI_NODE_RICH_TEXT, "富文本 RICH", "", 0u,
                                 CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 15u, CANOPUS_UI_NODE_DIVIDER, "", "", 0u,
                                 CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0u);
    if (rc == OK) rc = component(ctx, tree, 16u, CANOPUS_UI_NODE_SPACER, "", "", 0u,
                                 CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0u);
    if (rc == OK) rc = status(ctx, tree, 17u, "分隔线之后", "状态行");
    return rc;
}

static int32_t build_image(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    int32_t rc = component(ctx, tree, 10u, CANOPUS_UI_NODE_IMAGE, "/data/canopus/manager_icon.bin", "",
                           0u, CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 1u);
    layout.width = 96;
    layout.height = 96;
    if (rc == OK) rc = ctx->ui->node_set_layout(tree, 10u, &layout);
    if (rc == OK) rc = status(ctx, tree, 11u, "图片", "Manager 图标");
    return rc;
}

static int32_t build_dialog(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    struct canopus_ui_button_props_v1 cancel = { sizeof(cancel), "取消", text_len("取消"), EV_DIALOG_CANCEL, 1u };
    struct canopus_ui_button_props_v1 confirm = { sizeof(confirm), "确认", text_len("确认"), EV_DIALOG_OK, 1u };
    int32_t rc;
    if (s_state.dialog == 0u) return action(ctx, tree, 10u, "弹出对话框", "对话框", EV_DIALOG_OPEN);
    rc = component(ctx, tree, 10u, CANOPUS_UI_NODE_DIALOG, "确认操作", "", 0u,
                   CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0u);
    if (rc == OK) rc = text(ctx, tree, 11u, "要继续吗？", CANOPUS_UI_TEXT_BODY);
    if (rc == OK) rc = ctx->ui->button(tree, 12u, &cancel);
    if (rc == OK) rc = ctx->ui->button(tree, 13u, &confirm);
    if (rc == OK) rc = ctx->ui->end(tree);   /* dialog */
    return rc;
}

static int32_t build_style(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    struct canopus_ui_style_v1 filled = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_style_v1 danger = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_layout_v1 narrow = CANOPUS_UI_LAYOUT_DEFAULT;
    int32_t rc = action(ctx, tree, 10u, "填充样式", "FILLED", EV_STYLE_TAP);
    filled.variant = CANOPUS_UI_VARIANT_FILLED;
    danger.variant = CANOPUS_UI_VARIANT_DESTRUCTIVE;
    narrow.width = 120;
    if (rc == OK) rc = ctx->ui->node_set_style(tree, 10u, &filled);
    if (rc == OK) rc = ctx->ui->node_set_layout(tree, 10u, &narrow);
    if (rc == OK) rc = action(ctx, tree, 11u, "危险样式", "DESTRUCTIVE", EV_STYLE_TAP);
    if (rc == OK) rc = ctx->ui->node_set_style(tree, 11u, &danger);
    return rc;
}

static const char *route_title(uint32_t route)
{
    switch (route) {
    case R_FORM: return "表单控件";
    case R_PROGRESS: return "进度条";
    case R_TEXT: return "文本";
    case R_IMAGE: return "图片";
    case R_DIALOG: return "对话框";
    case R_STYLE: return "样式与布局";
    default: return "控件演示";
    }
}

static int32_t build_main(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree)
{
    int32_t rc = action(ctx, tree, 10u, "表单控件", "开关、复选、单选、按钮", EV_GO_FORM);
    if (rc == OK) rc = action(ctx, tree, 11u, "进度条", "进度条", EV_GO_PROGRESS);
    if (rc == OK) rc = action(ctx, tree, 12u, "文本", "文本、富文本、分隔线", EV_GO_TEXT);
    if (rc == OK) rc = action(ctx, tree, 13u, "图片", "图片", EV_GO_IMAGE);
    if (rc == OK) rc = action(ctx, tree, 14u, "对话框", "页面内的对话框", EV_GO_DIALOG);
    if (rc == OK) rc = action(ctx, tree, 15u, "样式与布局", "样式、布局", EV_GO_STYLE);
    if (rc == OK) rc = action(ctx, tree, 16u, "关闭本页", "模块自己关闭页面", EV_SELF_CLOSE);
    return rc;
}

static int32_t render(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui)
{
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 page;
    const char *title = route_title(s_state.route);
    int32_t rc = ctx->ui->tree_begin(ui, &tree);
    if (rc != OK) return rc;
    page.struct_size = sizeof(page);
    page.title = title;
    page.title_len = text_len(title);
    rc = ctx->ui->navigation_page(tree, 1u, &page);
    switch (s_state.route) {
    case R_FORM: if (rc == OK) rc = build_form(ctx, tree); break;
    case R_PROGRESS: if (rc == OK) rc = build_progress(ctx, tree); break;
    case R_TEXT: if (rc == OK) rc = build_text(ctx, tree); break;
    case R_IMAGE: if (rc == OK) rc = build_image(ctx, tree); break;
    case R_DIALOG: if (rc == OK) rc = build_dialog(ctx, tree); break;
    case R_STYLE: if (rc == OK) rc = build_style(ctx, tree); break;
    default: if (rc == OK) rc = build_main(ctx, tree); break;
    }
    if (rc == OK && s_state.route != R_MAIN && !(s_state.route == R_DIALOG && s_state.dialog != 0u)) {
        rc = back_row(ctx, tree, 90u);
    }
    if (rc == OK) rc = ctx->ui->end(tree);   /* page */
    if (rc == OK) {
        rc = ctx->ui->tree_commit(tree);
        if (rc != OK) probe(ctx, "commitfail", (uint32_t)rc);
        return rc;
    }
    probe(ctx, "renderfail", (uint32_t)rc);
    ctx->ui->tree_abort(tree);
    return rc;
}

static int32_t on_open(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    s_state.route = R_MAIN;
    s_state.dialog = 0u;
    probe(ctx, "open", 0u);
    return render(ctx, ui);
}

static int32_t on_event(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                        const struct canopus_ui_event_v1 *event, void *user_data)
{
    uint32_t event_id = event->event_id;
    (void)user_data;
    if (event->type != CANOPUS_UI_EVENT_ACTIVATE &&
        event->type != CANOPUS_UI_EVENT_VALUE_CHANGED) return CANOPUS_UI_OK;
    switch (event_id) {
    case EV_GO_FORM: s_state.route = R_FORM; break;
    case EV_GO_PROGRESS: s_state.route = R_PROGRESS; break;
    case EV_GO_TEXT: s_state.route = R_TEXT; break;
    case EV_GO_IMAGE: s_state.route = R_IMAGE; break;
    case EV_GO_DIALOG: s_state.route = R_DIALOG; s_state.dialog = 0u; break;
    case EV_GO_STYLE: s_state.route = R_STYLE; break;
    case EV_BACK: s_state.route = R_MAIN; break;
    case EV_SELF_CLOSE:
        probe(ctx, "selfclose", 0u);
        return ctx->ui->close_page(ctx);
    case EV_CHECK_A: s_state.check_a = !s_state.check_a; probe(ctx, "checkA", s_state.check_a); break;
    case EV_CHECK_B: s_state.check_b = !s_state.check_b; probe(ctx, "checkB", s_state.check_b); break;
    case EV_RADIO_0: case EV_RADIO_1: case EV_RADIO_2:
        s_state.radio = event_id - EV_RADIO_0;
        probe(ctx, "radio", s_state.radio);
        break;
    case EV_SWITCH: s_state.sw = !s_state.sw; probe(ctx, "switch", s_state.sw); break;
    case EV_BUTTON: probe(ctx, "button", 0u); break;
    case EV_PROGRESS_UP:
        if (s_state.progress < 10) s_state.progress++;
        probe(ctx, "progress", (uint32_t)s_state.progress);
        break;
    case EV_PROGRESS_DOWN:
        if (s_state.progress > 0) s_state.progress--;
        probe(ctx, "progress", (uint32_t)s_state.progress);
        break;
    case EV_DIALOG_OPEN: s_state.dialog = 1u; probe(ctx, "dialogopen", 0u); break;
    case EV_DIALOG_OK: s_state.dialog = 0u; probe(ctx, "dialogok", 0u); break;
    case EV_DIALOG_CANCEL: s_state.dialog = 0u; probe(ctx, "dialogcancel", 0u); break;
    case EV_STYLE_TAP: probe(ctx, "styletap", 0u); break;
    default: return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (event_id <= EV_BACK) probe(ctx, "route", s_state.route);
    return render(ctx, ui);
}

/* Handled (0) on a sub-page, which goes back to the list (a dialog closes with
 * it); from the list the page closes. */
static int32_t on_back(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    probe(ctx, "back", s_state.dialog);
    if (s_state.route == R_MAIN) return 1;
    s_state.route = R_MAIN;
    s_state.dialog = 0u;
    return render(ctx, ui) == OK ? 0 : 1;
}

static void on_close(const struct canopus_context_v1 *ctx, void *user_data)
{
    (void)user_data;
    probe(ctx, "close", 0u);
}

static const struct canopus_ui_entry_v1 s_entry = {
    sizeof(struct canopus_ui_entry_v1), "控件演示", on_open, on_event, on_back, on_close, 0,
};

static int32_t gallery_prepare(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

static int32_t gallery_activate(const struct canopus_context_v1 *ctx)
{
    if (ctx->ui == 0) return -1;
    return ctx->ui->set_entry(ctx, &s_entry);
}

static int32_t gallery_deactivate(const struct canopus_context_v1 *ctx)
{
    if (ctx->ui != 0) {
        (void)ctx->ui->set_entry(ctx, 0);
    }
    return 0;
}

static int32_t gallery_stop(const struct canopus_context_v1 *ctx)
{
    return gallery_deactivate(ctx);
}

static int32_t gallery_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    0u,
    GALLERY_MODULE_ID,
    "1.0.0",
    "ui-gallery",
    CANOPUS_TARGET_ID,
    gallery_prepare,
    gallery_activate,
    gallery_deactivate,
    gallery_stop,
    gallery_query,
    0,
    0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
