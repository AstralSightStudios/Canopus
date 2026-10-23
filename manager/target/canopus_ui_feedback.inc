/* Page-owned native feedback. The owner heartbeat carries no page pointers. */
static uint32_t host_feedback_hash(const char *text)
{
    uint32_t hash = 2166136261u;
    while (*text != '\0') hash = (hash ^ (uint8_t)*text++) * 16777619u;
    return hash;
}

static uint32_t host_feedback_now(struct canopus_ui_host_view *view)
{
    return view->hooks != NULL && view->hooks->time_ms != NULL
        ? view->hooks->time_ms(view) : view->feedback_clock;
}

static void host_feedback_clear(struct canopus_ui_host_view *view)
{
    if (view->toast_visible) host_set_hidden(view->toast_object, 1u);
    view->toast_visible = 0u;
    view->toast_deadline = 0u;
}

/* One shared page_title: the first navigation header drives it (binding
 * included), or the page root's title when the snapshot carries no header.
 * Only the page root used to yield; two headers still fought over the owner
 * and rewrote the shared binding on every commit. */
static const struct canopus_ui_node_v1 *host_feedback_title_driver(
    const struct canopus_ui_snapshot_v1 *snapshot)
{
    uint16_t i;
    const struct canopus_ui_node_v1 *page = NULL;
    for (i = 0; i < snapshot->node_count; i++) {
        if (snapshot->nodes[i].type == CANOPUS_UI_NODE_NAVIGATION_HEADER)
            return &snapshot->nodes[i];
        if (page == NULL && snapshot->nodes[i].type == CANOPUS_UI_NODE_NAVIGATION_PAGE)
            page = &snapshot->nodes[i];
    }
    return page;
}

static int32_t host_feedback_title_prepare(struct canopus_ui_host_view *view,
                                    const struct canopus_ui_snapshot_v1 *snapshot,
                                    const struct canopus_ui_node_v1 *node,
                                    const struct canopus_ui_snapshot_v1 *previous,
                                    void **replacement)
{
    const struct canopus_ui_node_v1 *old = host_previous_key(previous, node->key);
    uint8_t back = node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER
        ? (node->flags & CANOPUS_UI_NODE_FLAG_HEADER_BACK) != 0u
        : (view->flags & CANOPUS_UI_HOST_ROOT_PAGE) == 0u;
    if (node != host_feedback_title_driver(snapshot)) return CANOPUS_UI_OK;
    if (view->page_title == NULL || old == NULL || old->type != node->type ||
        view->title_binding.key != node->key || old->event_id != node->event_id ||
        back != view->title_back || !host_text_equal(snapshot, node, previous, old, 0)) {
        *replacement = host_title_create(view, snapshot->strings + node->primary_off, back);
        if (*replacement == NULL) return CANOPUS_UI_ERR_BACKEND;
        host_set_hidden(*replacement, 1u);
    }
    return CANOPUS_UI_OK;
}

static void host_feedback_title(struct canopus_ui_host_view *view,
                                const struct canopus_ui_snapshot_v1 *snapshot,
                                const struct canopus_ui_node_v1 *node,
                                void *replacement)
{
    if (node != host_feedback_title_driver(snapshot)) return;
    if (replacement != NULL) {
        if (view->page_title != NULL) host_object_delete(view->page_title);
        view->page_title = replacement;
        view->title_back = node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER
            ? (node->flags & CANOPUS_UI_NODE_FLAG_HEADER_BACK) != 0u
            : (view->flags & CANOPUS_UI_HOST_ROOT_PAGE) == 0u;
        if (++view->title_token == 0u) view->title_token = 1u;
        host_set_hidden(replacement, 0u);
    }
    view->title_binding.generation = snapshot->generation;
    view->title_binding.key = node->key;
    view->title_binding.event_id = node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER
        ? node->event_id : 0u;
}

/* The most recent *visible* toast node drives the shared owner object ("one
 * toast at a time"); earlier and hidden toast nodes are aliases and must not
 * touch the owner, or every keep-alive re-commit would restart the timer and a
 * trailing hidden placeholder would hide the visible toast ahead of it. With
 * no visible toast in the snapshot, the last toast node drives the hide. */
static int host_feedback_toast_driver(const struct canopus_ui_snapshot_v1 *snapshot,
                                      uint32_t index)
{
    uint32_t i;
    int later_toast = 0, later_visible = 0, any_visible = 0;
    for (i = 0u; i < snapshot->node_count; i++) {
        int visible = (snapshot->nodes[i].flags & CANOPUS_UI_NODE_FLAG_VISIBLE) != 0u;
        if (snapshot->nodes[i].type != CANOPUS_UI_NODE_TOAST) continue;
        if (visible) any_visible = 1;
        if (i > index) {
            later_toast = 1;
            if (visible) later_visible = 1;
        }
    }
    if ((snapshot->nodes[index].flags & CANOPUS_UI_NODE_FLAG_VISIBLE) != 0u)
        return !later_visible;
    return !later_toast && !any_visible;
}

/* Returns 1 when this call rewrote the owner's displayed content, so the
 * caller must repaint the driver's style along with it ("text rewrite implies
 * style repaint" — the two used to drift when style was written during a
 * hidden period and only text was replayed on re-show). */
static uint8_t host_feedback_toast(struct canopus_ui_host_view *view,
                                const struct canopus_ui_snapshot_v1 *snapshot,
                                const struct canopus_ui_node_v1 *node,
                                const struct canopus_ui_snapshot_v1 *previous,
                                void *replacement)
{
    uint32_t index = (uint32_t)(node - snapshot->nodes);
    const char *text = snapshot->strings + node->primary_off;
    uint32_t hash = host_feedback_hash(text);
    const struct canopus_ui_node_v1 *old = host_previous_key(previous, node->key);
    int32_t duration = snapshot->values[index].value;
    uint8_t same = old != NULL && old->type == CANOPUS_UI_NODE_TOAST &&
        (old->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) != 0u &&
        view->toast_key == node->key && view->toast_text_hash == hash &&
        previous->values[old - previous->nodes].value == duration &&
        host_text_equal(snapshot, node, previous, old, 0);
    view->toast_generation = snapshot->generation;
    if (!host_feedback_toast_driver(snapshot, index)) return 0;
    if (replacement != NULL) {
        if (view->toast_object != NULL) {
            uint32_t s;
            /* Toast slots alias the single owner object. Rebind the aliases
             * before the owner frees it so no slot keeps a freed pointer. */
            for (s = 0u; s < CANOPUS_UI_MAX_NODES; s++)
                if (view->nodes[s].object == view->toast_object)
                    view->nodes[s].object = replacement;
            host_object_delete(view->toast_object);
        }
        view->toast_object = replacement;
        host_label_style(replacement, CANOPUS_UI_TEXT_BODY);
        host_set_size(replacement, HOST_ROW_WIDTH, HOST_SIZE_CONTENT);
        host_label_set_text(replacement, text);
        host_align_to(replacement, view->root, 5u, 0, -36);
        host_set_hidden(replacement, !view->toast_visible);
    }
    if ((node->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) == 0u) {
        host_feedback_clear(view);
        return 0;
    }
    if (same) return 0;
    if (replacement == NULL) {
        host_label_set_text(view->toast_object, text);
        host_align_to(view->toast_object, view->root, 5u, 0, -36);
    }
    host_set_hidden(view->toast_object, 0u);
    view->toast_key = node->key;
    view->toast_text_hash = hash;
    view->toast_visible = 1u;
    view->toast_deadline = host_feedback_now(view) + (uint32_t)(duration > 0 ? duration : 2000);
    return 1;
}

static void host_feedback_finish(struct canopus_ui_host_view *view,
                                  const struct canopus_ui_snapshot_v1 *snapshot)
{
    uint32_t i;
    if (view->toast_generation != snapshot->generation) {
        host_feedback_clear(view);
        view->toast_key = 0u;
    }
    /* One shared owner: every toast slot aliases it, whichever node won. */
    for (i = 0u; i < CANOPUS_UI_MAX_NODES; i++)
        if (view->nodes[i].type == CANOPUS_UI_NODE_TOAST)
            view->nodes[i].object = view->toast_object;
}

void canopus_ui_host_poll(uint32_t now)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_UI_HOST_MAX_VIEWS; i++) {
        struct canopus_ui_host_view *view = s_views[i];
        if (view == NULL || !view->active) continue;
        view->feedback_clock = now;
        if (view->toast_visible && (int32_t)(now - view->toast_deadline) >= 0)
            host_feedback_clear(view);
    }
}
