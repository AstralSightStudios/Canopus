/* Included by canopus_ui_host.c after the keyed object/translation helpers.
 * Firmware calls are enabled only by exact-target generated FW_UI_* bindings.
 * Normalized routing is separate so host tests exercise the production state. */
#if !defined(FW_UI_OBJECT_SCROLL_TO_Y) && defined(FW_UI_OBJECT_SCROLL_TO) && \
    defined(FW_UI_OBJECT_GET_SCROLL_X)
static void host_input_scroll_to_y(void *object, int32_t y, uint32_t animation)
{
    int32_t x = ((int32_t (*)(void *))(uintptr_t)FW_UI_OBJECT_GET_SCROLL_X)(object);
    ((void (*)(void *, int32_t, int32_t, uint32_t))(uintptr_t)FW_UI_OBJECT_SCROLL_TO)(
        object, x, y, animation);
}
#define FW_UI_OBJECT_SCROLL_TO_Y host_input_scroll_to_y
#endif
#if !defined(FW_UI_OBJECT_GET_COORDS) && defined(FW_UI_OBJECT_COORDS_OFFSET)
static void host_input_get_coords(void *object, FW_UI_COORD_TYPE *out)
{
    const FW_UI_COORD_TYPE *coords = (const FW_UI_COORD_TYPE *)
        ((const uint8_t *)object + FW_UI_OBJECT_COORDS_OFFSET);
    uint32_t i;
    for (i = 0u; i < 4u; i++) out[i] = coords[i];
}
#define FW_UI_OBJECT_GET_COORDS host_input_get_coords
#endif
#define HOST_INPUT_CAPABILITIES CANOPUS_UI_CAP_TRANSLATE
#if defined(FW_UI_OBJECT_GET_SCROLL_Y) && defined(FW_UI_OBJECT_SCROLL_TO_Y)
#define HOST_INPUT_SCROLL_CAPABILITIES CANOPUS_UI_CAP_SCROLL
#else
#define HOST_INPUT_SCROLL_CAPABILITIES 0u
#endif
#if defined(FW_UI_COORD_TYPE) && defined(FW_UI_EVENT_GET_INDEV) && \
    defined(FW_UI_INDEV_GET_POINT) && defined(FW_UI_OBJECT_GET_COORDS) && \
    defined(FW_UI_EVENT_PRESSED) && defined(FW_UI_EVENT_PRESSING) && \
    defined(FW_UI_EVENT_LONG_PRESSED) && defined(FW_UI_EVENT_RELEASED) && \
    defined(FW_UI_EVENT_PRESS_LOST) && defined(FW_UI_EVENT_SCROLL)
#define HOST_INPUT_NATIVE_CAPABILITIES (CANOPUS_UI_CAP_POINTER_EVENTS | CANOPUS_UI_CAP_CAPTURE)
#else
#define HOST_INPUT_NATIVE_CAPABILITIES 0u
#endif
#define HOST_INPUT_CAPS (HOST_INPUT_CAPABILITIES | HOST_INPUT_SCROLL_CAPABILITIES | \
                         HOST_INPUT_NATIVE_CAPABILITIES)

struct host_input_state {
    struct canopus_ui_host_view *view;
    struct canopus_ui_host_binding press_binding, capture_binding;
    int32_t x, y;
    uint32_t time_ms;
    uint8_t pressed, captured;
    uint32_t locked_slots;
};
static struct host_input_state s_input[CANOPUS_UI_HOST_MAX_VIEWS];

static struct host_input_state *host_input_state(struct canopus_ui_host_view *view)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_input[i].view == view) return &s_input[i];
    }
    for (i = 0u; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_input[i].view == NULL) {
            s_input[i].view = view;
            return &s_input[i];
        }
    }
    return NULL;
}

static struct canopus_ui_host_node *host_input_node(struct canopus_ui_host_view *view,
                                                    uint32_t key)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_UI_MAX_NODES; i++) {
        if (view->nodes[i].object != NULL && view->nodes[i].key == key &&
            view->nodes[i].visible) return &view->nodes[i];
    }
    return NULL;
}

static int host_input_scroll_type(uint16_t type)
{
    return type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
           type == CANOPUS_UI_NODE_LIST || type == CANOPUS_UI_NODE_SCROLL;
}

static int32_t host_input_metrics(struct canopus_ui_host_view *view, uint32_t key,
                                 struct canopus_ui_scroll_metrics_v1 *out)
{
#ifdef FW_UI_OBJECT_GET_SCROLL_Y
    struct canopus_ui_host_node *node = host_input_node(view, key);
    const struct canopus_ui_snapshot_v1 *snapshot = canopus_ui_current(view->ui);
    void *object = host_input_object(view, key);
    uint32_t i;
    int32_t height, extent = 0;
    if (node == NULL || object == NULL || !host_input_scroll_type(node->type))
        return CANOPUS_UI_ERR_UNSUPPORTED;
    height = node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE ? HOST_CONTENT_HEIGHT : node->height;
    if (height <= 0) return CANOPUS_UI_ERR_UNSUPPORTED;
    if (node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE) extent = view->content_height;
    for (i = 0u; i < CANOPUS_UI_MAX_NODES; i++) {
        struct canopus_ui_host_node *child = &view->nodes[i];
        if (child->visible && child->parent_key == key && child->y + child->height > extent)
            extent = child->y + child->height;
    }
    if (snapshot != NULL) {
        for (i = 0u; i < snapshot->node_count; i++) {
            if (snapshot->nodes[i].key == key &&
                snapshot->visuals[i].content_height > (uint32_t)extent)
                extent = (int32_t)snapshot->visuals[i].content_height;
        }
    }
    out->y = ((int32_t (*)(void *))(uintptr_t)FW_UI_OBJECT_GET_SCROLL_Y)(object);
    out->viewport_height = height;
    out->content_height = extent > height ? extent : height;
    return CANOPUS_UI_OK;
#else
    (void)view; (void)key; (void)out;
    return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
}

static void host_input_restore(struct host_input_state *state)
{
    uint32_t i;
    /* Restore before replacing slots or detaching the view, while the old
     * objects are alive. Only previously scrollable ancestors have bits set. */
    for (i = 0u; state->locked_slots != 0u; i++) {
        uint32_t bit = UINT32_C(1) << i;
        if (state->locked_slots & bit) {
            state->locked_slots &= ~bit;
            host_object_flag(state->view->nodes[i].object, UINT32_C(16), 1);
        }
    }
    state->captured = 0u;
}

static void host_input_lock(struct canopus_ui_host_view *view,
                            struct host_input_state *state, uint32_t key)
{
    struct canopus_ui_host_node *node = host_input_node(view, key);
    uint32_t depth;
    state->captured = 1u;
    for (depth = 0u; node != NULL && depth < CANOPUS_UI_MAX_NODES; depth++) {
        void *object = host_input_object(view, node->key);
        uint8_t locked = node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE
                             ? view->scroll_locked : node->scroll_locked;
        if (host_input_scroll_type(node->type) && object != NULL && !locked) {
            host_object_flag(object, UINT32_C(16), 0);
            state->locked_slots |= UINT32_C(1) << (uint32_t)(node - view->nodes);
        }
        if (node->parent_key == 0u) break;
        node = host_input_node(view, node->parent_key);
    }
}

static int32_t host_input_send(struct canopus_ui_host_view *view,
                               const struct canopus_ui_host_binding *binding,
                               uint32_t type, uint32_t time_ms, int32_t x, int32_t y,
                               int32_t dx, int32_t dy)
{
    struct canopus_ui_event_v1 event;
    struct canopus_ui_scroll_metrics_v1 metrics;
    struct canopus_ui_host_node *node;
    canopus_memset(&event, 0, sizeof(event));
    event.struct_size = sizeof(event);
    event.generation = binding->generation;
    event.key = binding->key;
    event.event_id = binding->event_id;
    event.type = type;
    event.time_ms = time_ms;
    event.x = x; event.y = y;
    event.delta_x = dx; event.delta_y = dy;
    node = host_input_node(view, binding->key);
    while (node != NULL && !host_input_scroll_type(node->type))
        node = host_input_node(view, node->parent_key);
    if (node != NULL && host_input_metrics(view, node->key, &metrics) == 0) {
        event.scroll_y = metrics.y;
        event.viewport_height = metrics.viewport_height;
        event.content_height = metrics.content_height;
    }
    return canopus_ui_dispatch_event(view->ui, &event);
}

static void host_input_cancel(struct canopus_ui_host_view *view)
{
    struct host_input_state *state = host_input_state(view);
    struct canopus_ui_host_binding binding;
    int32_t x, y;
    uint32_t time_ms;
    if (state == NULL) return;
    binding = state->captured ? state->capture_binding : state->press_binding;
    x = state->x; y = state->y; time_ms = state->time_ms;
    if (view->hooks != NULL && view->hooks->time_ms != NULL)
        time_ms = view->hooks->time_ms(view);
#ifdef FW_UI_TICK_GET
    else time_ms = ((uint32_t (*)(void))(uintptr_t)FW_UI_TICK_GET)();
#endif
    if (!state->pressed) { host_input_restore(state); return; }
    state->pressed = 0u;
    host_input_restore(state);
    if (view->ui != NULL) (void)host_input_send(view, &binding,
        CANOPUS_UI_EVENT_CANCEL, time_ms, x, y, 0, 0);
}

static void host_input_forget(struct canopus_ui_host_view *view)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        if (s_input[i].view == view) canopus_memset(&s_input[i], 0, sizeof(s_input[i]));
    }
}

static __attribute__((unused)) int32_t host_input_route(struct canopus_ui_host_view *view,
                                const struct canopus_ui_host_binding *source,
                                uint32_t type, uint32_t time_ms, int32_t x, int32_t y)
{
    struct host_input_state *state;
    struct canopus_ui_host_binding binding;
    int32_t dx = 0, dy = 0;
    if (view == NULL || !view->active || !view->interactive || view->ui == NULL ||
        source == NULL || source->generation != view->rendered_generation)
        return CANOPUS_UI_ERR_STALE;
    state = host_input_state(view);
    if (state == NULL) return CANOPUS_UI_ERR_CAPACITY;
    binding = *source;
    if (type == CANOPUS_UI_EVENT_PRESS) {
        if (state->pressed) return CANOPUS_UI_ERR_STATE;
        state->press_binding = binding;
        state->pressed = 1u;
    } else if (type != CANOPUS_UI_EVENT_SCROLL) {
        if (!state->pressed || state->press_binding.generation != source->generation)
            return CANOPUS_UI_ERR_STALE;
        /* Capture temporarily redirects the press and suppresses native ancestor
         * scrolling. Releasing it restores the original press destination. */
        binding = state->captured ? state->capture_binding : state->press_binding;
        dx = x - state->x; dy = y - state->y;
    }
    state->time_ms = time_ms;
    state->x = x; state->y = y;
    if (type == CANOPUS_UI_EVENT_RELEASE || type == CANOPUS_UI_EVENT_CANCEL) {
        state->pressed = 0u;
        host_input_restore(state);
    }
    return host_input_send(view, &binding, type, time_ms, x, y, dx, dy);
}

static int32_t host_control(void *cookie, uint32_t generation, canopus_ui_node_id key,
                            uint32_t op, int32_t a, int32_t b, void *out)
{
    struct canopus_ui_host_view *view = cookie;
    struct host_input_state *state;
    struct canopus_ui_host_node *node;
    if (view == NULL || !view->active || !view->render_valid ||
        generation != view->rendered_generation) return CANOPUS_UI_ERR_STALE;
    node = host_input_node(view, key);
    if (node == NULL) return CANOPUS_UI_ERR_STALE;
    if (op == CANOPUS_UI_CONTROL_SCROLL_QUERY) {
        if (out == NULL || a != 0 || b != 0) return CANOPUS_UI_ERR_ARGUMENT;
        return host_input_metrics(view, key, out);
    }
    if (op == CANOPUS_UI_CONTROL_SCROLL_TO) {
#if defined(FW_UI_OBJECT_SCROLL_TO_Y) && defined(FW_UI_OBJECT_GET_SCROLL_Y)
        struct canopus_ui_scroll_metrics_v1 metrics;
        int32_t maximum, rc;
        if (b != 0 || out != NULL) return CANOPUS_UI_ERR_ARGUMENT;
        rc = host_input_metrics(view, key, &metrics);
        if (rc != 0) return rc;
        maximum = metrics.content_height - metrics.viewport_height;
        if (a < 0) a = 0;
        if (a > maximum) a = maximum;
        ((void (*)(void *, int32_t, uint32_t))(uintptr_t)FW_UI_OBJECT_SCROLL_TO_Y)(
            host_input_object(view, key), a, 0u);
        return CANOPUS_UI_OK;
#else
        return CANOPUS_UI_ERR_UNSUPPORTED;
#endif
    }
    if (op == CANOPUS_UI_CONTROL_TRANSLATE) {
        if (out != NULL) return CANOPUS_UI_ERR_ARGUMENT;
        return host_input_translate(view, key, a, b);
    }
    if (op != CANOPUS_UI_CONTROL_CAPTURE && op != CANOPUS_UI_CONTROL_RELEASE_CAPTURE)
        return CANOPUS_UI_ERR_UNSUPPORTED;
    if (a != 0 || b != 0 || out != NULL) return CANOPUS_UI_ERR_ARGUMENT;
    state = host_input_state(view);
    if (state == NULL) return CANOPUS_UI_ERR_CAPACITY;
    if (op == CANOPUS_UI_CONTROL_RELEASE_CAPTURE) {
        if (state->captured && state->capture_binding.key != key) return CANOPUS_UI_ERR_STATE;
        host_input_restore(state);
        return CANOPUS_UI_OK;
    }
    if (!view->interactive || !state->pressed) return CANOPUS_UI_ERR_STATE;
    host_input_restore(state);
    state->capture_binding = node->binding;
    host_input_lock(view, state, key);
    return CANOPUS_UI_OK;
}

static uint32_t host_input_page_key(const struct canopus_ui_snapshot_v1 *snapshot)
{
    uint32_t i;
    if (snapshot != NULL) for (i = 0u; i < snapshot->node_count; i++)
        if (snapshot->nodes[i].type == CANOPUS_UI_NODE_NAVIGATION_PAGE)
            return snapshot->nodes[i].key;
    return 0u;
}

static int host_input_binding_valid(const struct canopus_ui_snapshot_v1 *snapshot,
                                    const struct canopus_ui_host_binding *binding)
{
    uint32_t i;
    for (i = 0u; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        if (node->key != binding->key || node->event_id != binding->event_id) continue;
        /* ENABLED applies to interactive nodes; scroll/page surfaces accept
         * pointer events without it, just as the runtime dispatcher does. */
        if ((node->type == CANOPUS_UI_NODE_BUTTON ||
             node->type == CANOPUS_UI_NODE_ACTION_ROW ||
             node->type == CANOPUS_UI_NODE_SWITCH_ROW ||
             node->type == CANOPUS_UI_NODE_CHECKBOX ||
             node->type == CANOPUS_UI_NODE_RADIO_ROW ||
             ((node->type == CANOPUS_UI_NODE_ICON ||
               node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER) && node->event_id != 0u)) &&
            (node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u) return 0;
        for (;;) {
            if ((node->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) == 0u) return 0;
            if (node->parent == CANOPUS_UI_NO_NODE) return 1;
            node = &snapshot->nodes[node->parent];
        }
    }
    return 0;
}

/* Called only after all candidate allocations succeed. Send CANCEL while the
 * old snapshot and native ancestor chain still accept the active binding. */
static void host_input_prepare(struct canopus_ui_host_view *view,
                               const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct host_input_state *state = host_input_state(view);
    if (state == NULL || !state->pressed) return;
    if (host_input_page_key(canopus_ui_current(view->ui)) == host_input_page_key(snapshot) &&
        host_input_binding_valid(snapshot, &state->press_binding) &&
        (!state->captured || host_input_binding_valid(snapshot, &state->capture_binding))) return;
    host_input_cancel(view);
}

/* Called only after all candidate allocations succeed, while the old native
 * ancestors are still alive. Preserve capture intent across the replacement. */
static void host_input_before_replace(struct canopus_ui_host_view *view)
{
    struct host_input_state *state = host_input_state(view);
    uint8_t captured;
    if (state == NULL) return;
    captured = state->captured;
    host_input_restore(state);
    state->captured = captured;
}

static void host_input_publish(struct canopus_ui_host_view *view,
                               const struct canopus_ui_snapshot_v1 *snapshot,
                               uint32_t height, int preserve_capture)
{
    struct host_input_state *state = host_input_state(view);
    uint8_t captured;
    view->content_height = (int32_t)height;
    if (state == NULL || !state->pressed) return;
    /* An identical snapshot retains the native lock chain, including an empty
     * chain. Other commits restore before reacquiring the published ancestors. */
    captured = state->captured;
    if (!preserve_capture) host_input_restore(state);
    state->press_binding.generation = snapshot->generation;
    if (captured) {
        state->capture_binding.generation = snapshot->generation;
        if (!preserve_capture) host_input_lock(view, state, state->capture_binding.key);
    }
}

static void host_input_native(struct canopus_ui_host_view *view,
                              const struct canopus_ui_host_binding *binding,
                              void *native_event)
{
#if defined(FW_UI_COORD_TYPE) && defined(FW_UI_EVENT_GET_INDEV) && \
    defined(FW_UI_INDEV_GET_POINT) && defined(FW_UI_OBJECT_GET_COORDS) && \
    defined(FW_UI_EVENT_PRESSED) && \
    defined(FW_UI_EVENT_PRESSING) && defined(FW_UI_EVENT_LONG_PRESSED) && \
    defined(FW_UI_EVENT_RELEASED) && defined(FW_UI_EVENT_PRESS_LOST) && \
    defined(FW_UI_EVENT_SCROLL)
    uint32_t code, type, time_ms;
    FW_UI_COORD_TYPE point[2], area[4];
    void *indev;
    if (native_event == NULL || binding == NULL || view->content_root == NULL) return;
#ifndef CANOPUS_MGR_BAND11
    code = ((uint32_t (*)(void *))(uintptr_t)FW_LVX_EVENT_GET_CODE)(native_event);
#else
    code = *(const uint16_t *)((const uint8_t *)native_event + 8u) & UINT16_C(0x7fff);
#endif
    if (code == FW_UI_EVENT_PRESSED) type = CANOPUS_UI_EVENT_PRESS;
    else if (code == FW_UI_EVENT_PRESSING) type = CANOPUS_UI_EVENT_MOVE;
    else if (code == FW_UI_EVENT_LONG_PRESSED) type = CANOPUS_UI_EVENT_LONG_PRESS;
    else if (code == FW_UI_EVENT_RELEASED) type = CANOPUS_UI_EVENT_RELEASE;
    else if (code == FW_UI_EVENT_PRESS_LOST) type = CANOPUS_UI_EVENT_CANCEL;
    else if (code == FW_UI_EVENT_SCROLL) type = CANOPUS_UI_EVENT_SCROLL;
    else return;
    if (view->hooks != NULL && view->hooks->time_ms != NULL)
        time_ms = view->hooks->time_ms(view);
#ifdef FW_UI_TICK_GET
    else time_ms = ((uint32_t (*)(void))(uintptr_t)FW_UI_TICK_GET)();
#else
    else return; /* A synthetic timestamp is not a native input sample. */
#endif
    if (type == CANOPUS_UI_EVENT_SCROLL) {
        struct host_input_state *state = host_input_state(view);
        (void)host_input_route(view, binding, type, time_ms,
                               state != NULL ? state->x : 0,
                               state != NULL ? state->y : 0);
        return; /* Programmatic scroll events carry no input device. */
    }
    indev = ((void *(*)(void *))(uintptr_t)FW_UI_EVENT_GET_INDEV)(native_event);
    if (indev == NULL) return;
#ifdef FW_UI_UPDATE_LAYOUT
    ((void (*)(void *))(uintptr_t)FW_UI_UPDATE_LAYOUT)(view->content_root);
#endif
    ((void (*)(void *, FW_UI_COORD_TYPE *))(uintptr_t)FW_UI_INDEV_GET_POINT)(indev, point);
    ((void (*)(void *, FW_UI_COORD_TYPE *))(uintptr_t)FW_UI_OBJECT_GET_COORDS)(view->content_root, area);
    (void)host_input_route(view, binding, type, time_ms,
                           (int32_t)point[0] - area[0], (int32_t)point[1] - area[1]);
#else
    (void)view; (void)binding; (void)native_event;
#endif
}
