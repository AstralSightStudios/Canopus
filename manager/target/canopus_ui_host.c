/*
 * Generic native UI backend (see canopus_ui_host.h): Band 10 Pro by default,
 * Band 11 with -DCANOPUS_MGR_BAND11=1. The two firmwares differ in how the
 * page content root, title, rows and text styles are built; everything else
 * (slot reuse, ordering, events, dialogs' semantics) is shared.
 *
 * Every firmware entry point comes from the selected target's generated
 * configuration; no address lives here.
 */
#include <stddef.h>
#include <stdint.h>

#include "canopus_ui_host.h"
#ifndef CANOPUS_MGR_BAND11
#include "canopus_target_config.h"
#else /* CANOPUS_MGR_BAND11 */
#include "band11/canopus_band11_abi.h"
#include "canopus_veneer.h"
#endif /* CANOPUS_MGR_BAND11 */
#include "canopus_memory.h"

#define HOST_ROW_STATUS 1u
#define HOST_ROW_ACTION 2u
#define HOST_ROW_SWITCH 3u
#define HOST_TRAILING_NONE 0u
#define HOST_TRAILING_SWITCH 1u
#define HOST_TRAILING_FORWARD 3u
#define HOST_ALIGN_TOP_MID 2u
#define HOST_ALIGN_OUT_BOTTOM_MID 14u
#ifndef CANOPUS_MGR_BAND11
#define HOST_CONTENT_TOP_OFFSET 56
#define HOST_CONTENT_WIDTH 336
#define HOST_CONTENT_HEIGHT 424
#define HOST_BAR_WIDTH 336
#else /* CANOPUS_MGR_BAND11 */
#define HOST_CONTENT_TOP_OFFSET 68
#define HOST_CONTENT_WIDTH 212
#define HOST_CONTENT_HEIGHT 444
#define HOST_ROW_WIDTH 204
#define HOST_BAR_WIDTH 204
#endif /* CANOPUS_MGR_BAND11 */
#define HOST_ROW_GAP 8
#define HOST_BAR_HEIGHT 12
#define HOST_TEXT_GAP 4
#define HOST_EVENT_ALL 0u
#define HOST_EVENT_CLICKED 7u
#define HOST_EVENT_VALUE_CHANGED 30u

static struct canopus_ui_host_view *s_views[CANOPUS_UI_HOST_MAX_VIEWS];

/* ---- firmware widget primitives ------------------------------------- */

static void host_set_hidden(void *object, uint32_t hidden)
{
    if (object == NULL) {
        return;
    }
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, uint32_t))(uintptr_t)FW_LVX_SET_HIDDEN)(object, hidden);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, uint32_t))(uintptr_t)(hidden != 0u
        ? FW_LV_OBJECT_ADD_FLAG : FW_LV_OBJECT_CLEAR_FLAG))(object, UINT32_C(1));
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_set_size(void *object, int32_t width, int32_t height)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, int32_t, int32_t))(uintptr_t)FW_LVX_OBJECT_SET_SIZE)(
        object, width, height);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, int32_t, int32_t))(uintptr_t)FW_LV_OBJECT_SET_SIZE)(
        object, width, height);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_align_to(void *object, void *base, uint32_t align,
                          int32_t x, int32_t y)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, void *, uint32_t, int32_t, int32_t))(uintptr_t)
         FW_LVX_ALIGN_TO)(object, base, align, x, y);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, void *, uint32_t, int32_t, int32_t))(uintptr_t)
         FW_LV_ALIGN_TO)(object, base, align, x, y);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_add_event(void *object, void (*callback)(void *),
                           uint32_t code, void *user_data)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, void (*)(void *), uint32_t, void *))(uintptr_t)
         FW_LVX_EVENT_ADD)(object, callback, code, user_data);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, void (*)(void *), uint32_t, void *))(uintptr_t)
         FW_LV_EVENT_ADD)(object, callback, code, user_data);
#endif /* CANOPUS_MGR_BAND11 */
}

/* Creates the scrolling body every other widget is parented to. */
static void *host_content_create(void *root)
{
    void *content;
#ifndef CANOPUS_MGR_BAND11
    content = ((void *(*)(void *))(uintptr_t)FW_LVX_CONTENT_CREATE)(root);
    if (content == NULL) {
        return NULL;
    }
    host_set_size(content, HOST_CONTENT_WIDTH, HOST_CONTENT_HEIGHT);
    ((void (*)(void *, uint32_t, int32_t, int32_t))(uintptr_t)FW_LVX_OBJECT_ALIGN)(
        content, HOST_ALIGN_TOP_MID, 0, HOST_CONTENT_TOP_OFFSET);
#ifdef FW_LVX_CONTENT_PAD_BOTTOM
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LVX_CONTENT_PAD_BOTTOM)(
        content, 32, 0u);
#endif
#else /* CANOPUS_MGR_BAND11 */
    content = ((void *(*)(void *))(uintptr_t)FW_LV_OBJECT_CREATE)(root);
    if (content == NULL) {
        return NULL;
    }
    /* A transparent scrolling viewport. 0x0C693F78 is an activity root with its
     * own lifetime timer, not the 10 Pro content-container ABI. */
    ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_REMOVE_STYLES)(content);
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LV_OBJECT_PAD_TOP)(content, 4, 0u);
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LV_OBJECT_PAD_BOTTOM)(content, 32, 0u);
    host_set_size(content, HOST_CONTENT_WIDTH, HOST_CONTENT_HEIGHT);
    host_align_to(content, root, HOST_ALIGN_TOP_MID, 0, HOST_CONTENT_TOP_OFFSET);
#endif /* CANOPUS_MGR_BAND11 */
    return content;
}

static void *host_label_create(void *parent)
{
#ifndef CANOPUS_MGR_BAND11
    return ((void *(*)(void *))(uintptr_t)FW_LVX_LABEL_CREATE)(parent);
#else /* CANOPUS_MGR_BAND11 */
    return ((void *(*)(void *))(uintptr_t)FW_LV_LABEL_CREATE)(parent);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_label_set_text(void *label, const char *text)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, const char *))(uintptr_t)FW_LVX_LABEL_SET_TEXT)(label, text);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, const char *))(uintptr_t)FW_LV_LABEL_SET_TEXT)(label, text);
#endif /* CANOPUS_MGR_BAND11 */
}

/* Applies the semantic text role to a freshly created label. */
static void host_label_style(void *label, uint16_t text_style)
{
#ifndef CANOPUS_MGR_BAND11
#ifdef FW_STYLE_MISANS_DEMIBOLD_32
    if (text_style == CANOPUS_UI_TEXT_TITLE) {
        /* lvx_theme_default_init maps this exact-target style object to
         * MiSans-Demibold at 32 px; sub_C49EA98 is the stock helper firmware
         * labels use to replace the inherited text style. */
        (void)((int (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)
                   FW_LVX_STYLE_APPLY)(
            label, (const void *)(uintptr_t)FW_STYLE_MISANS_DEMIBOLD_32, 255u, 0u);
    }
#else
    (void)label;
    (void)text_style;
#endif
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)FW_LVX_STYLE_APPLY)(
        label,
        (const void *)(uintptr_t)(text_style == CANOPUS_UI_TEXT_TITLE
            ? FW_STYLE_MISANS_MEDIUM_28 : FW_STYLE_MISANS_MEDIUM_24),
        255u, 0u);
    ((void (*)(void *, int32_t))(uintptr_t)FW_LV_OBJECT_WIDTH)(label, HOST_ROW_WIDTH);
    ((void (*)(void *, uint32_t))(uintptr_t)FW_LV_LABEL_LONG_MODE)(label, 0u);
#endif /* CANOPUS_MGR_BAND11 */
}

static void *host_image_create(void *parent)
{
#ifndef CANOPUS_MGR_BAND11
    return ((void *(*)(void *))(uintptr_t)FW_LVX_IMAGE_CREATE)(parent);
#else /* CANOPUS_MGR_BAND11 */
    return ((void *(*)(void *))(uintptr_t)FW_LV_IMAGE_CREATE)(parent);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_image_set_src(void *image, const void *source)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, const void *))(uintptr_t)FW_LVX_IMAGE_SET_SRC)(image, source);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, const void *))(uintptr_t)FW_LV_IMAGE_SET_SRC)(image, source);
#endif /* CANOPUS_MGR_BAND11 */
}

static void *host_bar_create(void *parent)
{
#ifndef CANOPUS_MGR_BAND11
    return ((void *(*)(void *))(uintptr_t)FW_LVX_BAR_CREATE)(parent);
#else /* CANOPUS_MGR_BAND11 */
    return ((void *(*)(void *))(uintptr_t)FW_LV_BAR_CREATE)(parent);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_bar_set_range(void *bar, int32_t minimum, int32_t maximum)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, int32_t, int32_t))(uintptr_t)FW_LVX_BAR_SET_RANGE)(
        bar, minimum, maximum);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, int32_t, int32_t))(uintptr_t)FW_LV_BAR_SET_RANGE)(
        bar, minimum, maximum);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_bar_set_value(void *bar, int32_t value)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LVX_BAR_SET_VALUE)(bar, value, 0u);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LV_BAR_SET_VALUE)(bar, value, 0u);
#endif /* CANOPUS_MGR_BAND11 */
}

/* ---- row events ------------------------------------------------------ */

static uint8_t host_row_kind(uint16_t node_type)
{
    if (node_type == CANOPUS_UI_NODE_SWITCH_ROW ||
        node_type == CANOPUS_UI_NODE_CHECKBOX) {
        return HOST_ROW_SWITCH;
    }
    if (node_type == CANOPUS_UI_NODE_BUTTON ||
        node_type == CANOPUS_UI_NODE_ACTION_ROW ||
        node_type == CANOPUS_UI_NODE_RADIO_ROW) {
        return HOST_ROW_ACTION;
    }
    return HOST_ROW_STATUS;
}

static int32_t host_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot);

/* Whether `view` is still attached; compares pointers only, so it is safe after
 * the event handler may have closed the page and released the view's memory. */
static int host_view_alive(const struct canopus_ui_host_view *view)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == view) {
            return 1;
        }
    }
    return 0;
}

static void host_dispatch_row(uint32_t row_index, void *event)
{
    uint32_t code;
    uint32_t i;

    if (event == NULL || row_index >= CANOPUS_UI_HOST_MAX_ROWS) {
        return;
    }
#ifndef CANOPUS_MGR_BAND11
    code = ((uint32_t (*)(void *))(uintptr_t)FW_LVX_EVENT_GET_CODE)(event);
#else /* CANOPUS_MGR_BAND11 */
    code = *(const uint16_t *)((const uint8_t *)event + 8u) & UINT16_C(0x7fff);
#endif /* CANOPUS_MGR_BAND11 */
    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        struct canopus_ui_host_view *view = s_views[i];
        const struct canopus_ui_host_binding *binding;
        int32_t rc;

        if (view == NULL || !view->active || !view->interactive ||
            view->rows[row_index] == NULL) {
            continue;
        }
#ifdef CANOPUS_MGR_BAND11
        if (*(void *const *)((const uint8_t *)event + 4u) !=
            view->row_event_objects[row_index]) {
            continue;
        }
#endif /* CANOPUS_MGR_BAND11 */
        if (code != (view->row_kinds[row_index] == HOST_ROW_SWITCH
                         ? HOST_EVENT_VALUE_CHANGED : HOST_EVENT_CLICKED)) {
            return;
        }
        binding = &view->bindings[row_index];
        if (binding->event_id == 0u) {
            return;
        }
        if (view->hooks != NULL && view->hooks->before_event != NULL) {
            view->hooks->before_event(view, row_index, binding);
        }
        rc = view->ui != NULL
                 ? canopus_ui_dispatch_event(view->ui, binding->generation,
                                             binding->key, binding->event_id)
                 : CANOPUS_UI_ERR_STATE;
        if (!host_view_alive(view)) {
            return; /* the event closed the page and its view is gone */
        }
        if (view->hooks != NULL && view->hooks->after_event != NULL) {
            view->hooks->after_event(view, row_index, binding, rc);
        }
        if (rc == CANOPUS_UI_ERR_DISABLED && canopus_ui_current(view->ui) != NULL) {
            /* A disabled row was activated and a switch may have flipped by
             * itself: put the committed state back. */
            (void)host_apply(view, canopus_ui_current(view->ui));
        }
        return;
    }
}

#define HOST_ROW_CALLBACK(index) \
    static void host_row_event_##index(void *event) \
    { \
        host_dispatch_row(index, event); \
    }

HOST_ROW_CALLBACK(0)
HOST_ROW_CALLBACK(1)
HOST_ROW_CALLBACK(2)
HOST_ROW_CALLBACK(3)
HOST_ROW_CALLBACK(4)
HOST_ROW_CALLBACK(5)
HOST_ROW_CALLBACK(6)
HOST_ROW_CALLBACK(7)
HOST_ROW_CALLBACK(8)
HOST_ROW_CALLBACK(9)
HOST_ROW_CALLBACK(10)
HOST_ROW_CALLBACK(11)
HOST_ROW_CALLBACK(12)
HOST_ROW_CALLBACK(13)
HOST_ROW_CALLBACK(14)
HOST_ROW_CALLBACK(15)
HOST_ROW_CALLBACK(16)
HOST_ROW_CALLBACK(17)
HOST_ROW_CALLBACK(18)
HOST_ROW_CALLBACK(19)
HOST_ROW_CALLBACK(20)
HOST_ROW_CALLBACK(21)
HOST_ROW_CALLBACK(22)
HOST_ROW_CALLBACK(23)
HOST_ROW_CALLBACK(24)
HOST_ROW_CALLBACK(25)
HOST_ROW_CALLBACK(26)
HOST_ROW_CALLBACK(27)
HOST_ROW_CALLBACK(28)
HOST_ROW_CALLBACK(29)
HOST_ROW_CALLBACK(30)
HOST_ROW_CALLBACK(31)

static void (*const host_row_events[CANOPUS_UI_HOST_MAX_ROWS])(void *) = {
    host_row_event_0, host_row_event_1, host_row_event_2, host_row_event_3,
    host_row_event_4, host_row_event_5, host_row_event_6, host_row_event_7,
    host_row_event_8, host_row_event_9, host_row_event_10, host_row_event_11,
    host_row_event_12, host_row_event_13, host_row_event_14, host_row_event_15,
    host_row_event_16, host_row_event_17, host_row_event_18, host_row_event_19,
    host_row_event_20, host_row_event_21, host_row_event_22, host_row_event_23,
    host_row_event_24, host_row_event_25, host_row_event_26, host_row_event_27,
    host_row_event_28, host_row_event_29, host_row_event_30, host_row_event_31,
};

#undef HOST_ROW_CALLBACK

#ifdef CANOPUS_MGR_BAND11
static int host_snapshot_uses_row(const struct canopus_ui_snapshot_v1 *snapshot,
                                  uint8_t kind, canopus_ui_node_id key)
{
    uint16_t i;
    for (i = 0u; i < snapshot->node_count; i++) {
        uint16_t type = snapshot->nodes[i].type;
        if (snapshot->nodes[i].key == key && host_row_kind(type) == kind &&
            type != CANOPUS_UI_NODE_SECTION &&
            type != CANOPUS_UI_NODE_NAVIGATION_PAGE &&
            type != CANOPUS_UI_NODE_TEXT) {
            return 1;
        }
    }
    return 0;
}
#endif /* CANOPUS_MGR_BAND11 */

/* Picks the widget slot for a row. Band 10 Pro reuses any unused slot of the
 * same kind; Band 11 reuses the slot that showed the same key, then one whose
 * key left the page, so a row keeps its native object across renders. */
static int host_find_row(struct canopus_ui_host_view *view,
                         const struct canopus_ui_snapshot_v1 *snapshot,
                         uint8_t kind, canopus_ui_node_id key,
                         uint32_t used_mask)
{
    uint32_t i;
    int empty = -1;
#ifdef CANOPUS_MGR_BAND11
    int reusable = -1;
#else /* CANOPUS_MGR_BAND11 */
    (void)snapshot;
    (void)key;
#endif /* CANOPUS_MGR_BAND11 */

    for (i = 0; i < CANOPUS_UI_HOST_MAX_ROWS; i++) {
        if (view->rows[i] == NULL) {
            if (empty < 0) {
                empty = (int)i;
            }
        } else if (view->row_kinds[i] == kind &&
                   (used_mask & (UINT32_C(1) << i)) == 0u) {
#ifndef CANOPUS_MGR_BAND11
            return (int)i;
#else /* CANOPUS_MGR_BAND11 */
            if (view->row_keys[i] == key) {
                return (int)i;
            }
            if (reusable < 0 &&
                !host_snapshot_uses_row(snapshot, kind, view->row_keys[i])) {
                reusable = (int)i;
            }
#endif /* CANOPUS_MGR_BAND11 */
        }
    }
#ifndef CANOPUS_MGR_BAND11
    return empty;
#else /* CANOPUS_MGR_BAND11 */
    return reusable >= 0 ? reusable : empty;
#endif /* CANOPUS_MGR_BAND11 */
}

/* ---- title ----------------------------------------------------------- */

#ifndef CANOPUS_MGR_BAND11
static void host_title_back(void *event)
{
    struct canopus_ui_host_view *view =
        (struct canopus_ui_host_view *)((uintptr_t (*)(void *))(uintptr_t)
            FW_LVX_EVENT_GET_USER_DATA)(event);

    if (view == NULL || !view->active || !view->interactive ||
        view->hooks == NULL || view->hooks->back == NULL) {
        return;
    }
    view->hooks->back(view);
}
#endif /* CANOPUS_MGR_BAND11 */

static void *host_title_create(struct canopus_ui_host_view *view,
                               const char *text)
{
    uint32_t mode = (view->flags & CANOPUS_UI_HOST_ROOT_PAGE) != 0u ? 0u : 1u;
    void *title;
#ifndef CANOPUS_MGR_BAND11
    title = ((void *(*)(void *, const char *, uint32_t, void (*)(void *), void *))
                 (uintptr_t)FW_LVX_PAGE_TITLE_CREATE)(
        view->root, text, mode, mode != 0u ? host_title_back : NULL,
        mode != 0u ? (void *)view : NULL);
#else /* CANOPUS_MGR_BAND11 */
    title = ((void *(*)(void *, const char *, uint32_t, void (*)(void *)))
                 (uintptr_t)FW_LV_PAGE_TITLE_CREATE)(view->root, text, mode, NULL);
    if (title != NULL) {
        ((void (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)
             FW_LVX_STYLE_APPLY)(
            title, (const void *)(uintptr_t)FW_STYLE_MISANS_MEDIUM_24, 255u, 0u);
    }
#endif /* CANOPUS_MGR_BAND11 */
    return title;
}

/* ---- dialog ---------------------------------------------------------- */

#ifndef CANOPUS_MGR_BAND11
static void copy_text(char *out, uint32_t cap, const char *text)
{
    uint32_t n = 0u;
    while (n + 1u < cap && text[n] != '\0') {
        out[n] = text[n];
        n++;
    }
    out[n] = '\0';
}

static void host_dialog_dismiss(struct canopus_ui_host_view *view)
{
    void *box;
    if (view == NULL || !view->dialog.active) {
        return;
    }
    box = view->dialog.native_box;
    view->dialog.active = 0u;
    view->dialog.native_box = NULL;
    if (box != NULL) {
        ((void (*)(void *))(uintptr_t)FW_LVX_OBJECT_DELETE)(box);
    }
}

static void host_dialog_event(void *event)
{
    struct canopus_ui_host_dialog_binding *dialog_binding =
        (struct canopus_ui_host_dialog_binding *)((uintptr_t (*)(void *))(uintptr_t)
            FW_LVX_EVENT_GET_USER_DATA)(event);
    struct canopus_ui_host_view *view;

    if (dialog_binding == NULL || dialog_binding->view == NULL) {
        return;
    }
    view = dialog_binding->view;
    if (!view->active || !view->interactive || view->dialog.dispatching) {
        return;
    }
    view->dialog.dispatching = 1u;
    host_dialog_dismiss(view);
    if (view->ui != NULL) {
        (void)canopus_ui_dispatch_event(view->ui, dialog_binding->binding.generation,
                                        dialog_binding->binding.key,
                                        dialog_binding->binding.event_id);
    }
    if (host_view_alive(view)) {
        view->dialog.dispatching = 0u;
    }
}

/* The first dialog node, with its message (first text) and up to two buttons
 * (button or action rows) in tree order, which is the message box's left to
 * right order. */
static int host_find_dialog(const struct canopus_ui_snapshot_v1 *snapshot,
                            const struct canopus_ui_node_v1 **message,
                            const struct canopus_ui_node_v1 *buttons[2])
{
    uint16_t i, dialog = CANOPUS_UI_NO_NODE, count = 0u;
    *message = NULL;
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        if (dialog == CANOPUS_UI_NO_NODE) {
            if (node->type == CANOPUS_UI_NODE_DIALOG) {
                dialog = i;
            }
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_TEXT && *message == NULL) {
            *message = node;
        } else if ((node->type == CANOPUS_UI_NODE_BUTTON ||
                    node->type == CANOPUS_UI_NODE_ACTION_ROW) && count < 2u) {
            buttons[count++] = node;
        }
    }
    return dialog != CANOPUS_UI_NO_NODE && *message != NULL && count == 2u;
}

static int host_dialog_show(struct canopus_ui_host_view *view,
                            const struct canopus_ui_snapshot_v1 *snapshot,
                            const struct canopus_ui_node_v1 *message,
                            const struct canopus_ui_node_v1 *const buttons[2])
{
    uint32_t i;

    if (view->dialog.active) {
        return 0;
    }
    if (view->root == NULL || view->firmware_page == NULL) {
        return -1;
    }
    canopus_memset(&view->dialog, 0, sizeof(view->dialog));
    for (i = 0; i < 2u; i++) {
        view->dialog.bindings[i].binding.generation = snapshot->generation;
        view->dialog.bindings[i].binding.key = buttons[i]->key;
        view->dialog.bindings[i].binding.event_id = buttons[i]->event_id;
        view->dialog.bindings[i].view = view;
        view->dialog.buttons[i].callback = host_dialog_event;
        copy_text(view->dialog.button_text[i], sizeof(view->dialog.button_text[i]),
                  snapshot->strings + buttons[i]->primary_off);
        view->dialog.buttons[i].text = view->dialog.button_text[i];
        view->dialog.buttons[i].user_data = &view->dialog.bindings[i];
    }
    /* The stock prefab sizes its modal scrim from its parent. Parenting it to
     * the content root would confine it to the scrollable page body, so it is
     * parented to the page root: the dialog stays full screen and frontmost
     * over the existing title and content children. */
    view->dialog.native_box = ((void *(*)(void *, void *))(uintptr_t)
        FW_LVX_MSGBOX_CREATE)(view->root, view->firmware_page);
    if (view->dialog.native_box == NULL) {
        return -1;
    }
    view->dialog.active = 1u;
    ((void (*)(void *, uint32_t, const char *, const char *, const char *,
               const struct canopus_ui_host_msgbox_button *, uint32_t))(uintptr_t)
         FW_LVX_MSGBOX_SET_CONTENT)(
        view->dialog.native_box, 2u, NULL, snapshot->strings + message->primary_off,
        NULL, view->dialog.buttons, 2u);
    return 0;
}
#endif /* CANOPUS_MGR_BAND11 */

/* ---- apply ----------------------------------------------------------- */

/* Whether a node is a container the page body flattens: its children are
 * ordinary nodes of the same pre-order list. */
static int host_is_container(uint16_t type)
{
    return type == CANOPUS_UI_NODE_SECTION || type == CANOPUS_UI_NODE_LIST ||
           type == CANOPUS_UI_NODE_SCROLL || type == CANOPUS_UI_NODE_DIALOG ||
           type == CANOPUS_UI_NODE_DIVIDER || type == CANOPUS_UI_NODE_SPACER;
}

static int host_is_title(uint16_t type)
{
    return type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
           type == CANOPUS_UI_NODE_NAVIGATION_HEADER;
}

#ifndef CANOPUS_MGR_BAND11
/* On Mirage 3.101.043 the row widgets, the page title and the message box were
 * exercised; a page with a stock label showed missing glyphs and then a blank
 * screen, and a page with a progress bar stopped drawing at the bar. Band 10 Pro
 * therefore draws text as a status row and rejects images, bars and sliders
 * (before any widget is touched), until those entry points are verified. */
static int host_is_label(uint16_t type)
{
    (void)type;
    return 0;
}

static int host_is_unverified_widget(uint16_t type)
{
    return type == CANOPUS_UI_NODE_IMAGE || type == CANOPUS_UI_NODE_PROGRESS ||
           type == CANOPUS_UI_NODE_SLIDER;
}

static int host_is_text(uint16_t type)
{
    return type == CANOPUS_UI_NODE_TEXT || type == CANOPUS_UI_NODE_RICH_TEXT;
}
#else /* CANOPUS_MGR_BAND11 */
static int host_is_label(uint16_t type)
{
    return type == CANOPUS_UI_NODE_TEXT || type == CANOPUS_UI_NODE_RICH_TEXT;
}

static int host_is_unverified_widget(uint16_t type)
{
    (void)type;
    return 0;
}

static int host_is_text(uint16_t type)
{
    (void)type;
    return 0;
}
#endif /* CANOPUS_MGR_BAND11 */

static int host_is_row(uint16_t type)
{
    return type == CANOPUS_UI_NODE_STATUS_ROW || type == CANOPUS_UI_NODE_BUTTON ||
           type == CANOPUS_UI_NODE_ACTION_ROW || type == CANOPUS_UI_NODE_SWITCH_ROW ||
           type == CANOPUS_UI_NODE_CHECKBOX || type == CANOPUS_UI_NODE_RADIO_ROW ||
           host_is_text(type);
}

/* Places `object` under the previous widget (or at the top of the body). */
static void host_place(struct canopus_ui_host_view *view, void *object,
                       void **previous, void **first, int32_t gap)
{
    if (*previous == NULL) {
        host_align_to(object, view->content_root, HOST_ALIGN_TOP_MID, 0, 0);
        *first = object;
    } else {
        host_align_to(object, *previous, HOST_ALIGN_OUT_BOTTOM_MID, 0, gap);
    }
    *previous = object;
}

static int32_t host_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_ui_host_view *view = (struct canopus_ui_host_view *)cookie;
    uint16_t i;
    uint32_t visible_rows = 0u, visible_labels = 0u, visible_images = 0u;
    uint32_t visible_progress = 0u;
    uint32_t used_mask = 0u, label_used = 0u, image_used = 0u, progress_used = 0u;
    void *previous = NULL;
    void *first = NULL;

    if (view == NULL || view->root == NULL || snapshot == NULL ||
        snapshot->node_count == 0u) {
        return -1;
    }
#ifndef CANOPUS_MGR_BAND11
    {
        const struct canopus_ui_node_v1 *message;
        const struct canopus_ui_node_v1 *buttons[2];
        if (host_find_dialog(snapshot, &message, buttons) &&
            host_dialog_show(view, snapshot, message, buttons) == 0) {
            return 0;
        }
    }
    host_dialog_dismiss(view);
#endif /* CANOPUS_MGR_BAND11 */
    if (view->content_root == NULL) {
        view->content_root = host_content_create(view->root);
        if (view->content_root == NULL) {
            return -1;
        }
    }
    for (i = 0; i < snapshot->node_count; i++) {
        uint16_t type = snapshot->nodes[i].type;
        if (host_is_container(type) || host_is_title(type)) {
            continue;
        }
        if (host_is_unverified_widget(type)) {
            return -1;
        }
        if (host_is_label(type)) {
            visible_labels++;
        } else if (type == CANOPUS_UI_NODE_IMAGE) {
            visible_images++;
        } else if (type == CANOPUS_UI_NODE_PROGRESS || type == CANOPUS_UI_NODE_SLIDER) {
            visible_progress++;
        } else if (host_is_row(type)) {
            visible_rows++;
        } else {
            return -1;
        }
    }
    if (visible_labels > CANOPUS_UI_HOST_MAX_LABELS ||
        visible_rows > CANOPUS_UI_HOST_MAX_ROWS ||
        visible_images > CANOPUS_UI_HOST_MAX_IMAGES ||
        visible_progress > CANOPUS_UI_HOST_MAX_PROGRESS) {
        return -1;
    }

    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        const char *primary;
        const char *secondary;
        uint8_t kind;
        int slot;
        void *object;

        if (host_is_container(node->type)) {
            continue;
        }
        primary = snapshot->strings + node->primary_off;
        secondary = node->secondary_len != 0u
                        ? snapshot->strings + node->secondary_off : NULL;
        if (host_is_title(node->type)) {
            /* lvx_page_title_create owns the stock left title, right-side time,
             * optional back affordance and the standard content inset. It is
             * created once per firmware page: updating its title contract is
             * not yet recovered. */
            if (view->page_title == NULL) {
                view->page_title = host_title_create(view, primary);
                if (view->page_title == NULL) {
                    return -1;
                }
            }
            host_set_hidden(view->page_title, 0u);
#ifndef CANOPUS_MGR_BAND11
            previous = view->page_title;
#endif /* CANOPUS_MGR_BAND11 */
            first = view->page_title;
            continue;
        }
        if (host_is_label(node->type)) {
            object = view->labels[label_used];
            if (object == NULL) {
                object = host_label_create(view->content_root);
                if (object == NULL) {
                    return -1;
                }
                view->labels[label_used] = object;
            }
#ifdef CANOPUS_MGR_BAND11
            host_label_style(object, snapshot->styles[i].text_style);
            host_label_set_text(object, primary);
#else /* CANOPUS_MGR_BAND11 */
            host_label_set_text(object, primary);
            host_label_style(object, snapshot->styles[i].text_style);
#endif /* CANOPUS_MGR_BAND11 */
            host_set_hidden(object, 0u);
            host_place(view, object, &previous, &first, HOST_TEXT_GAP);
            label_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_IMAGE) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            if (primary[0] == '\0' || snapshot->values[i].resource_id == 0u ||
                layout->width <= 0 || layout->height <= 0) {
                return -1;
            }
            object = view->images[image_used];
            if (object == NULL) {
                object = host_image_create(view->content_root);
                if (object == NULL) {
                    return -1;
                }
                view->images[image_used] = object;
            }
            host_image_set_src(object, primary);
            host_set_size(object, layout->width, layout->height);
            host_set_hidden(object, 0u);
            host_place(view, object, &previous, &first, HOST_ROW_GAP);
            image_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_PROGRESS ||
            node->type == CANOPUS_UI_NODE_SLIDER) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            const struct canopus_ui_value_v1 *value = &snapshot->values[i];
            if (value->minimum >= value->maximum) {
                return -1;
            }
            object = view->progress[progress_used];
            if (object == NULL) {
                object = host_bar_create(view->content_root);
                if (object == NULL) {
                    return -1;
                }
                view->progress[progress_used] = object;
                host_bar_set_range(object, value->minimum, value->maximum);
                host_bar_set_value(object, value->value);
                view->progress_minimum[progress_used] = value->minimum;
                view->progress_maximum[progress_used] = value->maximum;
                view->progress_value[progress_used] = value->value;
            } else {
                if (view->progress_minimum[progress_used] != value->minimum ||
                    view->progress_maximum[progress_used] != value->maximum) {
                    host_bar_set_range(object, value->minimum, value->maximum);
                    view->progress_minimum[progress_used] = value->minimum;
                    view->progress_maximum[progress_used] = value->maximum;
                }
                if (view->progress_value[progress_used] != value->value) {
                    host_bar_set_value(object, value->value);
                    view->progress_value[progress_used] = value->value;
                }
            }
            /* An automatic size is a full-width bar. */
            host_set_size(object, layout->width > 0 ? layout->width : HOST_BAR_WIDTH,
                          layout->height > 0 ? layout->height : HOST_BAR_HEIGHT);
            host_set_hidden(object, 0u);
            host_place(view, object, &previous, &first, HOST_ROW_GAP);
            progress_used++;
            continue;
        }

        kind = host_row_kind(node->type);
        slot = host_find_row(view, snapshot, kind, node->key, used_mask);
        if (slot < 0) {
            return -1;
        }
        object = view->rows[slot];
        if (object == NULL) {
            uint8_t trailing = kind == HOST_ROW_SWITCH ? HOST_TRAILING_SWITCH
                             : kind == HOST_ROW_ACTION ? HOST_TRAILING_FORWARD
                                                       : HOST_TRAILING_NONE;
            void *event_object;
#ifndef CANOPUS_MGR_BAND11
            object = ((void *(*)(void *, const char *, const char *, uint8_t))(
                uintptr_t)FW_LVX_LIST_ROW_CREATE)(view->content_root, primary,
                                                  secondary != NULL ? secondary : "",
                                                  trailing);
            if (object == NULL) {
                return -1;
            }
            event_object = kind == HOST_ROW_SWITCH
                ? ((void *(*)(void *))(uintptr_t)FW_LVX_LIST_ROW_TRAILING)(object)
                : object;
            if (event_object == NULL) {
                return -1;
            }
            /* Switches register on the trailing object for LV_EVENT_ALL and
             * toggle on VALUE_CHANGED, exactly like the stock VAS alarm switch
             * (sub_C661410); rows register on the row for LV_EVENT_CLICKED. */
            host_add_event(event_object, host_row_events[slot],
                           kind == HOST_ROW_SWITCH ? HOST_EVENT_ALL
                                                   : HOST_EVENT_CLICKED, NULL);
#else /* CANOPUS_MGR_BAND11 */
            object = ((void *(*)(void *))(uintptr_t)FW_LVX_LIST_ITEM_CREATE)(
                view->content_root);
            if (object == NULL) {
                return -1;
            }
            /* The native class applies the settings theme: 204x96, radius 24,
             * #222 background, MiSans Medium 28/24 and grey secondary text.
             * Populate only once: repeating it would allocate duplicate children. */
            ((void (*)(void *, uint32_t))(uintptr_t)FW_LV_OBJECT_CLEAR_FLAG)(object, 16u);
            ((void *(*)(void *, const void *, const char *, const char *, int32_t,
                        uint8_t, uint8_t))(uintptr_t)FW_LVX_LIST_ITEM_ADD_CONTENT)(
                object, NULL, primary, secondary, 0,
                (node->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u, trailing);
            event_object = kind == HOST_ROW_SWITCH
                ? *(void **)((uint8_t *)object + FW_LVX_LIST_TRAILING_OFFSET)
                : object;
            if (event_object == NULL) {
                return -1;
            }
            view->row_event_objects[slot] = event_object;
            host_add_event(event_object, host_row_events[slot],
                           kind == HOST_ROW_SWITCH ? HOST_EVENT_VALUE_CHANGED
                                                   : HOST_EVENT_CLICKED, NULL);
#endif /* CANOPUS_MGR_BAND11 */
            view->rows[slot] = object;
            view->row_kinds[slot] = kind;
        }
        {
            uint8_t selected = kind != HOST_ROW_SWITCH ||
                (node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED |
                                CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u;
#ifndef CANOPUS_MGR_BAND11
            (void)((int (*)(void *, const void *, const char *, const char *,
                            int32_t, uint8_t))(uintptr_t)FW_LVX_LIST_ROW_UPDATE)(
                object, NULL, primary, secondary != NULL ? secondary : "", 0,
                selected);
#else /* CANOPUS_MGR_BAND11 */
            ((void (*)(void *, const void *, const char *, const char *, int32_t,
                       uint8_t))(uintptr_t)FW_LVX_LIST_ITEM_UPDATE)(
                object, NULL, primary, secondary, 0, selected);
#endif /* CANOPUS_MGR_BAND11 */
        }
        host_set_hidden(object, 0u);
        host_place(view, object, &previous, &first, HOST_ROW_GAP);
#ifdef CANOPUS_MGR_BAND11
        view->row_keys[slot] = node->key;
#endif /* CANOPUS_MGR_BAND11 */
        view->bindings[slot].generation = snapshot->generation;
        view->bindings[slot].key = node->key;
        view->bindings[slot].event_id = node->event_id;
        used_mask |= UINT32_C(1) << (uint32_t)slot;
    }
    for (i = 0; i < CANOPUS_UI_HOST_MAX_ROWS; i++) {
        if (view->rows[i] != NULL && (used_mask & (UINT32_C(1) << i)) == 0u) {
#ifndef CANOPUS_MGR_BAND11
            /* lvx_set_hidden left a row that had left the page drawn on Mirage
             * 3.101.043, so Band 10 Pro deletes it (a message box is deleted from
             * its own button callback the same way). */
            ((void (*)(void *))(uintptr_t)FW_LVX_OBJECT_DELETE)(view->rows[i]);
            view->rows[i] = NULL;
            view->row_kinds[i] = 0u;
#else /* CANOPUS_MGR_BAND11 */
            host_set_hidden(view->rows[i], 1u);
#endif /* CANOPUS_MGR_BAND11 */
            canopus_memset(&view->bindings[i], 0, sizeof(view->bindings[i]));
        }
    }
    for (i = (uint16_t)label_used; i < CANOPUS_UI_HOST_MAX_LABELS; i++) {
        host_set_hidden(view->labels[i], 1u);
    }
    for (i = (uint16_t)image_used; i < CANOPUS_UI_HOST_MAX_IMAGES; i++) {
        host_set_hidden(view->images[i], 1u);
    }
    for (i = (uint16_t)progress_used; i < CANOPUS_UI_HOST_MAX_PROGRESS; i++) {
        host_set_hidden(view->progress[i], 1u);
    }
    view->rendered_generation = snapshot->generation;
    if (view->hooks != NULL && view->hooks->rendered != NULL) {
        view->hooks->rendered(view, first);
    }
    return 0;
}

const struct canopus_ui_backend_v1 canopus_ui_host_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    host_apply,
};

/* ---- view lifecycle -------------------------------------------------- */

void canopus_ui_host_attach(struct canopus_ui_host_view *view, void *root,
                            void *firmware_page,
                            const struct canopus_ui_host_hooks *hooks,
                            uint32_t flags)
{
    uint32_t i;
    if (view == NULL) {
        return;
    }
    canopus_ui_host_detach(view);
    canopus_memset(view, 0, sizeof(*view));
    view->root = root;
    view->firmware_page = firmware_page;
    view->hooks = hooks;
    view->flags = flags;
    view->active = 1u;
    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == NULL) {
            s_views[i] = view;
            return;
        }
    }
    view->active = 0u; /* no registry slot: the view can render but gets no events */
}

void canopus_ui_host_bind(struct canopus_ui_host_view *view,
                          struct canopus_ui_context_v1 *ui)
{
    if (view != NULL) {
        view->ui = ui;
    }
}

void canopus_ui_host_set_interactive(struct canopus_ui_host_view *view, int interactive)
{
    if (view != NULL) {
        view->interactive = interactive != 0 ? 1u : 0u;
    }
}

void canopus_ui_host_detach(struct canopus_ui_host_view *view)
{
    uint32_t i;
    if (view == NULL) {
        return;
    }
#ifndef CANOPUS_MGR_BAND11
    host_dialog_dismiss(view);
#endif /* CANOPUS_MGR_BAND11 */
    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == view) {
            s_views[i] = NULL;
        }
    }
    canopus_memset(view, 0, sizeof(*view));
}
