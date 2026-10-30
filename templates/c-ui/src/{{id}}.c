/* {{id}} - Canopus module with one UI page: the Manager's "打开界面" opens it. */
#include "canopus_module_entry.h"
#include "canopus_ui.h"

enum { EV_ADD = 1, EV_TOGGLE };

static struct { uint32_t clicks, checked; } s_state;

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

/* Builds the whole page again and commits it; the host redraws only what changed. */
static int32_t render(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui)
{
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 page = { sizeof(page), "{{id}}", text_len("{{id}}") };
    struct canopus_ui_action_row_props_v1 add = {
        sizeof(add), "点击", text_len("点击"), "点击一次加一", text_len("点击一次加一"), EV_ADD, 1u };
    struct canopus_ui_switch_row_props_v1 toggle = {
        sizeof(toggle), "开关", text_len("开关"), "", 0u, EV_TOGGLE, s_state.checked, 1u };
    char count[12];
    struct canopus_ui_status_row_props_v1 status = { sizeof(status), "点击次数", text_len("点击次数"), count, 0u };
    int32_t rc = ctx->ui->tree_begin(ui, &tree);

    if (rc != CANOPUS_UI_OK) return rc;
    format_u32(count, s_state.clicks);
    status.value_len = text_len(count);
    rc = ctx->ui->navigation_page(tree, 1u, &page);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->status_row(tree, 2u, &status);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->action_row(tree, 3u, &add);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->switch_row(tree, 4u, &toggle);
    if (rc == CANOPUS_UI_OK) rc = ctx->ui->end(tree);   /* page */
    if (rc == CANOPUS_UI_OK) return ctx->ui->tree_commit(tree);
    ctx->ui->tree_abort(tree);
    return rc;
}

/* Runs on the UI task: the first tree must be committed before it returns. */
static int32_t on_open(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                       void *user_data)
{
    (void)user_data;
    return render(ctx, ui);
}

static int32_t on_event(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                        const struct canopus_ui_event_v1 *event, void *user_data)
{
    (void)user_data;
    if (event->type != CANOPUS_UI_EVENT_ACTIVATE &&
        event->type != CANOPUS_UI_EVENT_VALUE_CHANGED) return CANOPUS_UI_OK;
    if (event->event_id == EV_ADD) s_state.clicks++;
    else if (event->event_id == EV_TOGGLE) s_state.checked = !s_state.checked;
    else return CANOPUS_UI_ERR_ARGUMENT;
    return render(ctx, ui);
}

/* No on_back: the title's back closes the page. Return 0 from one to handle it
 * yourself, e.g. to pop a sub-page. */
static const struct canopus_ui_entry_v1 s_entry = {
    sizeof(struct canopus_ui_entry_v1), "{{id}}", on_open, on_event, 0, 0, 0, 0,
};

static int32_t on_prepare(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_activate(const struct canopus_context_v1 *ctx)
{
    if (ctx->ui == 0) return -1;
    return ctx->ui->set_entry(ctx, &s_entry);
}

static int32_t on_deactivate(const struct canopus_context_v1 *ctx)
{
    if (ctx->ui != 0) {
        (void)ctx->ui->set_entry(ctx, 0);
    }
    return 0;
}

static int32_t on_stop(const struct canopus_context_v1 *ctx) { return on_deactivate(ctx); }

static int32_t on_query(struct canopus_status_writer_v1 *writer) { (void)writer; return 0; }

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,
    "{{id}}", "0.1.0", "", {{descriptor_target}},
    on_prepare, on_activate, on_deactivate, on_stop, on_query, 0, 0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
