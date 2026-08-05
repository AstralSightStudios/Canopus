/* canopus_ui.c — bounded semantic tree builder and transactional commit. */
#include "canopus_ui.h"
#include "canopus_memory.h"

static int ui_backend_ok(const struct canopus_ui_backend_v1 *backend)
{
    return backend != 0 &&
           backend->struct_size == sizeof(struct canopus_ui_backend_v1) &&
           backend->abi_major == CANOPUS_UI_ABI_MAJOR &&
           backend->abi_minor <= CANOPUS_UI_ABI_MINOR &&
           backend->apply != 0;
}

int32_t canopus_ui_context_init(
    struct canopus_ui_context_v1 *context,
    const struct canopus_ui_backend_v1 *backend,
    void *backend_cookie,
    canopus_ui_event_handler_v1 event_handler,
    void *event_cookie)
{
    if (context == 0 || !ui_backend_ok(backend)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memset(context, 0, sizeof(*context));
    context->backend = backend;
    context->backend_cookie = backend_cookie;
    context->event_handler = event_handler;
    context->event_cookie = event_cookie;
    context->committed.abi_major = CANOPUS_UI_ABI_MAJOR;
    context->committed.abi_minor = CANOPUS_UI_ABI_MINOR;
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_tree_begin(
    struct canopus_ui_context_v1 *context,
    struct canopus_ui_tree_v1 **out_tree)
{
    uint32_t generation;
    if (context == 0 || out_tree == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    *out_tree = 0;
    if (context->building || !ui_backend_ok(context->backend)) {
        return CANOPUS_UI_ERR_STATE;
    }
    generation = context->committed.generation + 1u;
    if (generation == 0u) {
        generation = 1u;
    }
    canopus_memset(&context->staging, 0, sizeof(context->staging));
    context->staging._context = context;
    context->staging._snapshot.abi_major = CANOPUS_UI_ABI_MAJOR;
    context->staging._snapshot.abi_minor = CANOPUS_UI_ABI_MINOR;
    context->staging._snapshot.generation = generation;
    context->staging._active = 1u;
    context->building = 1u;
    *out_tree = &context->staging;
    return CANOPUS_UI_OK;
}

static int ui_tree_active(const struct canopus_ui_tree_v1 *tree)
{
    return tree != 0 && tree->_context != 0 && tree->_active &&
           tree->_context->building && &tree->_context->staging == tree;
}

static int ui_key_exists(const struct canopus_ui_snapshot_v1 *snapshot,
                         canopus_ui_node_id key)
{
    uint16_t i;
    for (i = 0; i < snapshot->node_count; i++) {
        if (snapshot->nodes[i].key == key) {
            return 1;
        }
    }
    return 0;
}

static int32_t ui_copy_string(struct canopus_ui_snapshot_v1 *snapshot,
                              const char *value, uint32_t len,
                              uint16_t *out_off, uint16_t *out_len)
{
    uint32_t need;
    if (out_off == 0 || out_len == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    *out_off = 0;
    *out_len = 0;
    if (len == 0u) {
        return CANOPUS_UI_OK;
    }
    if (value == 0 || len > 0xFFFFu) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    need = len + 1u;
    if (need < len || snapshot->string_used > CANOPUS_UI_STRING_CAPACITY ||
        need > CANOPUS_UI_STRING_CAPACITY - snapshot->string_used) {
        return CANOPUS_UI_ERR_CAPACITY;
    }
    *out_off = snapshot->string_used;
    *out_len = (uint16_t)len;
    canopus_memcpy(snapshot->strings + snapshot->string_used, value, len);
    snapshot->strings[snapshot->string_used + len] = '\0';
    snapshot->string_used = (uint16_t)(snapshot->string_used + need);
    return CANOPUS_UI_OK;
}

static int32_t ui_append_node(struct canopus_ui_tree_v1 *tree,
                              canopus_ui_node_id key, uint16_t type,
                              const char *primary, uint32_t primary_len,
                              const char *secondary, uint32_t secondary_len,
                              uint32_t event_id, uint32_t flags,
                              int push_container)
{
    struct canopus_ui_snapshot_v1 *snapshot;
    struct canopus_ui_node_v1 node;
    uint16_t index;
    uint16_t parent = CANOPUS_UI_NO_NODE;
    uint16_t string_mark;
    int32_t rc;

    if (!ui_tree_active(tree) || key == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    snapshot = &tree->_snapshot;
    if (snapshot->node_count >= CANOPUS_UI_MAX_NODES) {
        return CANOPUS_UI_ERR_CAPACITY;
    }
    if (ui_key_exists(snapshot, key)) {
        return CANOPUS_UI_ERR_DUP_KEY;
    }
    if (type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
        if (snapshot->node_count != 0u || tree->_depth != 0u) {
            return CANOPUS_UI_ERR_STATE;
        }
    } else if (tree->_depth == 0u) {
        return CANOPUS_UI_ERR_STATE;
    }
    if (tree->_depth > 0u) {
        parent = tree->_parent_stack[tree->_depth - 1u];
    }
    if (push_container && tree->_depth >= CANOPUS_UI_MAX_DEPTH) {
        return CANOPUS_UI_ERR_CAPACITY;
    }

    canopus_memset(&node, 0, sizeof(node));
    node.key = key;
    node.type = type;
    node.parent = parent;
    node.first_child = CANOPUS_UI_NO_NODE;
    node.next_sibling = CANOPUS_UI_NO_NODE;
    node.event_id = event_id;
    node.flags = flags;
    string_mark = snapshot->string_used;
    rc = ui_copy_string(snapshot, primary, primary_len,
                        &node.primary_off, &node.primary_len);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    rc = ui_copy_string(snapshot, secondary, secondary_len,
                        &node.secondary_off, &node.secondary_len);
    if (rc != CANOPUS_UI_OK) {
        /* Roll back the first string; no partially appended node is visible. */
        snapshot->string_used = string_mark;
        return rc;
    }

    index = snapshot->node_count;
    snapshot->nodes[index] = node;
    snapshot->node_count++;
    if (parent != CANOPUS_UI_NO_NODE) {
        struct canopus_ui_node_v1 *parent_node = &snapshot->nodes[parent];
        if (parent_node->first_child == CANOPUS_UI_NO_NODE) {
            parent_node->first_child = index;
        } else {
            uint16_t sibling = parent_node->first_child;
            while (snapshot->nodes[sibling].next_sibling != CANOPUS_UI_NO_NODE) {
                sibling = snapshot->nodes[sibling].next_sibling;
            }
            snapshot->nodes[sibling].next_sibling = index;
        }
    }
    if (push_container) {
        tree->_parent_stack[tree->_depth++] = index;
    }
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_navigation_page(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_navigation_page_props_v1 *props)
{
    if (props == 0 || props->struct_size != sizeof(*props)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_NAVIGATION_PAGE,
                          props->title, props->title_len, 0, 0, 0, 0, 1);
}

int32_t canopus_ui_section(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_section_props_v1 *props)
{
    if (props == 0 || props->struct_size != sizeof(*props)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_SECTION,
                          props->title, props->title_len, 0, 0, 0, 0, 1);
}

int32_t canopus_ui_text(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_text_props_v1 *props)
{
    if (props == 0 || props->struct_size != sizeof(*props)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_TEXT,
                          props->text, props->text_len, 0, 0, 0,
                          props->style, 0);
}

int32_t canopus_ui_status_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_status_row_props_v1 *props)
{
    if (props == 0 || props->struct_size != sizeof(*props)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_STATUS_ROW,
                          props->label, props->label_len,
                          props->value, props->value_len, 0, 0, 0);
}

int32_t canopus_ui_button(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_button_props_v1 *props)
{
    uint32_t flags;
    if (props == 0 || props->struct_size != sizeof(*props) ||
        props->event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    flags = props->enabled ? CANOPUS_UI_NODE_FLAG_ENABLED : 0u;
    return ui_append_node(tree, key, CANOPUS_UI_NODE_BUTTON,
                          props->label, props->label_len, 0, 0,
                          props->event_id, flags, 0);
}

int32_t canopus_ui_end(struct canopus_ui_tree_v1 *tree)
{
    if (!ui_tree_active(tree)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (tree->_depth == 0u) {
        return CANOPUS_UI_ERR_STATE;
    }
    tree->_depth--;
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_tree_commit(struct canopus_ui_tree_v1 *tree)
{
    struct canopus_ui_context_v1 *context;
    int32_t rc;
    if (!ui_tree_active(tree)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    context = tree->_context;
    if (tree->_depth != 0u || tree->_snapshot.node_count == 0u ||
        tree->_snapshot.nodes[0].type != CANOPUS_UI_NODE_NAVIGATION_PAGE) {
        return CANOPUS_UI_ERR_STATE;
    }
    rc = context->backend->apply(context->backend_cookie, &tree->_snapshot);
    tree->_active = 0u;
    context->building = 0u;
    if (rc != 0) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    context->committed = tree->_snapshot;
    context->has_committed = 1u;
    return CANOPUS_UI_OK;
}

void canopus_ui_tree_abort(struct canopus_ui_tree_v1 *tree)
{
    if (ui_tree_active(tree)) {
        tree->_active = 0u;
        tree->_context->building = 0u;
    }
}

const struct canopus_ui_snapshot_v1 *canopus_ui_current(
    const struct canopus_ui_context_v1 *context)
{
    if (context == 0 || !context->has_committed) {
        return 0;
    }
    return &context->committed;
}

int32_t canopus_ui_dispatch_event(
    struct canopus_ui_context_v1 *context, uint32_t generation,
    canopus_ui_node_id key, uint32_t event_id)
{
    uint16_t i;
    const struct canopus_ui_node_v1 *node = 0;
    if (context == 0 || key == 0u || event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!context->has_committed || generation != context->committed.generation) {
        if (context->dropped_events != 0xFFFFFFFFu) {
            context->dropped_events++;
        }
        return CANOPUS_UI_ERR_STALE;
    }
    for (i = 0; i < context->committed.node_count; i++) {
        if (context->committed.nodes[i].key == key) {
            node = &context->committed.nodes[i];
            break;
        }
    }
    if (node == 0 || node->type != CANOPUS_UI_NODE_BUTTON ||
        node->event_id != event_id) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if ((node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u) {
        return CANOPUS_UI_ERR_DISABLED;
    }
    if (context->event_handler == 0) {
        return CANOPUS_UI_OK;
    }
    return context->event_handler(context->event_cookie, generation, key,
                                  event_id);
}
