/*
 * ui_demo.c — portable UI page example (ADR-CAN-010).
 *
 * The module registers one page entry. While it runs, its detail page in the
 * Manager offers "打开界面"; opening it calls on_open, which builds the first
 * tree into the `ui` context the Supervisor hands over. The Supervisor's native
 * backend draws the tree and turns row activations into on_event calls. A second
 * level ("详情页") is a different tree in the same page; on Band 10 Pro the title's
 * back reaches on_back first, so it can pop to the first level instead of closing.
 * Band 11's stock title finishes the page by itself.
 */
#include "canopus_services.h"
#include "canopus_ui.h"

#define UI_MODULE_ID "ui_demo"

enum { EV_TOGGLE = 1, EV_ADD, EV_DETAIL, EV_BACK };
enum { ROUTE_MAIN = 0, ROUTE_DETAIL = 1 };

static struct {
    uint32_t route;
    uint32_t clicks;
    uint32_t checked;
    uint32_t opens;
    uint32_t closes;
} s_state;

static uint32_t text_len(const char *text)
{
    uint32_t n = 0u;
    while (text[n] != '\0') n++;
    return n;
}

static void format_u32(char *out, uint32_t value)
{
    char digits[12];
    uint32_t n = 0u, i;
    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);
    for (i = 0u; i < n; i++) out[i] = digits[n - 1u - i];
    out[n] = '\0';
}

/* Leaves a line the emulator harness can read back. */
static void probe(const struct canopus_context_v1 *ctx, const char *what)
{
    char line[64];
    uint32_t n = 0u, i;
    char number[12];
    if (ctx->fs == 0) return;
    for (i = 0u; what[i] != '\0' && n < 40u; i++) line[n++] = what[i];
    line[n++] = ' ';
    format_u32(number, s_state.route);
    for (i = 0u; number[i] != '\0'; i++) line[n++] = number[i];
    line[n++] = '/';
    format_u32(number, s_state.clicks);
    for (i = 0u; number[i] != '\0'; i++) line[n++] = number[i];
    line[n++] = '/';
    line[n++] = s_state.checked ? '1' : '0';
    line[n++] = '\n';
    (void)ctx->fs->write_all(ctx, "ui_probe.txt", line, n);
}

static int32_t add_status(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                          uint32_t key, const char *label, const char *value)
{
    struct canopus_ui_status_row_props_v1 props = {
        sizeof(props), label, text_len(label), value, text_len(value) };
    return ctx->ui->status_row(tree, key, &props);
}

static int32_t add_action(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *tree,
                          uint32_t key, const char *label, const char *detail, uint32_t event)
{
    struct canopus_ui_action_row_props_v1 props = {
        sizeof(props), label, text_len(label), detail, text_len(detail), event, 1u };
    return ctx->ui->action_row(tree, key, &props);
}

static int32_t render(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui)
{
    struct canopus_ui_tree_v1 *tree = 0;
    char count[12];
    int32_t rc;
    format_u32(count, s_state.clicks);
    rc = ctx->ui->tree_begin(ui, &tree);
    if (rc != CANOPUS_UI_OK) return rc;
    {
        struct canopus_ui_navigation_page_props_v1 page = {
            sizeof(page), "UI 演示", text_len("UI 演示") };
        rc = ctx->ui->navigation_page(tree, 1u, &page);
    }
    if (rc == CANOPUS_UI_OK) {
        struct canopus_ui_section_props_v1 section = {
            sizeof(section), s_state.route == ROUTE_MAIN ? "状态" : "详情",
            s_state.route == ROUTE_MAIN ? text_len("状态") : text_len("详情") };
        rc = ctx->ui->section(tree, 2u, &section);
    }
    if (rc == CANOPUS_UI_OK && s_state.route == ROUTE_MAIN) {
        struct canopus_ui_switch_row_props_v1 toggle = {
            sizeof(toggle), "开关", text_len("开关"), "演示开关", text_len("演示开关"),
            EV_TOGGLE, s_state.checked, 1u };
        struct canopus_ui_component_props_v1 bar = {
            sizeof(bar), "", 0u, "", 0u, 0u, CANOPUS_UI_NODE_FLAG_VISIBLE,
            (int32_t)(s_state.clicks % 11u), 0, 10, 1, 0u };
        rc = add_status(ctx, tree, 3u, "点击次数", count);
        if (rc == CANOPUS_UI_OK) rc = ctx->ui->switch_row(tree, 4u, &toggle);
        if (rc == CANOPUS_UI_OK) rc = ctx->ui->component(tree, 5u, CANOPUS_UI_NODE_PROGRESS, &bar);
        if (rc == CANOPUS_UI_OK) rc = add_action(ctx, tree, 6u, "点击", "点击一次加一", EV_ADD);
        if (rc == CANOPUS_UI_OK) rc = add_action(ctx, tree, 7u, "详情页", "进入第二层", EV_DETAIL);
    } else if (rc == CANOPUS_UI_OK) {
        struct canopus_ui_text_props_v1 note = {
            sizeof(note), "这是第二层页面。", text_len("这是第二层页面。"),
            CANOPUS_UI_TEXT_BODY };
        rc = ctx->ui->text(tree, 3u, &note);
        if (rc == CANOPUS_UI_OK) rc = add_status(ctx, tree, 4u, "累计点击", count);
        if (rc == CANOPUS_UI_OK) rc = add_action(ctx, tree, 5u, "返回", "回到第一层", EV_BACK);
    }
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->end(tree);   /* section */
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->end(tree);   /* page */
    if (rc == CANOPUS_UI_OK) return ctx->ui->tree_commit(tree);
    ctx->ui->tree_abort(tree);
    return rc;
}

static int32_t on_open(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    s_state.route = ROUTE_MAIN;
    s_state.opens++;
    probe(ctx, "open");
    return render(ctx, ui);
}

static int32_t on_event(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                        uint32_t key, uint32_t event_id, void *user_data)
{
    (void)key;
    (void)user_data;
    switch (event_id) {
    case EV_TOGGLE: s_state.checked = !s_state.checked; probe(ctx, "toggle"); break;
    case EV_ADD: s_state.clicks++; probe(ctx, "add"); break;
    case EV_DETAIL: s_state.route = ROUTE_DETAIL; probe(ctx, "detail"); break;
    case EV_BACK: s_state.route = ROUTE_MAIN; probe(ctx, "back"); break;
    default: return CANOPUS_UI_ERR_ARGUMENT;
    }
    return render(ctx, ui);
}

/* Handled (0) while on the second level; from the first level the page closes. */
static int32_t on_back(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    if (s_state.route == ROUTE_MAIN) return 1;
    s_state.route = ROUTE_MAIN;
    probe(ctx, "pop");
    return render(ctx, ui) == CANOPUS_UI_OK ? 0 : 1;
}

static void on_close(const struct canopus_context_v1 *ctx, void *user_data)
{
    (void)user_data;
    s_state.closes++;
    probe(ctx, "close");
}

static const struct canopus_ui_entry_v1 s_entry = {
    sizeof(struct canopus_ui_entry_v1), "两层页面演示",
    on_open, on_event, on_back, on_close, 0,
};

static int32_t demo_prepare(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

static int32_t demo_activate(const struct canopus_context_v1 *ctx)
{
    if (ctx->struct_size < sizeof(struct canopus_context_v1) || ctx->ui_ext == 0) return -1;
    return ctx->ui_ext->set_entry(ctx, &s_entry);
}

static int32_t demo_deactivate(const struct canopus_context_v1 *ctx)
{
    if (ctx->struct_size >= sizeof(struct canopus_context_v1) && ctx->ui_ext != 0) {
        (void)ctx->ui_ext->set_entry(ctx, 0);
    }
    return 0;
}

static int32_t demo_stop(const struct canopus_context_v1 *ctx)
{
    return demo_deactivate(ctx);
}

static int32_t demo_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    0u,
    UI_MODULE_ID,
    "1.0.0",
    "portable-example",
    "portable-thumbv8m",
    demo_prepare,
    demo_activate,
    demo_deactivate,
    demo_stop,
    demo_query,
    0,
    0,
};

__attribute__((constructor)) static void demo_init(const struct canopus_context_v1 *ctx)
{
    if (!canopus_services_has(ctx, CANOPUS_SVC_MODULE, 1)) return;
    (void)ctx->services->module_register(ctx, &s_descriptor);
}
