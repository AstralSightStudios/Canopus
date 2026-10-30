/* Page-owned native navigation titles. */
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

void canopus_ui_host_poll(uint32_t now)
{
    (void)now;
#ifdef HOST_FONT
    /* Native label DELETE callbacks outlive detached/reset view storage. */
    host_font_collect();
#endif
}
