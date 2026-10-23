/*
 * Generic native UI backend (see canopus_ui_host.h): Band 10 Pro by default,
 * Band 11 with -DCANOPUS_MGR_BAND11=1. The two firmwares differ in how the
 * page content root, title, rows and event objects are built; everything else
 * (labels, images, bars, slot reuse, placement, scroll locking, hiding,
 * ordering, events) is shared and renders the same.
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

/* The stock message box is used for dialogs where its entry points are
 * recovered; otherwise the dialog's children are shown inline. */
#if !defined(CANOPUS_MGR_BAND11) || defined(FW_LVX_MSGBOX_CREATE)
#define HOST_MSGBOX 1
#endif

/* The message box layouts are picked by a mode: 2 is two wide text buttons
 * (Band 10 Pro), 4 stacks two narrower ones, which fit Band 11's panel. */
#ifndef CANOPUS_MGR_BAND11
#define HOST_MSGBOX_MODE 2u
#else
#define HOST_MSGBOX_MODE 4u
#endif

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
#define HOST_ROW_WIDTH 336
#define HOST_BAR_WIDTH 336
#define HOST_CONTENT_PAD_TOP 0
#define HOST_CONTENT_PAD_BOTTOM 32
#define HOST_LABEL_LINE_CHARS 28u
#else /* CANOPUS_MGR_BAND11 */
#define HOST_CONTENT_TOP_OFFSET 68
#define HOST_CONTENT_WIDTH 212
#define HOST_CONTENT_HEIGHT 444
#define HOST_ROW_WIDTH 204
#define HOST_BAR_WIDTH 204
#define HOST_CONTENT_PAD_TOP 4
#define HOST_CONTENT_PAD_BOTTOM 32
#define HOST_LABEL_LINE_CHARS 18u
#endif /* CANOPUS_MGR_BAND11 */
#define HOST_ROW_HEIGHT 96
#define HOST_SIZE_CONTENT UINT32_C(0x200007D1) /* LV_SIZE_CONTENT */
#define HOST_ROW_GAP 8
#define HOST_BAR_HEIGHT 12
#define HOST_TEXT_GAP 4
#define HOST_EVENT_ALL 0u
#define HOST_EVENT_CLICKED 7u
#define HOST_EVENT_VALUE_CHANGED 30u

static struct canopus_ui_host_view *s_views[CANOPUS_UI_HOST_MAX_VIEWS];

/* ---- firmware widget primitives ------------------------------------- */

static void host_object_flag(void *object, uint32_t flag, int add)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *, uint32_t))(uintptr_t)(add ? FW_LVX_OBJECT_ADD_FLAG
                                                  : FW_LVX_OBJECT_CLEAR_FLAG))(object, flag);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *, uint32_t))(uintptr_t)(add ? FW_LV_OBJECT_ADD_FLAG
                                                  : FW_LV_OBJECT_CLEAR_FLAG))(object, flag);
#endif /* CANOPUS_MGR_BAND11 */
}

static void host_set_hidden(void *object, uint32_t hidden)
{
    if (object == NULL) {
        return;
    }
    host_object_flag(object, UINT32_C(1), hidden != 0u);
}

#ifdef HOST_MSGBOX
static void host_object_delete(void *object)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *))(uintptr_t)FW_LVX_OBJECT_DELETE)(object);
#else /* CANOPUS_MGR_BAND11 */
    ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_DELETE)(object);
#endif /* CANOPUS_MGR_BAND11 */
}
#endif /* HOST_MSGBOX */

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
        content, HOST_CONTENT_PAD_BOTTOM, 0u);
#endif
#else /* CANOPUS_MGR_BAND11 */
    content = ((void *(*)(void *))(uintptr_t)FW_LV_OBJECT_CREATE)(root);
    if (content == NULL) {
        return NULL;
    }
    /* A transparent scrolling viewport. 0x0C693F78 is an activity root with its
     * own lifetime timer, not the 10 Pro content-container ABI. */
    ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_REMOVE_STYLES)(content);
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LV_OBJECT_PAD_TOP)(content, HOST_CONTENT_PAD_TOP, 0u);
    ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_LV_OBJECT_PAD_BOTTOM)(content, HOST_CONTENT_PAD_BOTTOM, 0u);
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

/* Applies the semantic text role to a label: the settings theme's MiSans
 * Medium (28 px for titles, 24 px otherwise, which has the CJK glyphs the stock
 * label font lacks) and the row width with wrapping text. */
static void host_label_style(void *label, uint16_t text_style)
{
    ((void (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)FW_LVX_STYLE_APPLY)(
        label,
        (const void *)(uintptr_t)(text_style == CANOPUS_UI_TEXT_TITLE
            ? FW_STYLE_MISANS_MEDIUM_28 : FW_STYLE_MISANS_MEDIUM_24),
        255u, 0u);
#ifndef CANOPUS_MGR_BAND11
    host_set_size(label, HOST_ROW_WIDTH, (int32_t)HOST_SIZE_CONTENT);
#else /* CANOPUS_MGR_BAND11 */
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
    /* A radio row is drawn with the switch of a checkbox so its selection shows;
     * the module keeps the choice exclusive. */
    if (node_type == CANOPUS_UI_NODE_SWITCH_ROW ||
        node_type == CANOPUS_UI_NODE_CHECKBOX ||
        node_type == CANOPUS_UI_NODE_RADIO_ROW) {
        return HOST_ROW_SWITCH;
    }
    if (node_type == CANOPUS_UI_NODE_BUTTON ||
        node_type == CANOPUS_UI_NODE_ACTION_ROW) {
        return HOST_ROW_ACTION;
    }
    return HOST_ROW_STATUS;
}

/* Compare the last successfully applied snapshot without retaining another tree
 * per view. After a partial backend failure the native controls are unknown. */
static const struct canopus_ui_snapshot_v1 *host_previous(
    const struct canopus_ui_host_view *view)
{
    const struct canopus_ui_snapshot_v1 *previous = canopus_ui_current(view->ui);
    return view->render_valid && previous != NULL &&
                   previous->generation == view->rendered_generation
               ? previous : NULL;
}

static const struct canopus_ui_node_v1 *host_previous_key(
    const struct canopus_ui_snapshot_v1 *snapshot, uint32_t key)
{
    uint16_t i;
    if (snapshot != NULL) {
        for (i = 0u; i < snapshot->node_count; i++) {
            if (snapshot->nodes[i].key == key) return &snapshot->nodes[i];
        }
    }
    return NULL;
}

/* Labels, images and bars use ordinal native slots, unlike keyed rows. */
static __attribute__((noinline)) const struct canopus_ui_node_v1 *host_previous_slot(
    const struct canopus_ui_snapshot_v1 *snapshot, uint16_t type, uint32_t slot)
{
    uint16_t i;
    if (snapshot != NULL) {
        for (i = 0u; i < snapshot->node_count; i++) {
            uint16_t candidate = snapshot->nodes[i].type;
            if (candidate != type && !(type == CANOPUS_UI_NODE_TEXT &&
                                       candidate == CANOPUS_UI_NODE_RICH_TEXT)) continue;
            if (slot-- == 0u) return &snapshot->nodes[i];
        }
    }
    return NULL;
}

static __attribute__((noinline)) int host_text_equal(const struct canopus_ui_snapshot_v1 *a,
                           const struct canopus_ui_node_v1 *node_a,
                           const struct canopus_ui_snapshot_v1 *b,
                           const struct canopus_ui_node_v1 *node_b, int secondary)
{
    uint16_t length = secondary ? node_a->secondary_len : node_a->primary_len;
    uint16_t other_length = secondary ? node_b->secondary_len : node_b->primary_len;
    uint16_t off_a = secondary ? node_a->secondary_off : node_a->primary_off;
    uint16_t off_b = secondary ? node_b->secondary_off : node_b->primary_off;
    uint16_t i;
    if (length != other_length) return 0;
    for (i = 0u; i < length; i++) {
        if (a->strings[off_a + i] != b->strings[off_b + i]) return 0;
    }
    return 1;
}

/* Shared across widget classes to keep the freestanding backend within the
 * Supervisor's file budget. Visibility follows actual writes, even on failure. */
static __attribute__((noinline)) void host_visibility(
    void *object, uint8_t *visible, uint32_t slot, uint8_t show)
{
    if (object != NULL && visible[slot] != show) {
        host_set_hidden(object, !show);
        visible[slot] = show;
    }
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
        if (view->row_kinds[row_index] == HOST_ROW_SWITCH) {
            view->dirty_rows[row_index] = 1u;
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
HOST_ROW_CALLBACK(32)
HOST_ROW_CALLBACK(33)
HOST_ROW_CALLBACK(34)
HOST_ROW_CALLBACK(35)
HOST_ROW_CALLBACK(36)
HOST_ROW_CALLBACK(37)
HOST_ROW_CALLBACK(38)
HOST_ROW_CALLBACK(39)
HOST_ROW_CALLBACK(40)
HOST_ROW_CALLBACK(41)
HOST_ROW_CALLBACK(42)
HOST_ROW_CALLBACK(43)
HOST_ROW_CALLBACK(44)
HOST_ROW_CALLBACK(45)
HOST_ROW_CALLBACK(46)
HOST_ROW_CALLBACK(47)
HOST_ROW_CALLBACK(48)
HOST_ROW_CALLBACK(49)
HOST_ROW_CALLBACK(50)
HOST_ROW_CALLBACK(51)
HOST_ROW_CALLBACK(52)
HOST_ROW_CALLBACK(53)
HOST_ROW_CALLBACK(54)
HOST_ROW_CALLBACK(55)
HOST_ROW_CALLBACK(56)
HOST_ROW_CALLBACK(57)
HOST_ROW_CALLBACK(58)
HOST_ROW_CALLBACK(59)
HOST_ROW_CALLBACK(60)
HOST_ROW_CALLBACK(61)
HOST_ROW_CALLBACK(62)
HOST_ROW_CALLBACK(63)

static void (*const host_row_events[CANOPUS_UI_HOST_MAX_ROWS])(void *) = {
    host_row_event_0, host_row_event_1, host_row_event_2, host_row_event_3,
    host_row_event_4, host_row_event_5, host_row_event_6, host_row_event_7,
    host_row_event_8, host_row_event_9, host_row_event_10, host_row_event_11,
    host_row_event_12, host_row_event_13, host_row_event_14, host_row_event_15,
    host_row_event_16, host_row_event_17, host_row_event_18, host_row_event_19,
    host_row_event_20, host_row_event_21, host_row_event_22, host_row_event_23,
    host_row_event_24, host_row_event_25, host_row_event_26, host_row_event_27,
    host_row_event_28, host_row_event_29, host_row_event_30, host_row_event_31,
    host_row_event_32, host_row_event_33, host_row_event_34, host_row_event_35,
    host_row_event_36, host_row_event_37, host_row_event_38, host_row_event_39,
    host_row_event_40, host_row_event_41, host_row_event_42, host_row_event_43,
    host_row_event_44, host_row_event_45, host_row_event_46, host_row_event_47,
    host_row_event_48, host_row_event_49, host_row_event_50, host_row_event_51,
    host_row_event_52, host_row_event_53, host_row_event_54, host_row_event_55,
    host_row_event_56, host_row_event_57, host_row_event_58, host_row_event_59,
    host_row_event_60, host_row_event_61, host_row_event_62, host_row_event_63,
};

#undef HOST_ROW_CALLBACK

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

/* Picks the widget slot for a row: the slot that showed the same key, then one
 * whose key left the page, so a row keeps its native object across renders. */
static int host_find_row(struct canopus_ui_host_view *view,
                         const struct canopus_ui_snapshot_v1 *snapshot,
                         uint8_t kind, canopus_ui_node_id key,
                         uint64_t used_mask)
{
    uint32_t i;
    int empty = -1;
    int reusable = -1;

    for (i = 0; i < CANOPUS_UI_HOST_MAX_ROWS; i++) {
        if (view->rows[i] == NULL) {
            if (empty < 0) {
                empty = (int)i;
            }
        } else if (view->row_kinds[i] == kind &&
                   (used_mask & (UINT64_C(1) << i)) == 0u) {
            if (view->row_keys[i] == key) {
                return (int)i;
            }
            if (reusable < 0 &&
                !host_snapshot_uses_row(snapshot, kind, view->row_keys[i])) {
                reusable = (int)i;
            }
        }
    }
    return reusable >= 0 ? reusable : empty;
}

/* ---- title ----------------------------------------------------------- */

/* The title's back affordance reaches the view's back hook. Band 11's title
 * would otherwise send the stock back key to the page itself. The callbacks
 * carry no user data, so each view slot has its own. */
static void host_title_dispatch(uint32_t slot)
{
    struct canopus_ui_host_view *view = s_views[slot];

    if (view == NULL || !view->active || !view->interactive ||
        view->hooks == NULL || view->hooks->back == NULL) {
        return;
    }
    view->hooks->back(view);
}

#define HOST_TITLE_CALLBACK(index) \
    static void host_title_event_##index(void *event) \
    { \
        (void)event; \
        host_title_dispatch(index); \
    }

HOST_TITLE_CALLBACK(0)
HOST_TITLE_CALLBACK(1)
HOST_TITLE_CALLBACK(2)
HOST_TITLE_CALLBACK(3)
HOST_TITLE_CALLBACK(4)
HOST_TITLE_CALLBACK(5)
HOST_TITLE_CALLBACK(6)
HOST_TITLE_CALLBACK(7)
HOST_TITLE_CALLBACK(8)
HOST_TITLE_CALLBACK(9)
HOST_TITLE_CALLBACK(10)
HOST_TITLE_CALLBACK(11)
HOST_TITLE_CALLBACK(12)
HOST_TITLE_CALLBACK(13)
HOST_TITLE_CALLBACK(14)
HOST_TITLE_CALLBACK(15)

static void (*const host_title_events[CANOPUS_UI_HOST_MAX_VIEWS])(void *) = {
    host_title_event_0, host_title_event_1, host_title_event_2, host_title_event_3,
    host_title_event_4, host_title_event_5, host_title_event_6, host_title_event_7,
    host_title_event_8, host_title_event_9, host_title_event_10, host_title_event_11,
    host_title_event_12, host_title_event_13, host_title_event_14, host_title_event_15,
};

#undef HOST_TITLE_CALLBACK

static void *host_title_create(struct canopus_ui_host_view *view,
                               const char *text)
{
    uint32_t mode = (view->flags & CANOPUS_UI_HOST_ROOT_PAGE) != 0u ? 0u : 1u;
    void (*back)(void *) = NULL;
    void *title;
    uint32_t i;

    for (i = 0; mode != 0u && view->hooks != NULL && view->hooks->back != NULL &&
                i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == view) {
            back = host_title_events[i];
        }
    }
#ifndef CANOPUS_MGR_BAND11
    title = ((void *(*)(void *, const char *, uint32_t, void (*)(void *), void *))
                 (uintptr_t)FW_LVX_PAGE_TITLE_CREATE)(
        view->root, text, mode, back, NULL);
#else /* CANOPUS_MGR_BAND11 */
    title = ((void *(*)(void *, const char *, uint32_t, void (*)(void *)))
                 (uintptr_t)FW_LV_PAGE_TITLE_CREATE)(view->root, text, mode, back);
    if (title != NULL) {
        ((void (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)
             FW_LVX_STYLE_APPLY)(
            title, (const void *)(uintptr_t)FW_STYLE_MISANS_MEDIUM_24, 255u, 0u);
    }
#endif /* CANOPUS_MGR_BAND11 */
    return title;
}

/* ---- dialog ---------------------------------------------------------- */

#ifdef HOST_MSGBOX
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
        host_object_delete(box);
    }
}

/* A message box button was pressed: the page that is on top owns the dialog. */
static void host_dialog_dispatch(uint32_t button)
{
    uint32_t i;

    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        struct canopus_ui_host_view *view = s_views[i];
        struct canopus_ui_host_binding binding;

        if (view == NULL || !view->active || !view->interactive ||
            !view->dialog.active || view->dialog.dispatching) {
            continue;
        }
        binding = view->dialog.bindings[button];
        view->dialog.dispatching = 1u;
        host_dialog_dismiss(view);
        if (view->ui != NULL) {
            (void)canopus_ui_dispatch_event(view->ui, binding.generation, binding.key,
                                            binding.event_id);
        }
        if (host_view_alive(view)) {
            view->dialog.dispatching = 0u;
        }
        return;
    }
}

static void host_dialog_button_0(void *event)
{
    (void)event;
    host_dialog_dispatch(0u);
}

static void host_dialog_button_1(void *event)
{
    (void)event;
    host_dialog_dispatch(1u);
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

static __attribute__((noinline)) int host_dialog_show(struct canopus_ui_host_view *view,
                            const struct canopus_ui_snapshot_v1 *snapshot,
                            const struct canopus_ui_node_v1 *message,
                            const struct canopus_ui_node_v1 *const buttons[2])
{
    const struct canopus_ui_snapshot_v1 *previous = host_previous(view);
    const struct canopus_ui_node_v1 *old_message, *old_buttons[2];
    uint32_t i;
    int unchanged = view->dialog.active && previous != NULL &&
        host_find_dialog(previous, &old_message, old_buttons) &&
        host_text_equal(snapshot, message, previous, old_message, 0);

    for (i = 0u; unchanged && i < 2u; i++) {
        unchanged = host_text_equal(snapshot, buttons[i], previous, old_buttons[i], 0);
    }
    if (unchanged) {
        for (i = 0u; i < 2u; i++) {
            view->dialog.bindings[i].generation = snapshot->generation;
            view->dialog.bindings[i].key = buttons[i]->key;
            view->dialog.bindings[i].event_id = buttons[i]->event_id;
        }
        return 0;
    }
    view->render_valid = 0u;
    host_dialog_dismiss(view);
    if (view->root == NULL) {
        return -1;
    }
    canopus_memset(&view->dialog, 0, sizeof(view->dialog));
    for (i = 0; i < 2u; i++) {
        view->dialog.bindings[i].generation = snapshot->generation;
        view->dialog.bindings[i].key = buttons[i]->key;
        view->dialog.bindings[i].event_id = buttons[i]->event_id;
        view->dialog.buttons[i].callback = i == 0u ? host_dialog_button_0 : host_dialog_button_1;
        copy_text(view->dialog.button_text[i], sizeof(view->dialog.button_text[i]),
                  snapshot->strings + buttons[i]->primary_off);
        view->dialog.buttons[i].text = view->dialog.button_text[i];
    }
    /* The stock prefab sizes its modal scrim from its parent. Parenting it to
     * the content root would confine it to the scrollable page body, so it is
     * parented to the page root: the dialog stays full screen and frontmost
     * over the existing title and content children. Band 10 Pro also hands it
     * the firmware page so the page's back is routed to the box. */
#ifndef CANOPUS_MGR_BAND11
    view->dialog.native_box = ((void *(*)(void *, void *))(uintptr_t)
        FW_LVX_MSGBOX_CREATE)(view->root, view->firmware_page);
#else /* CANOPUS_MGR_BAND11 */
    view->dialog.native_box = ((void *(*)(void *, void *))(uintptr_t)
        FW_LVX_MSGBOX_CREATE)(view->root, NULL);
#endif /* CANOPUS_MGR_BAND11 */
    if (view->dialog.native_box == NULL) {
        return -1;
    }
    view->dialog.active = 1u;
    ((void (*)(void *, uint32_t, const char *, const char *, const char *,
               const struct canopus_ui_host_msgbox_button *, uint32_t))(uintptr_t)
         FW_LVX_MSGBOX_SET_CONTENT)(
        view->dialog.native_box, HOST_MSGBOX_MODE, NULL, snapshot->strings + message->primary_off,
        NULL, view->dialog.buttons, 2u);
    return 0;
}
#endif /* HOST_MSGBOX */

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

static int host_is_label(uint16_t type)
{
    return type == CANOPUS_UI_NODE_TEXT || type == CANOPUS_UI_NODE_RICH_TEXT;
}

static int host_is_row(uint16_t type)
{
    return type == CANOPUS_UI_NODE_STATUS_ROW || type == CANOPUS_UI_NODE_BUTTON ||
           type == CANOPUS_UI_NODE_ACTION_ROW || type == CANOPUS_UI_NODE_SWITCH_ROW ||
           type == CANOPUS_UI_NODE_CHECKBOX || type == CANOPUS_UI_NODE_RADIO_ROW;
}

struct host_chain {
    struct canopus_ui_host_view *view;
    void *previous;
    void *first;
    uint32_t old_count;   /* the placed chain of the previous apply */
    uint32_t total;       /* objects this apply places */
    uint32_t height;      /* estimated extent of the placed chain */
    uint32_t count;
    uint8_t diverged;
    uint16_t id[CANOPUS_UI_HOST_MAX_PLACED];
    uint32_t sig[CANOPUS_UI_HOST_MAX_PLACED];
};

#define HOST_PLACE_LABEL(n) ((uint16_t)(0x100u | (n)))
#define HOST_PLACE_IMAGE(n) ((uint16_t)(0x200u | (n)))
#define HOST_PLACE_BAR(n) ((uint16_t)(0x300u | (n)))
#define HOST_PLACE_ROW(n) ((uint16_t)(0x400u | (n)))

/* A label wraps; a generous line count keeps a page scrollable when unsure. */
static uint32_t host_label_height(const char *text)
{
    uint32_t length = 0u;
    while (text[length] != '\0') {
        length++;
    }
    return 36u * (1u + (length > HOST_LABEL_LINE_CHARS) + (length > 2u * HOST_LABEL_LINE_CHARS));
}

static uint32_t host_hash(const char *text)
{
    uint32_t hash = UINT32_C(2166136261);
    while (*text != '\0') {
        hash = (hash ^ (uint8_t)*text++) * UINT32_C(16777619);
    }
    return hash;
}

/* Places `object` under the previous widget (or at the top of the body).
 * Aligning moves an object back to its anchor, which also drops the page's
 * scroll offset, so while the whole chain is the same as in the last render
 * (same objects in the same order, the same inputs to their sizes) nothing is
 * aligned again. Any other change aligns the whole chain: objects the user
 * scrolled and a new route's rows must not keep each other's positions. */
static void host_place(struct host_chain *chain, void *object, uint16_t id,
                       uint32_t sig, int32_t gap, uint32_t height)
{
    struct canopus_ui_host_view *view = chain->view;
    uint32_t i = chain->count++;
    int unchanged = !chain->diverged && chain->total == chain->old_count &&
                    view->placed[i] == id && view->placed_sig[i] == sig;

    chain->id[i] = id;
    chain->sig[i] = sig;
    chain->height += (i != 0u ? (uint32_t)gap : 0u) + height;
    if (!unchanged) {
        chain->diverged = 1u;
        if (chain->previous == NULL) {
            host_align_to(object, view->content_root, HOST_ALIGN_TOP_MID, 0, 0);
        } else {
            host_align_to(object, chain->previous, HOST_ALIGN_OUT_BOTTOM_MID, 0, gap);
        }
    }
    if (chain->previous == NULL) {
        chain->first = object;
    }
    chain->previous = object;
}

static int32_t host_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_ui_host_view *view = (struct canopus_ui_host_view *)cookie;
    uint16_t i;
    uint32_t visible_rows = 0u, visible_labels = 0u, visible_images = 0u;
    uint32_t visible_progress = 0u;
    uint64_t used_mask = 0u;
    uint32_t label_used = 0u, image_used = 0u, progress_used = 0u;
    const struct canopus_ui_snapshot_v1 *previous;
    struct host_chain chain;

    if (view == NULL || view->root == NULL || snapshot == NULL ||
        snapshot->node_count == 0u) {
        return -1;
    }
    previous = host_previous(view);
#ifdef HOST_MSGBOX
    {
        const struct canopus_ui_node_v1 *message;
        const struct canopus_ui_node_v1 *buttons[2];
        if (host_find_dialog(snapshot, &message, buttons) &&
            host_dialog_show(view, snapshot, message, buttons) == 0) {
            view->rendered_generation = snapshot->generation;
            view->render_valid = 1u;
            return 0;
        }
        /* A dialog snapshot does not describe the body kept underneath it. */
        if (previous != NULL && host_find_dialog(previous, &message, buttons)) {
            previous = NULL;
        }
    }
    host_dialog_dismiss(view);
#endif /* HOST_MSGBOX */
    view->render_valid = 0u;
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
        if (host_is_label(type)) {
            visible_labels++;
        } else if (type == CANOPUS_UI_NODE_IMAGE) {
            visible_images++;
        } else if (type == CANOPUS_UI_NODE_PROGRESS) {
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

    /* A failure below leaves the chain unknown: the next render aligns everything. */
    chain.view = view;
    chain.previous = NULL;
    chain.first = NULL;
    chain.old_count = view->placed_count;
    chain.total = visible_rows + visible_labels + visible_images + visible_progress;
    chain.height = HOST_CONTENT_PAD_TOP;
    chain.count = 0u;
    chain.diverged = 0u;
    view->placed_count = 0u;
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
                host_set_hidden(view->page_title, 0u);
            }
#ifndef CANOPUS_MGR_BAND11
            chain.previous = view->page_title;
#endif /* CANOPUS_MGR_BAND11 */
            chain.first = view->page_title;
            continue;
        }
        if (host_is_label(node->type)) {
            const struct canopus_ui_node_v1 *old =
                host_previous_slot(previous, CANOPUS_UI_NODE_TEXT, label_used);
            object = view->labels[label_used];
            if (object == NULL) {
                object = host_label_create(view->content_root);
                if (object == NULL) {
                    return -1;
                }
                view->labels[label_used] = object;
            }
            if (old == NULL ||
                (previous->styles[old - previous->nodes].text_style == CANOPUS_UI_TEXT_TITLE) !=
                    (snapshot->styles[i].text_style == CANOPUS_UI_TEXT_TITLE)) {
                host_label_style(object, snapshot->styles[i].text_style);
            }
            if (old == NULL || !host_text_equal(snapshot, node, previous, old, 0)) {
                host_label_set_text(object, primary);
            }
            host_visibility(object, view->visible_labels, label_used, 1);
            host_place(&chain, object, HOST_PLACE_LABEL(label_used),
                       host_hash(primary) ^ snapshot->styles[i].text_style, HOST_TEXT_GAP,
                       host_label_height(primary));
            label_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_IMAGE) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            const struct canopus_ui_node_v1 *old =
                host_previous_slot(previous, CANOPUS_UI_NODE_IMAGE, image_used);
            const struct canopus_ui_layout_v1 *old_layout =
                old != NULL ? &previous->layouts[old - previous->nodes] : NULL;
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
            if (old == NULL || !host_text_equal(snapshot, node, previous, old, 0) ||
                snapshot->values[i].resource_id !=
                    previous->values[old - previous->nodes].resource_id) {
                host_image_set_src(object, primary);
            }
            if (old_layout == NULL || layout->width != old_layout->width ||
                layout->height != old_layout->height) {
                host_set_size(object, layout->width, layout->height);
            }
            host_visibility(object, view->visible_images, image_used, 1);
            host_place(&chain, object, HOST_PLACE_IMAGE(image_used),
                       ((uint32_t)layout->width << 16) ^ (uint32_t)layout->height,
                       HOST_ROW_GAP, (uint32_t)layout->height);
            image_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_PROGRESS) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            const struct canopus_ui_value_v1 *value = &snapshot->values[i];
            const struct canopus_ui_node_v1 *old =
                host_previous_slot(previous, CANOPUS_UI_NODE_PROGRESS, progress_used);
            const struct canopus_ui_layout_v1 *old_layout =
                old != NULL ? &previous->layouts[old - previous->nodes] : NULL;
            const struct canopus_ui_value_v1 *old_value =
                old != NULL ? &previous->values[old - previous->nodes] : NULL;
            int32_t width = layout->width > 0 ? layout->width : HOST_BAR_WIDTH;
            int32_t height = layout->height > 0 ? layout->height : HOST_BAR_HEIGHT;
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
            }
            if (old_value == NULL || old_value->minimum != value->minimum ||
                old_value->maximum != value->maximum) {
                host_bar_set_range(object, value->minimum, value->maximum);
            }
            if (old_value == NULL || old_value->value != value->value) {
                host_bar_set_value(object, value->value);
            }
            /* An automatic size is a full-width bar. */
            if (old_layout == NULL ||
                width != (old_layout->width > 0 ? old_layout->width : HOST_BAR_WIDTH) ||
                height != (old_layout->height > 0 ? old_layout->height : HOST_BAR_HEIGHT)) {
                host_set_size(object, width, height);
            }
            host_visibility(object, view->visible_progress, progress_used, 1);
            host_place(&chain, object, HOST_PLACE_BAR(progress_used),
                       ((uint32_t)(layout->width > 0 ? layout->width : HOST_BAR_WIDTH) << 16) ^
                           (uint32_t)(layout->height > 0 ? layout->height : HOST_BAR_HEIGHT),
                       HOST_ROW_GAP,
                       (uint32_t)(layout->height > 0 ? layout->height : HOST_BAR_HEIGHT));
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
                (node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED |
                                CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u, trailing);
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
            const struct canopus_ui_node_v1 *old = host_previous_key(previous, node->key);
            uint8_t selected = kind != HOST_ROW_SWITCH ||
                (node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED |
                                CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u;
            uint8_t old_selected = old != NULL && (kind != HOST_ROW_SWITCH ||
                (old->flags & (CANOPUS_UI_NODE_FLAG_CHECKED |
                               CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u);
            if (old == NULL || !host_is_row(old->type) || host_row_kind(old->type) != kind ||
                view->dirty_rows[slot] ||
                selected != old_selected ||
                !host_text_equal(snapshot, node, previous, old, 0) ||
                !host_text_equal(snapshot, node, previous, old, 1)) {
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
            view->dirty_rows[slot] = 0u;
        }
        host_visibility(object, view->visible_rows, (uint32_t)slot, 1);
        host_place(&chain, object, HOST_PLACE_ROW((uint32_t)slot), 0u, HOST_ROW_GAP,
                   HOST_ROW_HEIGHT);
        view->row_keys[slot] = node->key;
        view->bindings[slot].generation = snapshot->generation;
        view->bindings[slot].key = node->key;
        view->bindings[slot].event_id = node->event_id;
        used_mask |= UINT64_C(1) << (uint32_t)slot;
    }
    for (i = 0; i < CANOPUS_UI_HOST_MAX_ROWS; i++) {
        if (view->rows[i] != NULL && (used_mask & (UINT64_C(1) << i)) == 0u) {
            host_visibility(view->rows[i], view->visible_rows, i, 0);
            canopus_memset(&view->bindings[i], 0, sizeof(view->bindings[i]));
        }
    }
    for (i = (uint16_t)label_used; i < CANOPUS_UI_HOST_MAX_LABELS; i++) {
        host_visibility(view->labels[i], view->visible_labels, i, 0);
    }
    for (i = (uint16_t)image_used; i < CANOPUS_UI_HOST_MAX_IMAGES; i++) {
        host_visibility(view->images[i], view->visible_images, i, 0);
    }
    for (i = (uint16_t)progress_used; i < CANOPUS_UI_HOST_MAX_PROGRESS; i++) {
        host_visibility(view->progress[i], view->visible_progress, i, 0);
    }
    /* Dragging content that already fits threw it to the page bottom, where it
     * stayed (Mirage 4.100.155), so a page that fits is not scrollable. */
    {
        uint8_t fits = chain.height + HOST_CONTENT_PAD_BOTTOM <= HOST_CONTENT_HEIGHT;
        if (fits != view->scroll_locked) {
            host_object_flag(view->content_root, UINT32_C(16), !fits);
            view->scroll_locked = fits;
        }
    }
    for (i = 0; i < chain.count; i++) {
        view->placed[i] = chain.id[i];
        view->placed_sig[i] = chain.sig[i];
    }
    view->placed_count = (uint8_t)chain.count;
    view->rendered_generation = snapshot->generation;
    view->render_valid = 1u;
    if (view->hooks != NULL && view->hooks->rendered != NULL) {
        view->hooks->rendered(view, chain.first);
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
#ifdef HOST_MSGBOX
    host_dialog_dismiss(view);
#endif /* HOST_MSGBOX */
    for (i = 0; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == view) {
            s_views[i] = NULL;
        }
    }
    canopus_memset(view, 0, sizeof(*view));
}
