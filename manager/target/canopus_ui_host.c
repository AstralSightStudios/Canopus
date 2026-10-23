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

static void host_object_delete(void *object)
{
#ifndef CANOPUS_MGR_BAND11
    ((void (*)(void *))(uintptr_t)FW_LVX_OBJECT_DELETE)(object);
#else /* CANOPUS_MGR_BAND11 */
    /* The bound internal delete does not invalidate the old screen rectangle. */
    host_set_hidden(object, 1u);
    ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_DELETE)(object);
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
    /* Parent-aligned layout uses content coordinates. Native align_to adds the
     * parent's scroll offset to keep its reference point on the viewport. */
    if (align == 0u) {
#ifdef FW_UI_OBJECT_GET_SCROLL_X
        x -= ((int32_t (*)(void *))(uintptr_t)FW_UI_OBJECT_GET_SCROLL_X)(base);
#endif
#ifdef FW_UI_OBJECT_GET_SCROLL_Y
        y -= ((int32_t (*)(void *))(uintptr_t)FW_UI_OBJECT_GET_SCROLL_Y)(base);
#endif
    }
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

/* Applies the semantic text role and wrapping without changing layout bounds:
 * MiSans Medium has the CJK glyphs the stock label font lacks. */
static void host_label_style(void *label, uint16_t text_style)
{
    ((void (*)(void *, const void *, uint32_t, uint32_t))(uintptr_t)FW_LVX_STYLE_APPLY)(
        label,
        (const void *)(uintptr_t)(text_style == CANOPUS_UI_TEXT_TITLE
            ? FW_STYLE_MISANS_MEDIUM_28 : FW_STYLE_MISANS_MEDIUM_24),
        255u, 0u);
#ifdef CANOPUS_MGR_BAND11
    ((void (*)(void *, uint32_t))(uintptr_t)FW_LV_LABEL_LONG_MODE)(label, 0u);
#endif
}

static void *host_image_create(void *parent)
{
#ifndef CANOPUS_MGR_BAND11
#ifdef FW_UI_IMAGE_CREATE_THEME_ARG
    return ((void *(*)(void *, const void *))(uintptr_t)FW_LVX_IMAGE_CREATE)(parent, NULL);
#else
    return ((void *(*)(void *))(uintptr_t)FW_LVX_IMAGE_CREATE)(parent);
#endif
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

/* Native objects are keyed and bounded by the public tree capacity. */
static struct canopus_ui_host_node *host_node(struct canopus_ui_host_view *view, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_UI_MAX_NODES; i++)
        if (view->nodes[i].object != NULL && view->nodes[i].key == key) return &view->nodes[i];
    return NULL;
}
/* Native delete reaps descendants. Forgetting every slot below `key` keeps
 * later cleanup passes from deleting the reaped objects a second time. */
static void host_forget_descendants(struct canopus_ui_host_view *view, uint32_t key)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_UI_MAX_NODES; i++) {
        struct canopus_ui_host_node *child = &view->nodes[i];
        if (child->object == NULL || child->parent_key != key) continue;
        host_forget_descendants(view, child->key);
        canopus_memset(child, 0, sizeof(*child));
    }
}
static void *host_input_object(struct canopus_ui_host_view *view, uint32_t key)
{
    struct canopus_ui_host_node *node = host_node(view, key);
    return node != NULL ? node->object : NULL;
}
static int32_t host_input_translate(struct canopus_ui_host_view *view, uint32_t key,
                                     int32_t x, int32_t y)
{
    struct canopus_ui_host_node *node = host_node(view, key);
    void *parent;
    if (node == NULL || !node->visible) return CANOPUS_UI_ERR_STALE;
    parent = host_input_object(view, node->parent_key);
    if (parent == NULL) parent = view->content_root;
    host_align_to(node->object, parent, 0u, node->x + x, node->y + y);
    return 0;
}
#include "canopus_ui_input.inc"

static int32_t host_apply(void *, const struct canopus_ui_snapshot_v1 *);
static void host_content_event(void *event)
{
    uint32_t v, i;
    if (event == NULL) return;
    for (v = 0; v < CANOPUS_UI_HOST_MAX_VIEWS; v++) {
        struct canopus_ui_host_view *view = s_views[v];
        if (view == NULL || !view->active || !view->interactive) continue;
#ifndef CANOPUS_MGR_BAND11
        if (((void *(*)(void *))(uintptr_t)FW_LVX_EVENT_GET_USER_DATA)(event) != view) continue;
#else
        if (*(void *const *)((const uint8_t *)event + 4u) != view->content_root) continue;
#endif
        for (i = 0; i < CANOPUS_UI_MAX_NODES; i++) {
            struct canopus_ui_host_node *node = &view->nodes[i];
            if (node->visible && node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
                struct canopus_ui_host_binding binding = node->binding;
                host_input_native(view, &binding, event);
                return;
            }
        }
    }
}
static int host_is_row(uint16_t type)
{
    return type == CANOPUS_UI_NODE_STATUS_ROW || type == CANOPUS_UI_NODE_BUTTON ||
        type == CANOPUS_UI_NODE_ACTION_ROW || type == CANOPUS_UI_NODE_SWITCH_ROW ||
        type == CANOPUS_UI_NODE_CHECKBOX || type == CANOPUS_UI_NODE_RADIO_ROW;
}
static uint8_t host_row_kind(uint16_t type)
{
    return type == CANOPUS_UI_NODE_SWITCH_ROW ? HOST_ROW_SWITCH :
        (type == CANOPUS_UI_NODE_CHECKBOX || type == CANOPUS_UI_NODE_RADIO_ROW ? 4u :
         (type == CANOPUS_UI_NODE_BUTTON || type == CANOPUS_UI_NODE_ACTION_ROW ? HOST_ROW_ACTION : HOST_ROW_STATUS));
}
static void host_dispatch_row(uint32_t slot, void *event)
{
    uint32_t v, code;
    if (event == NULL) return;
#ifndef CANOPUS_MGR_BAND11
    code = ((uint32_t (*)(void *))(uintptr_t)FW_LVX_EVENT_GET_CODE)(event);
#else
    code = *(const uint16_t *)((const uint8_t *)event + 8u) & 0x7fffu;
#endif
    for (v = 0; v < CANOPUS_UI_HOST_MAX_VIEWS; v++) {
        struct canopus_ui_host_view *view = s_views[v];
        struct canopus_ui_host_node *node;
        struct canopus_ui_host_binding binding;
        struct canopus_ui_event_v1 semantic;
        int32_t rc;
        if (view == NULL || !view->active || !view->interactive) continue;
        node = &view->nodes[slot];
        if (!node->visible || node->object == NULL) continue;
#ifdef CANOPUS_MGR_BAND11
        if (*(void *const *)((const uint8_t *)event + 4u) != node->event_object) continue;
#endif
        binding = node->binding;
        host_input_native(view, &binding, event);
        if (!host_view_alive(view)) return;
        if (view->rendered_generation != binding.generation) return;
        if (code != (node->kind == HOST_ROW_SWITCH ? HOST_EVENT_VALUE_CHANGED : HOST_EVENT_CLICKED) ||
            binding.event_id == 0u) return;
        if (node->kind == HOST_ROW_SWITCH) node->dirty = 1u;
        if (view->hooks != NULL && view->hooks->before_event != NULL)
            view->hooks->before_event(view, slot, &binding);
        if (!host_view_alive(view)) return;
        canopus_memset(&semantic, 0, sizeof(semantic));
        semantic.struct_size = sizeof(semantic); semantic.generation = binding.generation;
        semantic.key = binding.key; semantic.event_id = binding.event_id;
        semantic.type = node->kind == HOST_ROW_SWITCH ? CANOPUS_UI_EVENT_VALUE_CHANGED : CANOPUS_UI_EVENT_ACTIVATE;
        rc = canopus_ui_dispatch_event(view->ui, &semantic);
        if (!host_view_alive(view)) return;
        if (view->hooks != NULL && view->hooks->after_event != NULL)
            view->hooks->after_event(view, slot, &binding, rc);
        if (!host_view_alive(view)) return;
        if (rc == CANOPUS_UI_ERR_DISABLED && canopus_ui_current(view->ui) != NULL)
            (void)host_apply(view, canopus_ui_current(view->ui));
        return;
    }
}
#define HOST_CB(n) static void host_row_event_##n(void *e) { host_dispatch_row(n,e); }
HOST_CB(0) HOST_CB(1) HOST_CB(2) HOST_CB(3) HOST_CB(4) HOST_CB(5) HOST_CB(6) HOST_CB(7)
HOST_CB(8) HOST_CB(9) HOST_CB(10) HOST_CB(11) HOST_CB(12) HOST_CB(13) HOST_CB(14) HOST_CB(15)
HOST_CB(16) HOST_CB(17) HOST_CB(18) HOST_CB(19) HOST_CB(20) HOST_CB(21) HOST_CB(22) HOST_CB(23)
HOST_CB(24) HOST_CB(25) HOST_CB(26) HOST_CB(27) HOST_CB(28) HOST_CB(29) HOST_CB(30) HOST_CB(31)
#undef HOST_CB
static void (*const host_row_events[CANOPUS_UI_MAX_NODES])(void *) = {
    host_row_event_0,host_row_event_1,host_row_event_2,host_row_event_3,
    host_row_event_4,host_row_event_5,host_row_event_6,host_row_event_7,
    host_row_event_8,host_row_event_9,host_row_event_10,host_row_event_11,
    host_row_event_12,host_row_event_13,host_row_event_14,host_row_event_15,
    host_row_event_16,host_row_event_17,host_row_event_18,host_row_event_19,
    host_row_event_20,host_row_event_21,host_row_event_22,host_row_event_23,
    host_row_event_24,host_row_event_25,host_row_event_26,host_row_event_27,
    host_row_event_28,host_row_event_29,host_row_event_30,host_row_event_31
};

/* ---- title ----------------------------------------------------------- */

/* An explicit header dispatches its back event; ordinary page titles use the
 * view's back hook. Band 11's title would otherwise send the stock back key to
 * the page itself. Its callbacks carry no user data, so each view has its own.
 *
 * A title is replaced whenever its text or back state changes, so a native
 * event queued against the old object must not navigate the page that now
 * owns the slot. Band 11 identifies the sender by the event target, the same
 * identity check the row dispatcher uses; Band 10 Pro returns the token
 * registered with the title. */
static void host_title_dispatch(uint32_t slot, void *event)
{
    struct canopus_ui_host_view *view = s_views[slot];

    if (view == NULL || !view->active || !view->interactive ||
        view->page_title == NULL || !view->title_back) {
        return;
    }
    if (event != NULL) {
#ifdef CANOPUS_MGR_BAND11
        void *target = *(void *const *)((const uint8_t *)event + 4u);
        if (target != *(void **)((uint8_t *)view->page_title + FW_LVX_TITLE_ICON_OFFSET) &&
            target != *(void **)((uint8_t *)view->page_title + FW_LVX_TITLE_LABEL_OFFSET)) return;
#elif defined(FW_LVX_EVENT_GET_USER_DATA)
        if ((uintptr_t)
                ((void *(*)(void *))(uintptr_t)FW_LVX_EVENT_GET_USER_DATA)(event) !=
            (uintptr_t)view->title_token) return;
#endif
    }
    if (view->ui != NULL && view->title_binding.event_id != 0u) {
        struct canopus_ui_event_v1 semantic;
        if (view->rendered_generation != view->title_binding.generation) return;
        canopus_memset(&semantic, 0, sizeof(semantic));
        semantic.struct_size = sizeof(semantic);
        semantic.generation = view->title_binding.generation;
        semantic.key = view->title_binding.key;
        semantic.event_id = view->title_binding.event_id;
        semantic.type = CANOPUS_UI_EVENT_ACTIVATE;
        (void)canopus_ui_dispatch_event(view->ui, &semantic);
        return;
    }
    if (view->hooks != NULL && view->hooks->back != NULL) view->hooks->back(view);
}

#define HOST_TITLE_CALLBACK(index) \
    static void host_title_event_##index(void *event) \
    { \
        host_title_dispatch(index, event); \
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
                               const char *text, uint8_t back_enabled)
{
    uint32_t mode = back_enabled != 0u;
#ifndef CANOPUS_MGR_BAND11
    uint32_t token = view->title_token + 1u;
#endif
    void (*back)(void *) = NULL;
    void *title;
    uint32_t i;

    for (i = 0; mode != 0u && i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_views[i] == view) {
            back = host_title_events[i];
        }
    }
#ifndef CANOPUS_MGR_BAND11
    if (token == 0u) token = 1u;
    title = ((void *(*)(void *, const char *, uint32_t, void (*)(void *), void *))
                 (uintptr_t)FW_LVX_PAGE_TITLE_CREATE)(
        view->root, text, mode, back, (void *)(uintptr_t)token);
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

#include "canopus_ui_feedback.inc"

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
            struct canopus_ui_event_v1 semantic;
            canopus_memset(&semantic, 0, sizeof(semantic));
            semantic.struct_size = sizeof(semantic); semantic.type = CANOPUS_UI_EVENT_ACTIVATE;
            semantic.generation = binding.generation; semantic.key = binding.key;
            semantic.event_id = binding.event_id;
            (void)canopus_ui_dispatch_event(view->ui, &semantic);
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
    void *replacement = NULL;
    int unchanged = view->dialog.active && previous != NULL &&
        host_find_dialog(previous, &old_message, old_buttons) &&
        host_text_equal(snapshot, message, previous, old_message, 0);

    for (i = 0u; unchanged && i < 2u; i++) {
        unchanged = host_text_equal(snapshot, buttons[i], previous, old_buttons[i], 0);
    }
    if (!unchanged) {
        replacement = ((void *(*)(void *, void *))(uintptr_t)FW_LVX_MSGBOX_CREATE)(
#ifndef CANOPUS_MGR_BAND11
            view->root, view->firmware_page);
#else
            view->root, NULL);
#endif
        if (replacement == NULL) return CANOPUS_UI_ERR_BACKEND;
        host_set_hidden(replacement, 1u);
    }
    if (view->rendered_generation != snapshot->generation) {
        host_input_prepare(view, snapshot);
        if (!host_view_alive(view)) {
            /* The CANCEL callback may have destroyed the page root, which
             * already reclaimed the staged box; do not delete it again. */
            return CANOPUS_UI_ERR_STATE;
        }
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
    view->dialog.native_box = replacement;
    view->dialog.active = 1u;
    ((void (*)(void *, uint32_t, const char *, const char *, const char *,
               const struct canopus_ui_host_msgbox_button *, uint32_t))(uintptr_t)
         FW_LVX_MSGBOX_SET_CONTENT)(
        view->dialog.native_box, HOST_MSGBOX_MODE, NULL, snapshot->strings + message->primary_off,
        NULL, view->dialog.buttons, 2u);
    host_set_hidden(replacement, 0u);
    return 0;
}
#endif /* HOST_MSGBOX */

/* Layout is solved before native mutation. All positions are parent-relative;
 * native clipping and native scrolling therefore follow the actual tree. */
struct host_geometry { int32_t x, y, width, height, extent; };
static int host_is_title(uint16_t type)
{ return type == CANOPUS_UI_NODE_NAVIGATION_PAGE || type == CANOPUS_UI_NODE_NAVIGATION_HEADER; }
/* Title and toast natives are owned by the feedback paths (page_title and
 * toast_object) and deleted only there. Generic subtree cleanup must skip them
 * by node type: a cached owner pointer can go stale inside one multi-node
 * batch while the slot still references the replaced object. */
static int host_feedback_owned(uint16_t type)
{ return host_is_title(type) || type == CANOPUS_UI_NODE_TOAST; }
static int host_is_label(uint16_t type)
{ return type == CANOPUS_UI_NODE_TEXT || type == CANOPUS_UI_NODE_RICH_TEXT; }
static int host_is_container(uint16_t type)
{
    return type == CANOPUS_UI_NODE_SECTION || type == CANOPUS_UI_NODE_LIST ||
        type == CANOPUS_UI_NODE_SCROLL || type == CANOPUS_UI_NODE_DIALOG ||
        type == CANOPUS_UI_NODE_DIVIDER || type == CANOPUS_UI_NODE_SPACER;
}
static int host_bytes_equal(const void *a, const void *b, uint32_t size)
{
    const uint8_t *x = a, *y = b;
    while (size-- != 0u) if (*x++ != *y++) return 0;
    return 1;
}
static int host_snapshot_unchanged(const struct canopus_ui_host_view *view,
                                   const struct canopus_ui_snapshot_v1 *a,
                                   const struct canopus_ui_snapshot_v1 *b)
{
    uint32_t i, count = b->node_count;
    if (a == NULL || a->abi_major != b->abi_major || a->abi_minor != b->abi_minor ||
        a->node_count != count || a->string_used != b->string_used) return 0;
    for (i = 0; i < CANOPUS_UI_MAX_NODES; i++) if (view->nodes[i].dirty) return 0;
    return host_bytes_equal(a->nodes,b->nodes,count * sizeof(*a->nodes)) &&
        host_bytes_equal(a->strings,b->strings,b->string_used) &&
        host_bytes_equal(a->styles,b->styles,count * sizeof(*a->styles)) &&
        host_bytes_equal(a->layouts,b->layouts,count * sizeof(*a->layouts)) &&
        host_bytes_equal(a->values,b->values,count * sizeof(*a->values)) &&
        host_bytes_equal(a->visuals,b->visuals,count * sizeof(*a->visuals));
}
static int32_t host_bound(int32_t value, int16_t minimum, int16_t maximum)
{
    if (minimum >= 0 && value < minimum) value = minimum;
    if (maximum >= 0 && value > maximum) value = maximum;
    return value > 0 ? value : 0;
}
static uint32_t host_label_height(const char *text, int32_t width, uint32_t font)
{
    uint32_t lines = 1u, column = 0u, columns = width > 0 ? (uint32_t)width / (font / 2u) : 1u;
    const uint8_t *p = (const uint8_t *)text;
    if (columns == 0u) columns = 1u;
    while (*p != 0u) {
        if (*p == '\n') { lines++; column = 0; }
        else if ((*p & 0xc0u) != 0x80u) {
            column += *p < 0x80u ? 1u : 2u;
            if (column > columns) { lines++; column = 1u; }
        }
        p++;
    }
    return lines * (font + 8u);
}
static void host_layout(const struct canopus_ui_snapshot_v1 *s, uint16_t index,
                        int32_t available_width, int32_t stretched_height,
                        struct host_geometry *g)
{
    const struct canopus_ui_node_v1 *node = &s->nodes[index];
    const struct canopus_ui_layout_v1 *l = &s->layouts[index];
    const struct canopus_ui_visual_v1 *v = &s->visuals[index];
    struct host_geometry *box = &g[index];
    uint16_t child;
    int32_t inner, main = 0, cross = 0, count = 0, gap = l->gap;
    uint32_t font = v->font_px != 0 ? v->font_px :
        (s->styles[index].text_style == CANOPUS_UI_TEXT_TITLE ? 28u : 24u);
    int title = host_is_title(node->type);
    box->width = host_bound(l->width >= 0 ? l->width : available_width, l->min_width, l->max_width);
    inner = box->width - l->padding_left - l->padding_right;
    if (inner < 0) inner = 0;
    box->height = 0;
    if (title) box->height = HOST_CONTENT_HEIGHT;
    else if (host_is_row(node->type)) box->height = HOST_ROW_HEIGHT;
    else if (host_is_label(node->type)) box->height = (int32_t)host_label_height(s->strings + node->primary_off, inner, font);
    else if (node->type == CANOPUS_UI_NODE_PROGRESS) box->height = HOST_BAR_HEIGHT;
    else if (node->type == CANOPUS_UI_NODE_IMAGE) box->height = l->height;
    else if (node->type == CANOPUS_UI_NODE_DIVIDER) box->height = 1;
    if (node->type == CANOPUS_UI_NODE_SECTION && node->primary_len != 0u)
        main = (int32_t)host_label_height(s->strings + node->primary_off, inner, 28u) + HOST_TEXT_GAP;
    for (child = node->first_child; child != CANOPUS_UI_NO_NODE; child = s->nodes[child].next_sibling) {
        const struct canopus_ui_layout_v1 *cl = &s->layouts[child];
        struct host_geometry *cb = &g[child];
        int horizontal = l->axis == CANOPUS_UI_AXIS_HORIZONTAL;
        int32_t child_width = inner - cl->margin_left - cl->margin_right;
        if (horizontal && cl->width < 0 && host_is_label(s->nodes[child].type)) {
            uint32_t len = s->nodes[child].primary_len;
            int32_t natural = (int32_t)len * 12;
            if (natural < child_width) child_width = natural;
        }
        host_layout(s, child, child_width, -1, g);
        /* Title nodes are chrome drawn into the firmware titlebar, not flow
         * boxes: a nested navigation header must not displace its siblings. */
        if (host_is_title(s->nodes[child].type)) continue;
        if (!(s->nodes[child].flags & CANOPUS_UI_NODE_FLAG_VISIBLE) ||
            s->nodes[child].type == CANOPUS_UI_NODE_TOAST) continue;
        if (s->visuals[child].flags & CANOPUS_UI_VISUAL_FLAG_ABSOLUTE) continue;
        if (count++ != 0) main += gap != 0 ? gap : (host_is_row(s->nodes[child].type) ? HOST_ROW_GAP : HOST_TEXT_GAP);
        main += horizontal ? cb->width + cl->margin_left + cl->margin_right : cb->height + cl->margin_top + cl->margin_bottom;
        {
            int32_t span = horizontal ? cb->height + cl->margin_top + cl->margin_bottom : cb->width + cl->margin_left + cl->margin_right;
            if (span > cross) cross = span;
        }
    }
    if (host_is_container(node->type) && node->type != CANOPUS_UI_NODE_DIVIDER && node->type != CANOPUS_UI_NODE_SPACER)
        box->height = (l->axis == CANOPUS_UI_AXIS_HORIZONTAL ? cross : main) + l->padding_top + l->padding_bottom;
    box->height = host_bound(l->height >= 0 ? l->height :
        (stretched_height >= 0 ? stretched_height : box->height), l->min_height, l->max_height);
    box->extent = (l->axis == CANOPUS_UI_AXIS_HORIZONTAL ? cross : main) + l->padding_top + l->padding_bottom;
    if ((int32_t)v->content_height > box->extent) box->extent = (int32_t)v->content_height;
    {
        int horizontal = l->axis == CANOPUS_UI_AXIS_HORIZONTAL;
        int32_t space = (horizontal ? inner : box->height - l->padding_top - l->padding_bottom) - main;
        int32_t cursor = horizontal ? l->padding_left : l->padding_top;
        int32_t extra_gap = 0;
        if (space > 0) {
            if (l->justify == CANOPUS_UI_JUSTIFY_CENTER) cursor += space / 2;
            else if (l->justify == CANOPUS_UI_JUSTIFY_END) cursor += space;
            else if (l->justify == CANOPUS_UI_JUSTIFY_SPACE_BETWEEN && count > 1) extra_gap = space / (count - 1);
            else if (l->justify == CANOPUS_UI_JUSTIFY_SPACE_AROUND && count > 0) { extra_gap = space / count; cursor += extra_gap / 2; }
            else if (l->justify == CANOPUS_UI_JUSTIFY_SPACE_EVENLY && count > 0) { extra_gap = space / (count + 1); cursor += extra_gap; }
        }
        if (node->type == CANOPUS_UI_NODE_SECTION && node->primary_len != 0u)
            cursor += (int32_t)host_label_height(s->strings + node->primary_off, inner, 28u) + HOST_TEXT_GAP;
        count = 0;
        for (child = node->first_child; child != CANOPUS_UI_NO_NODE; child = s->nodes[child].next_sibling) {
            const struct canopus_ui_layout_v1 *cl = &s->layouts[child];
            struct host_geometry *cb = &g[child];
            uint8_t align = cl->align != CANOPUS_UI_ALIGN_AUTO ? cl->align : l->align;
            int32_t cross_space;
            cb->x = l->padding_left + cl->margin_left; cb->y = l->padding_top + cl->margin_top;
            if (host_is_title(s->nodes[child].type)) continue;
            if (!(s->nodes[child].flags & CANOPUS_UI_NODE_FLAG_VISIBLE)) continue;
            if (s->visuals[child].flags & CANOPUS_UI_VISUAL_FLAG_ABSOLUTE) { cb->x = s->visuals[child].x; cb->y = s->visuals[child].y; continue; }
            if (align == CANOPUS_UI_ALIGN_STRETCH && horizontal && cl->height < 0) {
                int32_t height = box->height - l->padding_top - l->padding_bottom - cl->margin_top - cl->margin_bottom;
                height = host_bound(height,cl->min_height,cl->max_height);
                if (height != cb->height) host_layout(s, child, cb->width, height, g);
            }
            cross_space = horizontal ? box->height - l->padding_top - l->padding_bottom - cb->height - cl->margin_top - cl->margin_bottom : inner - cb->width - cl->margin_left - cl->margin_right;
            if (count++ != 0) cursor += (gap != 0 ? gap : (host_is_row(s->nodes[child].type) ? HOST_ROW_GAP : HOST_TEXT_GAP)) + extra_gap;
            if (horizontal) { cb->x = cursor + cl->margin_left; cursor += cb->width + cl->margin_left + cl->margin_right; }
            else { cb->y = cursor + cl->margin_top; cursor += cb->height + cl->margin_top + cl->margin_bottom; }
            if (align == CANOPUS_UI_ALIGN_CENTER) { if (horizontal) cb->y += cross_space / 2; else cb->x += cross_space / 2; }
            else if (align == CANOPUS_UI_ALIGN_END) { if (horizontal) cb->y += cross_space; else cb->x += cross_space; }
        }
    }
    /* Absolute boxes do not consume flow space, but their lower bounds still
     * belong to the native scrolling surface. */
    for (child = node->first_child; child != CANOPUS_UI_NO_NODE; child = s->nodes[child].next_sibling) {
        const struct canopus_ui_node_v1 *n = &s->nodes[child];
        const struct canopus_ui_visual_v1 *cv = &s->visuals[child];
        int32_t bottom = g[child].y + g[child].height + cv->translate_y + l->padding_bottom;
        if ((n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) && !host_is_title(n->type) &&
            n->type != CANOPUS_UI_NODE_TOAST && bottom > box->extent) box->extent = bottom;
    }
}

static void *host_container_create(void *parent)
{
#ifndef CANOPUS_MGR_BAND11
    void *object = ((void *(*)(void *))(uintptr_t)FW_LVX_CONTENT_CREATE)(parent);
#else
    void *object = ((void *(*)(void *))(uintptr_t)FW_LV_OBJECT_CREATE)(parent);
    if (object != NULL) ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_REMOVE_STYLES)(object);
#endif
    if (object != NULL) host_object_flag(object, 16u, 0);
    return object;
}
static uint32_t host_color(uint16_t role)
{
    static const uint32_t colors[] = { 0xffffff,0x000000,0x222222,0xffffff,0x999999,0x3498ff,0x43d17b,0xffbd44,0xff5c5c,0x666666,0x000000 };
    return role < sizeof(colors)/sizeof(colors[0]) ? colors[role] : 0xffffff;
}
static void host_style_property(void *object, uint32_t prop, uint32_t value)
{
#ifdef FW_UI_STYLE_SET
    ((void (*)(void *, uint32_t, uint32_t, uint32_t))(uintptr_t)FW_UI_STYLE_SET)(object, prop, value, 0u);
#else
    (void)object; (void)prop; (void)value;
#endif
}
static void host_style(void *object, const struct canopus_ui_style_v1 *s,
                        const struct canopus_ui_visual_v1 *v)
{
    uint32_t background = s->background;
    if (background == CANOPUS_UI_COLOR_INHERIT && (s->variant == CANOPUS_UI_VARIANT_FILLED || s->variant == CANOPUS_UI_VARIANT_TONAL)) background = CANOPUS_UI_COLOR_SURFACE_ALT;
    if (s->variant == CANOPUS_UI_VARIANT_DESTRUCTIVE && s->foreground == CANOPUS_UI_COLOR_INHERIT)
        host_style_property(object, 88u, host_color(CANOPUS_UI_COLOR_DANGER));
    if (s->foreground != CANOPUS_UI_COLOR_INHERIT || (v->flags & CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB))
        host_style_property(object, 88u, v->flags & CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB ? v->foreground_rgb : host_color(s->foreground));
    if (background != CANOPUS_UI_COLOR_INHERIT || (v->flags & CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB)) {
        host_style_property(object, 28u, v->flags & CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB ? v->background_rgb : host_color((uint16_t)background));
        host_style_property(object, 29u, background == CANOPUS_UI_COLOR_TRANSPARENT ? 0u : 255u);
    }
#ifdef FW_UI_STYLE_RADIUS
    if (s->corner_radius >= 0) host_style_property(object, FW_UI_STYLE_RADIUS, (uint32_t)s->corner_radius);
#endif
#ifdef FW_UI_STYLE_CLIP_CORNER
    host_style_property(object, FW_UI_STYLE_CLIP_CORNER, (v->flags & CANOPUS_UI_VISUAL_FLAG_CLIP) != 0u);
#endif
#ifdef FW_UI_STYLE_BORDER_COLOR
    if (s->border_color != CANOPUS_UI_COLOR_INHERIT || (v->flags & CANOPUS_UI_VISUAL_FLAG_BORDER_RGB))
        host_style_property(object, FW_UI_STYLE_BORDER_COLOR, v->flags & CANOPUS_UI_VISUAL_FLAG_BORDER_RGB ? v->border_rgb : host_color(s->border_color));
#endif
#ifdef FW_UI_STYLE_BORDER_WIDTH
    if (s->border_width >= 0 || s->variant == CANOPUS_UI_VARIANT_OUTLINED)
        host_style_property(object, FW_UI_STYLE_BORDER_WIDTH, s->border_width >= 0 ? (uint32_t)s->border_width : 1u);
#endif
#ifdef FW_UI_STYLE_OPA
    if (s->opacity != 0 || (v->flags & CANOPUS_UI_VISUAL_FLAG_OPACITY))
        host_style_property(object, FW_UI_STYLE_OPA, (uint32_t)s->opacity * 255u / 1000u);
#endif
}
/* No exact-target binding removes one local property. Rebuild the keyed subtree
 * when a property we wrote becomes inherited, retaining native theme defaults. */
static uint8_t host_local_style(const struct canopus_ui_style_v1 *s,
                                 const struct canopus_ui_visual_v1 *v)
{
    return (uint8_t)(
        ((s->foreground != CANOPUS_UI_COLOR_INHERIT || s->variant == CANOPUS_UI_VARIANT_DESTRUCTIVE || (v->flags & CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB)) ? 1u : 0u) |
        ((s->background != CANOPUS_UI_COLOR_INHERIT || s->variant == CANOPUS_UI_VARIANT_FILLED || s->variant == CANOPUS_UI_VARIANT_TONAL || (v->flags & CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB)) ? 2u : 0u) |
        (s->corner_radius >= 0 ? 4u : 0u) |
        ((s->border_color != CANOPUS_UI_COLOR_INHERIT || (v->flags & CANOPUS_UI_VISUAL_FLAG_BORDER_RGB)) ? 8u : 0u) |
        ((s->border_width >= 0 || s->variant == CANOPUS_UI_VARIANT_OUTLINED) ? 16u : 0u) |
        ((s->opacity != 0 || (v->flags & CANOPUS_UI_VISUAL_FLAG_OPACITY)) ? 32u : 0u));
}
static int32_t host_validate(const struct canopus_ui_snapshot_v1 *s)
{
    uint16_t i;
    for (i = 0; i < s->node_count; i++) {
        const struct canopus_ui_node_v1 *n = &s->nodes[i];
        const struct canopus_ui_visual_v1 *v = &s->visuals[i];
        const struct canopus_ui_style_v1 *st = &s->styles[i];
        (void)st;
        if (!host_is_title(n->type) && !host_is_container(n->type) && !host_is_row(n->type) && !host_is_label(n->type) && n->type != CANOPUS_UI_NODE_IMAGE && n->type != CANOPUS_UI_NODE_PROGRESS && n->type != CANOPUS_UI_NODE_TOAST) return CANOPUS_UI_ERR_UNSUPPORTED;
        if (n->type == CANOPUS_UI_NODE_IMAGE && (n->primary_len == 0 || s->values[i].resource_id == 0 || s->layouts[i].width <= 0 || s->layouts[i].height <= 0)) return CANOPUS_UI_ERR_ARGUMENT;
        if (n->type == CANOPUS_UI_NODE_PROGRESS && s->values[i].minimum >= s->values[i].maximum) return CANOPUS_UI_ERR_ARGUMENT;
        /* Toast geometry is owned by the feedback label and header handles are
         * viewport chrome: neither publishes a virtual extent marker. */
        if ((n->type == CANOPUS_UI_NODE_NAVIGATION_HEADER || n->type == CANOPUS_UI_NODE_TOAST) &&
            (v->flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT)) return CANOPUS_UI_ERR_UNSUPPORTED;
        if (v->font_px != 0u && v->font_px != 24u && v->font_px != 28u) return CANOPUS_UI_ERR_UNSUPPORTED;
        if (v->image_fit != CANOPUS_UI_IMAGE_FIT_NATIVE) return CANOPUS_UI_ERR_UNSUPPORTED;
#ifndef FW_UI_STYLE_SET
        { const struct canopus_ui_style_v1 defaults = CANOPUS_UI_STYLE_DEFAULT;
          struct canopus_ui_style_v1 copy = *st; copy.text_style = defaults.text_style;
          if (!host_bytes_equal(&copy, &defaults, sizeof(copy)) || (v->flags & (CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB | CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB | CANOPUS_UI_VISUAL_FLAG_BORDER_RGB | CANOPUS_UI_VISUAL_FLAG_OPACITY))) return CANOPUS_UI_ERR_UNSUPPORTED; }
#endif
#ifndef FW_UI_STYLE_RADIUS
        if (st->corner_radius >= 0) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
#ifndef FW_UI_STYLE_BORDER_COLOR
        if (st->border_color != CANOPUS_UI_COLOR_INHERIT || (v->flags & CANOPUS_UI_VISUAL_FLAG_BORDER_RGB)) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
#ifndef FW_UI_STYLE_BORDER_WIDTH
        if (st->border_width >= 0 || st->variant == CANOPUS_UI_VARIANT_OUTLINED) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
#ifndef FW_UI_STYLE_OPA
        if (st->opacity != 0 || (v->flags & CANOPUS_UI_VISUAL_FLAG_OPACITY)) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
#ifndef FW_UI_STYLE_CLIP_CORNER
        if (v->flags & CANOPUS_UI_VISUAL_FLAG_CLIP) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
        if (host_is_row(n->type) && host_row_kind(n->type) != 4u &&
            (s->layouts[i].padding_left != 0 || s->layouts[i].padding_right != 0 ||
             s->layouts[i].padding_top != 0 || s->layouts[i].padding_bottom != 0))
            return CANOPUS_UI_ERR_UNSUPPORTED;
        if (s->layouts[i].grow != 0 || s->layouts[i].shrink != 1) return CANOPUS_UI_ERR_UNSUPPORTED;
#ifndef FW_UI_OBJECT_SCROLL_TO_Y
        if (v->scroll_y != 0) return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
    }
    return 0;
}

static int32_t host_create_node(struct canopus_ui_host_node *slot, void *parent,
                                 const struct canopus_ui_snapshot_v1 *s, uint16_t i)
{
    const struct canopus_ui_node_v1 *node = &s->nodes[i];
    const char *primary = s->strings + node->primary_off;
    const char *secondary = node->secondary_len ? s->strings + node->secondary_off : "";
    void *object;
    uint8_t kind = host_row_kind(node->type);
    if (host_is_label(node->type)) object = host_label_create(parent);
    else if (node->type == CANOPUS_UI_NODE_IMAGE) object = host_image_create(parent);
    else if (node->type == CANOPUS_UI_NODE_PROGRESS) object = host_bar_create(parent);
    else if (host_is_row(node->type) && kind != 4u) {
        uint8_t trailing = kind == HOST_ROW_SWITCH ? HOST_TRAILING_SWITCH : kind == HOST_ROW_ACTION ? HOST_TRAILING_FORWARD : HOST_TRAILING_NONE;
#ifndef CANOPUS_MGR_BAND11
        object = ((void *(*)(void *,const char *,const char *,uint8_t))(uintptr_t)FW_LVX_LIST_ROW_CREATE)(parent,primary,secondary,trailing);
        if (object != NULL) slot->event_object = kind == HOST_ROW_SWITCH ? ((void *(*)(void *))(uintptr_t)FW_LVX_LIST_ROW_TRAILING)(object) : object;
#else
        object = ((void *(*)(void *))(uintptr_t)FW_LVX_LIST_ITEM_CREATE)(parent);
        if (object != NULL) {
            ((void *(*)(void *,const void *,const char *,const char *,int32_t,uint8_t,uint8_t))(uintptr_t)FW_LVX_LIST_ITEM_ADD_CONTENT)(object,NULL,primary,secondary,0,(node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED|CANOPUS_UI_NODE_FLAG_SELECTED)) != 0,trailing);
            slot->event_object = kind == HOST_ROW_SWITCH ? *(void **)((uint8_t *)object + FW_LVX_LIST_TRAILING_OFFSET) : object;
        }
#endif
    } else {
        object = host_container_create(parent);
        if (object != NULL && ((node->type == CANOPUS_UI_NODE_SECTION && node->primary_len) || kind == 4u)) {
            slot->primary = host_label_create(object);
            if (slot->primary == NULL) { host_object_delete(object); return CANOPUS_UI_ERR_BACKEND; }
        }
        if (object != NULL && kind == 4u) {
            slot->secondary = host_label_create(object); slot->indicator = host_label_create(object);
            if (slot->secondary == NULL || slot->indicator == NULL) { host_object_delete(object); return CANOPUS_UI_ERR_BACKEND; }
        }
    }
    if (object == NULL) return CANOPUS_UI_ERR_BACKEND;
    slot->object = object;
    if (slot->event_object == NULL) slot->event_object = object;
    slot->type = node->type; slot->kind = kind;
    return 0;
}

static void *host_extent_create(void *parent)
{
    void *object = host_container_create(parent);
    if (object != NULL) {
        /* A visible transparent pixel extends LVGL's child bounds without
         * participating in hit testing or drawing a second content surface. */
        host_object_flag(object, 2u, 0);
        host_set_size(object, 1, 1);
        host_style_property(object, 29u, 0u);
        host_set_hidden(object, 1u);
    }
    return object;
}

static int32_t host_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_ui_host_view *view = cookie;
    const struct canopus_ui_snapshot_v1 *previous;
    struct host_geometry geometry[CANOPUS_UI_MAX_NODES];
    uint8_t map[CANOPUS_UI_MAX_NODES], fresh[CANOPUS_UI_MAX_NODES] = {0};
    struct canopus_ui_host_node candidates[CANOPUS_UI_MAX_NODES];
    void *extent_candidates[CANOPUS_UI_MAX_NODES] = {0};
    void *content_candidate = NULL;
    int32_t retained_scroll = 0;
    uint32_t used = 0u;
    uint16_t i, j;
    int32_t result;
    int unchanged_capture;
    if (view == NULL || view->root == NULL || snapshot == NULL || snapshot->node_count == 0 || snapshot->node_count > CANOPUS_UI_MAX_NODES) return CANOPUS_UI_ERR_ARGUMENT;
    result = host_validate(snapshot); if (result != 0) return result;
    previous = host_previous(view);
    unchanged_capture = host_snapshot_unchanged(view,previous,snapshot);
    canopus_memset(geometry, 0, sizeof(geometry));
    host_layout(snapshot, 0u, HOST_CONTENT_WIDTH, -1, geometry);
    /* Keep all existing keyed slots reserved before allocating new keys. */
    for (i = 0; i < snapshot->node_count; i++) {
        map[i] = UINT8_MAX;
        for (j = 0; j < CANOPUS_UI_MAX_NODES; j++)
            if (view->nodes[j].object != NULL && view->nodes[j].key == snapshot->nodes[i].key) { map[i] = (uint8_t)j; used |= UINT32_C(1) << j; break; }
    }
    for (i = 0; i < snapshot->node_count; i++) if (map[i] == UINT8_MAX) {
        for (j = 0; used & (UINT32_C(1) << j); j++) { }
        map[i] = (uint8_t)j; used |= UINT32_C(1) << j;
    }
#ifdef HOST_MSGBOX
    {
        const struct canopus_ui_node_v1 *message, *buttons[2];
        if (host_find_dialog(snapshot, &message, buttons)) {
            result = host_dialog_show(view, snapshot, message, buttons);
            if (result != 0) return result;
            view->rendered_generation = snapshot->generation; view->render_valid = 1;
            return 0;
        }
    }
#endif
    if (view->content_root == NULL) {
        view->content_root = host_content_create(view->root);
        if (view->content_root == NULL) return CANOPUS_UI_ERR_BACKEND;
        host_add_event(view->content_root,host_content_event,HOST_EVENT_ALL,view);
    }
    /* Replacements include descendants so no retained pointer belongs to a
     * deleted parent. All allocations precede input or old-tree mutation. */
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *n = &snapshot->nodes[i];
        struct canopus_ui_host_node *slot = &view->nodes[map[i]];
        const struct canopus_ui_node_v1 *old = host_previous_key(previous,n->key);
        uint32_t oi = old != NULL ? (uint32_t)(old - previous->nodes) : 0;
        uint32_t parent_key = n->parent == CANOPUS_UI_NO_NODE ? 0u : snapshot->nodes[n->parent].key;
        fresh[i] = slot->object == NULL || slot->key != n->key || slot->type != n->type || slot->parent_key != parent_key ||
            (old != NULL && (host_local_style(&previous->styles[oi],&previous->visuals[oi]) &
                             ~host_local_style(&snapshot->styles[i],&snapshot->visuals[i]))) ||
            (n->parent != CANOPUS_UI_NO_NODE && fresh[n->parent]);
    }
    canopus_memset(candidates, 0, sizeof(candidates));
    if (fresh[0]) {
        const struct canopus_ui_node_v1 *old = host_previous_key(previous,snapshot->nodes[0].key);
        if (old != NULL && old->type == CANOPUS_UI_NODE_NAVIGATION_PAGE && view->nodes[map[0]].object != NULL) {
            content_candidate = host_content_create(view->root);
            if (content_candidate == NULL) { result = CANOPUS_UI_ERR_BACKEND; goto allocation_failed; }
            host_set_hidden(content_candidate,1u);
            host_add_event(content_candidate,host_content_event,HOST_EVENT_ALL,view);
#ifdef FW_UI_OBJECT_GET_SCROLL_Y
            retained_scroll = ((int32_t (*)(void *))(uintptr_t)FW_UI_OBJECT_GET_SCROLL_Y)(view->content_root);
#endif
        }
    }
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *n = &snapshot->nodes[i];
        struct canopus_ui_host_node *slot = &view->nodes[map[i]];
        void *parent = n->parent == CANOPUS_UI_NO_NODE ? view->content_root :
            (fresh[n->parent] ? candidates[n->parent].object : view->nodes[map[n->parent]].object);
        if (host_is_title(n->type)) {
            if (fresh[i]) candidates[i].object = content_candidate != NULL ? content_candidate : view->content_root;
            result = host_feedback_title_prepare(view,snapshot,n,previous,&candidates[i].primary);
            if (result != 0) goto allocation_failed;
        } else if (n->type == CANOPUS_UI_NODE_TOAST) {
            if (host_feedback_toast_driver(snapshot,i) &&
                (((n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) && view->toast_object == NULL) ||
                 /* A drivership switch repaints through a fresh label: the new
                  * driver's style bytes can be unchanged relative to its own
                  * previous node, so a self-diff would never write them (and
                  * the old driver's style would linger). */
                 ((n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) && view->toast_key != n->key &&
                  view->toast_object != NULL) ||
                 /* A hidden node never owns displayed content, so it must not
                  * replace the owner either (its style would ride along). */
                 ((n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) && fresh[i] &&
                  view->toast_object != NULL))) {
                candidates[i].object = host_label_create(view->root);
                if (candidates[i].object == NULL) { result = CANOPUS_UI_ERR_BACKEND; goto allocation_failed; }
                host_set_hidden(candidates[i].object,1u);
            }
        } else if (fresh[i]) {
            result = host_create_node(&candidates[i], parent, snapshot, i);
            if (result != 0) goto allocation_failed;
            host_set_hidden(candidates[i].object, 1u);
        } else if (n->type == CANOPUS_UI_NODE_SECTION && n->primary_len && slot->primary == NULL) {
            candidates[i].primary = host_label_create(slot->object);
            if (candidates[i].primary == NULL) { result = CANOPUS_UI_ERR_BACKEND; goto allocation_failed; }
            host_set_hidden(candidates[i].primary,1u);
        }
    }
    for (i = 0; i < snapshot->node_count; i++) {
        struct canopus_ui_host_node *slot = &view->nodes[map[i]];
        if ((snapshot->visuals[i].flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT) &&
            (fresh[i] || slot->extent_object == NULL)) {
            extent_candidates[i] = host_extent_create(fresh[i] ? candidates[i].object : slot->object);
            if (extent_candidates[i] == NULL) { result = CANOPUS_UI_ERR_BACKEND; goto allocation_failed; }
        }
    }
    if (view->rendered_generation != snapshot->generation) {
        host_input_prepare(view, snapshot);
        /* A CANCEL callback may close the page and let firmware destroy the
         * native root, which already reclaimed every staged candidate under
         * it. Never delete staged objects again once the view is gone. */
        if (!host_view_alive(view)) return CANOPUS_UI_ERR_STATE;
    }
    if (!unchanged_capture) host_input_before_replace(view);
    view->render_valid = 0;
#ifdef HOST_MSGBOX
    host_dialog_dismiss(view);
#endif
    /* Old snapshot order is parent-before-child, regardless of slot reuse. */
    for (j = previous != NULL ? previous->node_count : 0; j-- != 0;) {
        struct canopus_ui_host_node *slot = host_node(view,previous->nodes[j].key);
        if (slot == NULL) continue;
        i = (uint16_t)(slot-view->nodes);
        {
            uint16_t n;
            int replace = !(used & (UINT32_C(1) << i));
            for (n=0;n<snapshot->node_count;n++) if (map[n]==i && fresh[n]) replace=1;
            if (replace) {
                if (slot->extent_object != NULL && host_is_title(slot->type)) host_object_delete(slot->extent_object);
                if (!host_feedback_owned(slot->type)) {
                    host_object_delete(slot->object);
                    host_forget_descendants(view, slot->key);
                }
                canopus_memset(slot,0,sizeof(*slot));
            }
        }
    }
    for (i = CANOPUS_UI_MAX_NODES; i-- != 0;) {
        struct canopus_ui_host_node *slot = &view->nodes[i];
        int replace = 0;
        for (j = 0; j < snapshot->node_count; j++) if (map[j] == i && fresh[j]) replace = 1;
        if (slot->object != NULL && (!(used & (UINT32_C(1) << i)) || replace)) {
            if (slot->extent_object != NULL && host_is_title(slot->type)) host_object_delete(slot->extent_object);
            if (!host_feedback_owned(slot->type)) {
                host_object_delete(slot->object);
                host_forget_descendants(view, slot->key);
            }
            canopus_memset(slot, 0, sizeof(*slot));
        }
    }
    if (content_candidate != NULL) {
        host_object_delete(view->content_root);
        view->content_root = content_candidate;
    }
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *n = &snapshot->nodes[i];
        struct canopus_ui_host_node *slot = &view->nodes[map[i]];
        const struct canopus_ui_node_v1 *old = fresh[i] ? NULL : host_previous_key(previous,n->key);
        uint32_t oi = old != NULL ? (uint32_t)(old - previous->nodes) : 0;
        const struct canopus_ui_layout_v1 *l = &snapshot->layouts[i];
        const struct canopus_ui_visual_v1 *v = &snapshot->visuals[i];
        void *parent = n->parent == CANOPUS_UI_NO_NODE ? view->content_root : view->nodes[map[n->parent]].object;
        int show = (n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) != 0 && (n->parent == CANOPUS_UI_NO_NODE || view->nodes[map[n->parent]].visible);
        int text_changed = old == NULL || !host_text_equal(snapshot,n,previous,old,0);
        int secondary_changed = old == NULL || !host_text_equal(snapshot,n,previous,old,1);
        int style_changed = old == NULL || !host_bytes_equal(&snapshot->styles[i],&previous->styles[oi],sizeof(snapshot->styles[i])) || !host_bytes_equal(v,&previous->visuals[oi],sizeof(*v));
        const char *primary = n->primary_len ? snapshot->strings + n->primary_off : "";
        const char *secondary = n->secondary_len ? snapshot->strings + n->secondary_off : "";
        int padding_changed = old == NULL || l->padding_left != previous->layouts[oi].padding_left ||
            l->padding_right != previous->layouts[oi].padding_right || l->padding_top != previous->layouts[oi].padding_top ||
            l->padding_bottom != previous->layouts[oi].padding_bottom;
        if (host_is_title(n->type)) {
            host_feedback_title(view,snapshot,n,candidates[i].primary);
            slot->object = view->content_root; slot->event_object = slot->object; slot->type = n->type;
        } else if (n->type == CANOPUS_UI_NODE_TOAST) {
            /* Text rewrite implies style repaint: the shared owner's style is
             * only ever written here or on a staged replacement, never during
             * a hidden period where it could linger past a driver change. */
            if (host_feedback_toast(view,snapshot,n,previous,candidates[i].object) ||
                candidates[i].object != NULL)
                style_changed = 1;
            slot->object = view->toast_object; slot->type = n->type;
        } else {
            if (slot->object == NULL) {
                *slot = candidates[i];
                host_add_event(slot->event_object,host_row_events[map[i]],HOST_EVENT_ALL,NULL);
            } else if (candidates[i].primary != NULL) {
                slot->primary = candidates[i].primary;
                style_changed = 1;
                host_set_hidden(slot->primary,0u);
            }
            if (host_is_label(n->type) || slot->primary != NULL) {
                void *label = host_is_label(n->type) ? slot->object : slot->primary;
                if (style_changed) host_label_style(label,v->font_px == 28 || snapshot->styles[i].text_style == CANOPUS_UI_TEXT_TITLE ? CANOPUS_UI_TEXT_TITLE : CANOPUS_UI_TEXT_BODY);
                if (text_changed) host_label_set_text(label,primary);
                if (style_changed) host_style(label,&snapshot->styles[i],v);
                if (slot->kind == 4u && (fresh[i] || style_changed || old == NULL || old->flags != n->flags || slot->dirty)) {
                    int checked = (n->flags & (CANOPUS_UI_NODE_FLAG_CHECKED|CANOPUS_UI_NODE_FLAG_SELECTED)) != 0;
                    host_label_style(slot->indicator,CANOPUS_UI_TEXT_BODY);
                    host_label_set_text(slot->indicator,n->type == CANOPUS_UI_NODE_RADIO_ROW ? (checked ? "(●)" : "(○)") : (n->flags & CANOPUS_UI_NODE_FLAG_INDETERMINATE ? "[-]" : checked ? "[✓]" : "[ ]"));
                    host_label_style(slot->secondary,CANOPUS_UI_TEXT_BODY);
                }
                if (slot->secondary != NULL && secondary_changed) host_label_set_text(slot->secondary,secondary);
            } else if (host_is_row(n->type)) {
                if (old == NULL || text_changed || secondary_changed || old->flags != n->flags || slot->dirty) {
                    uint8_t selected = slot->kind != HOST_ROW_SWITCH || (n->flags & (CANOPUS_UI_NODE_FLAG_CHECKED|CANOPUS_UI_NODE_FLAG_SELECTED)) != 0;
#ifndef CANOPUS_MGR_BAND11
                    ((void (*)(void *,const void *,const char *,const char *,int32_t,uint8_t))(uintptr_t)FW_LVX_LIST_ROW_UPDATE)(slot->object,NULL,primary,secondary,0,selected);
#else
                    ((void (*)(void *,const void *,const char *,const char *,int32_t,uint8_t))(uintptr_t)FW_LVX_LIST_ITEM_UPDATE)(slot->object,NULL,primary,secondary,0,selected);
#endif
                }
            } else if (n->type == CANOPUS_UI_NODE_IMAGE) {
                if (text_changed || old == NULL || snapshot->values[i].resource_id != previous->values[oi].resource_id) host_image_set_src(slot->object,primary);
            } else if (n->type == CANOPUS_UI_NODE_PROGRESS) {
                if (old == NULL || snapshot->values[i].minimum != previous->values[oi].minimum || snapshot->values[i].maximum != previous->values[oi].maximum) host_bar_set_range(slot->object,snapshot->values[i].minimum,snapshot->values[i].maximum);
                if (old == NULL || snapshot->values[i].value != previous->values[oi].value) host_bar_set_value(slot->object,snapshot->values[i].value);
            }
        }
        /* Headers are semantic handles to the page viewport. Toast geometry and
         * visibility belong to the feedback owner, outside the body layout. */
        if (n->type == CANOPUS_UI_NODE_NAVIGATION_HEADER) goto publish_node;
        /* Alias toast nodes share the owner with the driving node and must not
         * paint it: only a *visible* driver's style may reach the shared
         * object, so a hide-driver's self-diff style write cannot linger past
         * the next re-show of an earlier driver. */
        if (style_changed && slot->object != NULL &&
            (n->type != CANOPUS_UI_NODE_TOAST ||
             (host_feedback_toast_driver(snapshot,i) &&
              (n->flags & CANOPUS_UI_NODE_FLAG_VISIBLE))))
            host_style(slot->object,&snapshot->styles[i],v);
        if (n->type == CANOPUS_UI_NODE_TOAST) goto publish_node;
        if (fresh[i] || slot->width != geometry[i].width || slot->height != geometry[i].height)
            host_set_size(slot->object,geometry[i].width,geometry[i].height);
        if (slot->primary != NULL && (fresh[i] || candidates[i].primary != NULL || padding_changed || slot->width != geometry[i].width || slot->height != geometry[i].height)) {
            int32_t indicator = slot->kind == 4u ? 44 : 0;
            int32_t width = geometry[i].width - l->padding_left - l->padding_right - indicator;
            host_set_size(slot->primary,width,HOST_SIZE_CONTENT);
            host_align_to(slot->primary,slot->object,0u,l->padding_left + indicator,l->padding_top);
            if (slot->secondary != NULL) {
                host_set_size(slot->secondary,width,HOST_SIZE_CONTENT);
                host_align_to(slot->secondary,slot->primary,12u,0,4);
                host_set_size(slot->indicator,44,HOST_SIZE_CONTENT);
                host_align_to(slot->indicator,slot->object,0u,l->padding_left,l->padding_top);
            }
        }
        if (!host_is_title(n->type) && n->type != CANOPUS_UI_NODE_TOAST && (fresh[i] || slot->x != geometry[i].x || slot->y != geometry[i].y || (old != NULL && (v->translate_x != previous->visuals[oi].translate_x || v->translate_y != previous->visuals[oi].translate_y))))
            host_align_to(slot->object,parent,0u,geometry[i].x + v->translate_x,geometry[i].y + v->translate_y);
        if (fresh[i] || slot->visible != show) host_set_hidden(slot->object,!show);
        if (extent_candidates[i] != NULL) slot->extent_object = extent_candidates[i];
        if (slot->extent_object != NULL) {
            int32_t height = (v->flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT) ? (int32_t)v->content_height : 0;
            if (extent_candidates[i] != NULL || slot->extent_height != height) {
                if (height > 0) host_align_to(slot->extent_object,slot->object,0u,0,height - 1);
                host_set_hidden(slot->extent_object,height == 0);
                slot->extent_height = height;
            }
        }
        {
            uint8_t locked = !(n->type == CANOPUS_UI_NODE_NAVIGATION_PAGE || n->type == CANOPUS_UI_NODE_SCROLL || n->type == CANOPUS_UI_NODE_LIST) || geometry[i].extent <= geometry[i].height;
            if (fresh[i] || slot->scroll_locked != locked) host_object_flag(slot->object,16u,!locked);
            slot->scroll_locked = locked;
            if (n->type == CANOPUS_UI_NODE_NAVIGATION_PAGE) view->scroll_locked = locked;
        }
publish_node:
        slot->key = n->key; slot->parent_key = n->parent == CANOPUS_UI_NO_NODE ? 0 : snapshot->nodes[n->parent].key;
        slot->x = geometry[i].x; slot->y = geometry[i].y; slot->width = geometry[i].width; slot->height = geometry[i].height;
        slot->visible = (uint8_t)show; slot->dirty = 0;
        slot->binding.generation = snapshot->generation; slot->binding.key = n->key; slot->binding.event_id = n->event_id;
    }
#ifdef FW_UI_OBJECT_SCROLL_TO_Y
    /* Position only after every child and native extent marker is published;
     * native scroll_to updates layout and clamps against those child bounds. */
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *n = &snapshot->nodes[i];
        const struct canopus_ui_visual_v1 *v = &snapshot->visuals[i];
        const struct canopus_ui_node_v1 *old = fresh[i] ? NULL : host_previous_key(previous,n->key);
        uint32_t oi = old != NULL ? (uint32_t)(old - previous->nodes) : 0;
        if (host_input_scroll_type(n->type) &&
            ((v->flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT &&
              (old == NULL || previous->visuals[oi].scroll_y != v->scroll_y)) ||
             (n->type == CANOPUS_UI_NODE_NAVIGATION_PAGE && fresh[i])))
            ((void (*)(void *,int32_t,uint32_t))(uintptr_t)FW_UI_OBJECT_SCROLL_TO_Y)(view->nodes[map[i]].object,v->scroll_y,0);
    }
#endif
#ifdef FW_UI_OBJECT_SCROLL_TO_Y
    if (content_candidate != NULL && !(snapshot->visuals[0].flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT))
        ((void (*)(void *,int32_t,uint32_t))(uintptr_t)FW_UI_OBJECT_SCROLL_TO_Y)(view->content_root,retained_scroll,0u);
#else
    (void)retained_scroll;
#endif
    host_feedback_finish(view,snapshot);
    view->rendered_generation = snapshot->generation; view->render_valid = 1;
    host_input_publish(view,snapshot,geometry[0].extent,unchanged_capture);
    if (view->hooks != NULL && view->hooks->rendered != NULL) view->hooks->rendered(view,view->page_title);
    return 0;
allocation_failed:
    for (j = snapshot->node_count; j-- != 0;) {
        if (extent_candidates[j] != NULL) host_object_delete(extent_candidates[j]);
        if ((host_is_title(snapshot->nodes[j].type) || !fresh[j]) && candidates[j].primary != NULL)
            host_object_delete(candidates[j].primary);
        if (candidates[j].object != NULL && !host_is_title(snapshot->nodes[j].type)) host_object_delete(candidates[j].object);
    }
    if (content_candidate != NULL) host_object_delete(content_candidate);
    /* Allocation failures leave the previous tree and input state published. */
    return result;
}
const struct canopus_ui_backend_v1 canopus_ui_host_backend = {
    sizeof(struct canopus_ui_backend_v1),CANOPUS_UI_ABI_MAJOR,CANOPUS_UI_ABI_MINOR,
    CANOPUS_UI_CAP_EXTENDED_COMPONENTS|CANOPUS_UI_CAP_STYLE|CANOPUS_UI_CAP_LAYOUT|CANOPUS_UI_CAP_VALUES|
    CANOPUS_UI_CAP_NAVIGATION_HEADER|CANOPUS_UI_CAP_IMAGE|CANOPUS_UI_CAP_PROGRESS|CANOPUS_UI_CAP_VISUAL|
    CANOPUS_UI_CAP_TOAST|CANOPUS_UI_CAP_VIRTUAL_CONTENT|HOST_INPUT_CAPS,host_apply,host_control
};
void canopus_ui_host_attach(struct canopus_ui_host_view *view, void *root, void *page,
                            const struct canopus_ui_host_hooks *hooks, uint32_t flags)
{
    uint32_t i;
    if (view == NULL) return;
    canopus_ui_host_detach(view); canopus_memset(view,0,sizeof(*view));
    view->root=root; view->firmware_page=page; view->hooks=hooks; view->flags=flags; view->active=1;
    for (i=0;i<CANOPUS_UI_HOST_MAX_VIEWS;i++) if (s_views[i]==NULL) { s_views[i]=view; return; }
    view->active=0;
}
void canopus_ui_host_bind(struct canopus_ui_host_view *view, struct canopus_ui_context_v1 *ui)
{ if (view != NULL) view->ui=ui; }
void canopus_ui_host_set_interactive(struct canopus_ui_host_view *view, int interactive)
{
    if (view == NULL) return;
    if (!interactive && view->interactive) { host_input_cancel(view); if (!host_view_alive(view)) return; host_feedback_clear(view); }
    view->interactive=interactive != 0;
}
void canopus_ui_host_detach(struct canopus_ui_host_view *view)
{
    uint32_t i;
    if (view == NULL) return;
    /* Detach forgets page-owned objects; firmware destroys their parent.
     * Feedback and dialog natives are page-owned: never touch them here,
     * because the parent may already have reclaimed them on close. */
    if (host_view_alive(view)) { host_input_cancel(view); if (!host_view_alive(view)) return; }
    host_input_forget(view);
    for(i=0;i<CANOPUS_UI_HOST_MAX_VIEWS;i++) if(s_views[i]==view) s_views[i]=NULL;
    canopus_memset(view,0,sizeof(*view));
}
