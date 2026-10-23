/* canopus_ui.c — bounded semantic tree builder and transactional commit. */
#include "canopus_ui.h"
#include "canopus_memory.h"

static const struct canopus_ui_style_v1 ui_default_style = CANOPUS_UI_STYLE_DEFAULT;
static const struct canopus_ui_layout_v1 ui_default_layout = CANOPUS_UI_LAYOUT_DEFAULT;

static int ui_backend_ok(const struct canopus_ui_backend_v1 *backend)
{
    return backend != 0 &&
           backend->struct_size == sizeof(struct canopus_ui_backend_v1) &&
           backend->abi_major == CANOPUS_UI_ABI_MAJOR &&
           backend->abi_minor == CANOPUS_UI_ABI_MINOR &&
           backend->apply != 0 &&
           ((backend->capabilities & (CANOPUS_UI_CAP_SCROLL |
               CANOPUS_UI_CAP_TRANSLATE | CANOPUS_UI_CAP_CAPTURE)) == 0u ||
            backend->control != 0);
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
    context->committed.abi_minor = backend->abi_minor;
    return CANOPUS_UI_OK;
}

uint32_t canopus_ui_context_capabilities(
    const struct canopus_ui_context_v1 *context)
{
    if (context == 0 || !ui_backend_ok(context->backend)) {
        return 0u;
    }
    return context->backend->capabilities;
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
    context->staging._snapshot.abi_minor = context->backend->abi_minor;
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

static int ui_tree_supports(const struct canopus_ui_tree_v1 *tree,
                             uint32_t capabilities)
{
    return ui_tree_active(tree) &&
           (tree->_context->backend->capabilities & capabilities) == capabilities;
}

static int ui_style_is_default(const struct canopus_ui_style_v1 *style)
{
    return style->variant == ui_default_style.variant &&
           style->text_style == ui_default_style.text_style &&
           style->foreground == ui_default_style.foreground &&
           style->background == ui_default_style.background &&
           style->accent == ui_default_style.accent &&
           style->border_color == ui_default_style.border_color &&
           style->corner_radius == ui_default_style.corner_radius &&
           style->border_width == ui_default_style.border_width &&
           style->opacity == ui_default_style.opacity &&
           style->reserved == ui_default_style.reserved;
}

static int ui_layout_is_default(const struct canopus_ui_layout_v1 *layout)
{
    return layout->width == ui_default_layout.width &&
           layout->height == ui_default_layout.height &&
           layout->min_width == ui_default_layout.min_width &&
           layout->min_height == ui_default_layout.min_height &&
           layout->max_width == ui_default_layout.max_width &&
           layout->max_height == ui_default_layout.max_height &&
           layout->margin_top == ui_default_layout.margin_top &&
           layout->margin_right == ui_default_layout.margin_right &&
           layout->margin_bottom == ui_default_layout.margin_bottom &&
           layout->margin_left == ui_default_layout.margin_left &&
           layout->padding_top == ui_default_layout.padding_top &&
           layout->padding_right == ui_default_layout.padding_right &&
           layout->padding_bottom == ui_default_layout.padding_bottom &&
           layout->padding_left == ui_default_layout.padding_left &&
           layout->gap == ui_default_layout.gap &&
           layout->axis == ui_default_layout.axis &&
           layout->align == ui_default_layout.align &&
           layout->justify == ui_default_layout.justify &&
           layout->grow == ui_default_layout.grow &&
           layout->shrink == ui_default_layout.shrink &&
           layout->reserved[0] == 0u && layout->reserved[1] == 0u &&
           layout->reserved[2] == 0u;
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
    canopus_memcpy(&snapshot->styles[index], &ui_default_style,
                   sizeof(ui_default_style));
    canopus_memcpy(&snapshot->layouts[index], &ui_default_layout,
                   sizeof(ui_default_layout));
    canopus_memset(&snapshot->values[index], 0, sizeof(snapshot->values[index]));
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
                          props->title, props->title_len, 0, 0, 0,
                          CANOPUS_UI_NODE_FLAG_VISIBLE, 1);
}

int32_t canopus_ui_section(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_section_props_v1 *props)
{
    if (props == 0 || props->struct_size != sizeof(*props)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_SECTION,
                          props->title, props->title_len, 0, 0, 0,
                          CANOPUS_UI_NODE_FLAG_VISIBLE, 1);
}

int32_t canopus_ui_text(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_text_props_v1 *props)
{
    int32_t rc;
    uint16_t index;
    if (props == 0 || props->struct_size != sizeof(*props) ||
        props->style > CANOPUS_UI_TEXT_WARNING) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!ui_tree_active(tree)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (props->style != CANOPUS_UI_TEXT_BODY &&
        !ui_tree_supports(tree, CANOPUS_UI_CAP_STYLE)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    rc = ui_append_node(tree, key, CANOPUS_UI_NODE_TEXT,
                        props->text, props->text_len, 0, 0, 0,
                        CANOPUS_UI_NODE_FLAG_VISIBLE, 0);
    if (rc == CANOPUS_UI_OK) {
        index = (uint16_t)(tree->_snapshot.node_count - 1u);
        tree->_snapshot.styles[index].text_style = (uint16_t)props->style;
    }
    return rc;
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
                          props->value, props->value_len, 0,
                          CANOPUS_UI_NODE_FLAG_VISIBLE, 0);
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
    flags = CANOPUS_UI_NODE_FLAG_VISIBLE |
            (props->enabled ? CANOPUS_UI_NODE_FLAG_ENABLED : 0u);
    return ui_append_node(tree, key, CANOPUS_UI_NODE_BUTTON,
                          props->label, props->label_len, 0, 0,
                          props->event_id, flags, 0);
}

int32_t canopus_ui_action_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_action_row_props_v1 *props)
{
    uint32_t flags;
    if (props == 0 || props->struct_size != sizeof(*props) ||
        props->event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    flags = CANOPUS_UI_NODE_FLAG_VISIBLE |
            (props->enabled ? CANOPUS_UI_NODE_FLAG_ENABLED : 0u);
    return ui_append_node(tree, key, CANOPUS_UI_NODE_ACTION_ROW,
                          props->label, props->label_len,
                          props->detail, props->detail_len,
                          props->event_id, flags, 0);
}

int32_t canopus_ui_switch_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_switch_row_props_v1 *props)
{
    uint32_t flags = CANOPUS_UI_NODE_FLAG_VISIBLE;
    if (props == 0 || props->struct_size != sizeof(*props) ||
        props->event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (props->enabled) {
        flags |= CANOPUS_UI_NODE_FLAG_ENABLED;
    }
    if (props->checked) {
        flags |= CANOPUS_UI_NODE_FLAG_CHECKED;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_SWITCH_ROW,
                          props->label, props->label_len,
                          props->detail, props->detail_len,
                          props->event_id, flags, 0);
}

int32_t canopus_ui_navigation_header(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_navigation_header_props_v1 *props)
{
    uint32_t flags = CANOPUS_UI_NODE_FLAG_VISIBLE;
    /* A visible back needs a handler. A hidden one may keep a reserved id: it is
     * unreachable while hidden and becomes live when show_back is set on a later
     * render, so the pair is deliberately not forced to agree. */
    if (props == 0 || props->struct_size != sizeof(*props) ||
        (props->show_back && props->back_event_id == 0u)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!ui_tree_active(tree)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!ui_tree_supports(tree, CANOPUS_UI_CAP_NAVIGATION_HEADER)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    if (props->show_back) {
        flags |= CANOPUS_UI_NODE_FLAG_ENABLED |
                 CANOPUS_UI_NODE_FLAG_HEADER_BACK;
    }
    if (props->centered) {
        flags |= CANOPUS_UI_NODE_FLAG_HEADER_CENTERED;
    }
    if (props->elevated) {
        flags |= CANOPUS_UI_NODE_FLAG_HEADER_ELEVATED;
    }
    return ui_append_node(tree, key, CANOPUS_UI_NODE_NAVIGATION_HEADER,
                          props->title, props->title_len,
                          props->subtitle, props->subtitle_len,
                          props->back_event_id, flags, 1);
}

static int ui_component_type_valid(uint16_t type)
{
    return type >= CANOPUS_UI_NODE_LIST &&
           type <= CANOPUS_UI_NODE_NAVIGATION_HEADER &&
           type != CANOPUS_UI_NODE_RETIRED_SLIDER;
}

static int ui_component_interactive(uint16_t type)
{
    return type == CANOPUS_UI_NODE_CHECKBOX ||
           type == CANOPUS_UI_NODE_RADIO_ROW;
}

static int ui_component_container(uint16_t type)
{
    return type == CANOPUS_UI_NODE_LIST || type == CANOPUS_UI_NODE_SCROLL ||
           type == CANOPUS_UI_NODE_DIALOG ||
           type == CANOPUS_UI_NODE_NAVIGATION_HEADER;
}

static int32_t ui_find_node_index(const struct canopus_ui_tree_v1 *tree,
                                  canopus_ui_node_id key, uint16_t *out_index)
{
    uint16_t i;
    if (!ui_tree_active(tree) || key == 0u || out_index == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    for (i = 0; i < tree->_snapshot.node_count; i++) {
        if (tree->_snapshot.nodes[i].key == key) {
            *out_index = i;
            return CANOPUS_UI_OK;
        }
    }
    return CANOPUS_UI_ERR_ARGUMENT;
}

int32_t canopus_ui_component(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key, uint16_t type,
    const struct canopus_ui_component_props_v1 *props)
{
    uint16_t index;
    int32_t rc;
    if (props == 0 || props->struct_size != sizeof(*props) ||
        !ui_component_type_valid(type)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!ui_tree_active(tree)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!ui_tree_supports(tree, CANOPUS_UI_CAP_EXTENDED_COMPONENTS) ||
        (type == CANOPUS_UI_NODE_NAVIGATION_HEADER &&
         !ui_tree_supports(tree, CANOPUS_UI_CAP_NAVIGATION_HEADER)) ||
        (type == CANOPUS_UI_NODE_IMAGE &&
         !ui_tree_supports(tree, CANOPUS_UI_CAP_IMAGE)) ||
        (type == CANOPUS_UI_NODE_PROGRESS &&
         !ui_tree_supports(tree, CANOPUS_UI_CAP_PROGRESS)) ||
        (type == CANOPUS_UI_NODE_TOAST &&
         !ui_tree_supports(tree, CANOPUS_UI_CAP_TOAST)) ||
        ((props->value != 0 || props->minimum != 0 || props->maximum != 0 ||
          props->step != 0 || props->resource_id != 0u) &&
         !ui_tree_supports(tree, CANOPUS_UI_CAP_VALUES))) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    if (ui_component_interactive(type) && props->event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (type == CANOPUS_UI_NODE_IMAGE &&
        (props->resource_id == 0u || props->primary == 0 ||
         props->primary_len == 0u)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (type == CANOPUS_UI_NODE_TOAST &&
        (props->value < 0 || props->value > 60000)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (type == CANOPUS_UI_NODE_PROGRESS &&
        (props->minimum >= props->maximum || props->value < props->minimum ||
         props->value > props->maximum || props->step < 0)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    rc = ui_append_node(tree, key, type, props->primary, props->primary_len,
                        props->secondary, props->secondary_len,
                        props->event_id, props->flags,
                        ui_component_container(type));
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    index = (uint16_t)(tree->_snapshot.node_count - 1u);
    tree->_snapshot.values[index].value = props->value;
    tree->_snapshot.values[index].minimum = props->minimum;
    tree->_snapshot.values[index].maximum = props->maximum;
    tree->_snapshot.values[index].step = props->step;
    tree->_snapshot.values[index].resource_id = props->resource_id;
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_node_set_style(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_style_v1 *style)
{
    uint16_t index;
    int32_t rc;
    if (style == 0 || style->variant > CANOPUS_UI_VARIANT_COMPACT ||
        style->text_style > CANOPUS_UI_TEXT_WARNING ||
        style->foreground > CANOPUS_UI_COLOR_TRANSPARENT ||
        style->background > CANOPUS_UI_COLOR_TRANSPARENT ||
        style->accent > CANOPUS_UI_COLOR_TRANSPARENT ||
        style->border_color > CANOPUS_UI_COLOR_TRANSPARENT ||
        style->corner_radius < -1 || style->border_width < -1 ||
        style->opacity > 1000u || style->reserved != 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    rc = ui_find_node_index(tree, key, &index);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    if (!ui_style_is_default(style) &&
        !ui_tree_supports(tree, CANOPUS_UI_CAP_STYLE)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    canopus_memcpy(&tree->_snapshot.styles[index], style, sizeof(*style));
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_node_set_layout(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_layout_v1 *layout)
{
    uint16_t index;
    int32_t rc;
    if (layout == 0 || layout->width < -1 || layout->height < -1 ||
        layout->min_width < -1 || layout->min_height < -1 ||
        layout->max_width < -1 || layout->max_height < -1 ||
        layout->gap < -1 || layout->axis > CANOPUS_UI_AXIS_HORIZONTAL ||
        layout->align > CANOPUS_UI_ALIGN_STRETCH ||
        layout->justify > CANOPUS_UI_JUSTIFY_SPACE_EVENLY ||
        layout->reserved[0] != 0u || layout->reserved[1] != 0u ||
        layout->reserved[2] != 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    rc = ui_find_node_index(tree, key, &index);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    if (!ui_layout_is_default(layout) &&
        !ui_tree_supports(tree, CANOPUS_UI_CAP_LAYOUT)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    canopus_memcpy(&tree->_snapshot.layouts[index], layout, sizeof(*layout));
    return CANOPUS_UI_OK;
}

static int ui_scroll_type(uint16_t type)
{
    return type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
           type == CANOPUS_UI_NODE_SCROLL || type == CANOPUS_UI_NODE_LIST;
}

int32_t canopus_ui_node_set_visual(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_visual_v1 *visual)
{
    const uint32_t known_flags = CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB |
        CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB | CANOPUS_UI_VISUAL_FLAG_BORDER_RGB |
        CANOPUS_UI_VISUAL_FLAG_ABSOLUTE | CANOPUS_UI_VISUAL_FLAG_CLIP |
        CANOPUS_UI_VISUAL_FLAG_OPACITY | CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT;
    uint32_t required = 0u;
    uint16_t index;
    int32_t rc;
    if (visual == 0 || (visual->flags & ~known_flags) != 0u ||
        visual->foreground_rgb > 0xFFFFFFu ||
        visual->background_rgb > 0xFFFFFFu || visual->border_rgb > 0xFFFFFFu ||
        visual->image_fit > CANOPUS_UI_IMAGE_FIT_STRETCH ||
        ((visual->flags & CANOPUS_UI_VISUAL_FLAG_ABSOLUTE) == 0u &&
         (visual->x != 0 || visual->y != 0)) ||
        ((visual->flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT) == 0u &&
         (visual->content_height != 0u || visual->scroll_y != 0)) ||
        visual->content_height > 0x7FFFFFFFu || visual->scroll_y < 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    rc = ui_find_node_index(tree, key, &index);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    if ((visual->flags & CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT) != 0u) {
        if (!ui_scroll_type(tree->_snapshot.nodes[index].type)) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        required |= CANOPUS_UI_CAP_VIRTUAL_CONTENT | CANOPUS_UI_CAP_SCROLL;
    }
    if (visual->image_fit != CANOPUS_UI_IMAGE_FIT_NATIVE) {
        if (tree->_snapshot.nodes[index].type != CANOPUS_UI_NODE_IMAGE) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        required |= visual->image_fit == CANOPUS_UI_IMAGE_FIT_CONTAIN ?
            CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN :
            visual->image_fit == CANOPUS_UI_IMAGE_FIT_COVER ?
            CANOPUS_UI_CAP_IMAGE_FIT_COVER : CANOPUS_UI_CAP_IMAGE_FIT_STRETCH;
    }
    if (visual->translate_x != 0 || visual->translate_y != 0) {
        required |= CANOPUS_UI_CAP_TRANSLATE;
    }
    if (visual->flags != 0u || visual->foreground_rgb != 0u ||
        visual->background_rgb != 0u || visual->border_rgb != 0u ||
        visual->font_px != 0u || required != 0u) {
        required |= CANOPUS_UI_CAP_VISUAL;
    }
    if (!ui_tree_supports(tree, required)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    canopus_memcpy(&tree->_snapshot.visuals[index], visual, sizeof(*visual));
    return CANOPUS_UI_OK;
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
        return rc == CANOPUS_UI_ERR_UNSUPPORTED ? rc : CANOPUS_UI_ERR_BACKEND;
    }
    canopus_memcpy(&context->committed, &tree->_snapshot,
                   sizeof(context->committed));
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

void canopus_ui_context_abort_pending(struct canopus_ui_context_v1 *context)
{
    if (context != 0) {
        context->staging._active = 0u;
        context->building = 0u;
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

static const struct canopus_ui_node_v1 *ui_committed_node(
    const struct canopus_ui_context_v1 *context, canopus_ui_node_id key)
{
    uint16_t i;
    for (i = 0; i < context->committed.node_count; i++) {
        if (context->committed.nodes[i].key == key) {
            return &context->committed.nodes[i];
        }
    }
    return 0;
}

static int ui_node_interactive(const struct canopus_ui_node_v1 *node)
{
    return node->type == CANOPUS_UI_NODE_BUTTON ||
           node->type == CANOPUS_UI_NODE_ACTION_ROW ||
           node->type == CANOPUS_UI_NODE_SWITCH_ROW ||
           node->type == CANOPUS_UI_NODE_CHECKBOX ||
           node->type == CANOPUS_UI_NODE_RADIO_ROW ||
           ((node->type == CANOPUS_UI_NODE_ICON ||
             node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER) &&
            node->event_id != 0u);
}

int32_t canopus_ui_dispatch_event(
    struct canopus_ui_context_v1 *context,
    const struct canopus_ui_event_v1 *event)
{
    const struct canopus_ui_node_v1 *node;
    uint32_t capabilities;
    if (context == 0 || event == 0 || event->struct_size != sizeof(*event) ||
        event->key == 0u || event->type < CANOPUS_UI_EVENT_ACTIVATE ||
        event->type > CANOPUS_UI_EVENT_SCROLL) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!context->has_committed ||
        event->generation != context->committed.generation) {
        if (context->dropped_events != 0xFFFFFFFFu) {
            context->dropped_events++;
        }
        return CANOPUS_UI_ERR_STALE;
    }
    node = ui_committed_node(context, event->key);
    if (node == 0 || node->event_id != event->event_id) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    capabilities = canopus_ui_context_capabilities(context);
    if (event->type == CANOPUS_UI_EVENT_SCROLL) {
        if (!ui_scroll_type(node->type) || event->scroll_y < 0 ||
            event->viewport_height < 0 || event->content_height < 0) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        if ((capabilities & CANOPUS_UI_CAP_SCROLL) == 0u) {
            return CANOPUS_UI_ERR_UNSUPPORTED;
        }
    } else if (event->type >= CANOPUS_UI_EVENT_PRESS) {
        if ((capabilities & CANOPUS_UI_CAP_POINTER_EVENTS) == 0u) {
            return CANOPUS_UI_ERR_UNSUPPORTED;
        }
    } else if (!ui_node_interactive(node) || event->event_id == 0u ||
               (event->type == CANOPUS_UI_EVENT_VALUE_CHANGED &&
                node->type != CANOPUS_UI_NODE_SWITCH_ROW &&
                node->type != CANOPUS_UI_NODE_CHECKBOX &&
                node->type != CANOPUS_UI_NODE_RADIO_ROW)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if ((node->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) == 0u ||
        (ui_node_interactive(node) &&
         (node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u)) {
        return CANOPUS_UI_ERR_DISABLED;
    }
    if (context->event_handler == 0) {
        return CANOPUS_UI_OK;
    }
    return context->event_handler(context->event_cookie, event);
}

int32_t canopus_ui_control(
    struct canopus_ui_context_v1 *context, uint32_t generation,
    canopus_ui_node_id key, uint32_t op, int32_t a, int32_t b, void *out)
{
    const struct canopus_ui_node_v1 *node;
    uint32_t capability;
    int32_t rc;
    if (context == 0 || key == 0u || op < CANOPUS_UI_CONTROL_SCROLL_QUERY ||
        op > CANOPUS_UI_CONTROL_RELEASE_CAPTURE) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (!context->has_committed || generation != context->committed.generation) {
        return CANOPUS_UI_ERR_STALE;
    }
    node = ui_committed_node(context, key);
    if (node == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (op == CANOPUS_UI_CONTROL_SCROLL_QUERY) {
        if (!ui_scroll_type(node->type) || a != 0 || b != 0 || out == 0) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        capability = CANOPUS_UI_CAP_SCROLL;
    } else {
        if (out != 0) {
            return CANOPUS_UI_ERR_ARGUMENT;
        }
        if (op == CANOPUS_UI_CONTROL_SCROLL_TO) {
            if (!ui_scroll_type(node->type) || a < 0 || b != 0) {
                return CANOPUS_UI_ERR_ARGUMENT;
            }
            capability = CANOPUS_UI_CAP_SCROLL;
        } else if (op == CANOPUS_UI_CONTROL_TRANSLATE) {
            capability = CANOPUS_UI_CAP_TRANSLATE;
        } else {
            if (a != 0 || b != 0) {
                return CANOPUS_UI_ERR_ARGUMENT;
            }
            if (op == CANOPUS_UI_CONTROL_CAPTURE &&
                ((node->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) == 0u ||
                 (ui_node_interactive(node) &&
                  (node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u))) {
                return CANOPUS_UI_ERR_DISABLED;
            }
            capability = CANOPUS_UI_CAP_CAPTURE;
        }
    }
    if ((canopus_ui_context_capabilities(context) & capability) == 0u ||
        context->backend->control == 0) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    rc = context->backend->control(context->backend_cookie, generation, key,
                                    op, a, b, out);
    if (rc == CANOPUS_UI_OK || rc == CANOPUS_UI_ERR_UNSUPPORTED ||
        rc == CANOPUS_UI_ERR_ARGUMENT || rc == CANOPUS_UI_ERR_STATE ||
        rc == CANOPUS_UI_ERR_STALE || rc == CANOPUS_UI_ERR_DISABLED) {
        return rc;
    }
    return CANOPUS_UI_ERR_BACKEND;
}

static int ui_router_valid(const struct canopus_ui_router_v1 *router)
{
    return router != 0 && router->depth > 0u &&
           router->depth <= CANOPUS_UI_ROUTER_MAX_DEPTH &&
           router->generation != 0u;
}

static void ui_router_advance(struct canopus_ui_router_v1 *router)
{
    router->generation++;
    if (router->generation == 0u) {
        router->generation = 1u;
    }
}

int32_t canopus_ui_router_init(
    struct canopus_ui_router_v1 *router, uint32_t root_route)
{
    if (router == 0 || root_route == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memset(router, 0, sizeof(*router));
    router->routes[0] = root_route;
    router->depth = 1u;
    router->generation = 1u;
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_router_push(
    struct canopus_ui_router_v1 *router, uint32_t route)
{
    if (!ui_router_valid(router) || route == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (router->depth >= CANOPUS_UI_ROUTER_MAX_DEPTH) {
        return CANOPUS_UI_ERR_CAPACITY;
    }
    router->routes[router->depth++] = route;
    ui_router_advance(router);
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_router_replace(
    struct canopus_ui_router_v1 *router, uint32_t route)
{
    if (!ui_router_valid(router) || route == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    router->routes[router->depth - 1u] = route;
    ui_router_advance(router);
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_router_pop(
    struct canopus_ui_router_v1 *router, uint32_t *out_route)
{
    if (!ui_router_valid(router)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (router->depth == 1u) {
        return CANOPUS_UI_ERR_STATE;
    }
    router->depth--;
    router->routes[router->depth] = 0u;
    ui_router_advance(router);
    if (out_route != 0) {
        *out_route = router->routes[router->depth - 1u];
    }
    return CANOPUS_UI_OK;
}

int32_t canopus_ui_router_back(
    struct canopus_ui_router_v1 *router, uint32_t generation,
    uint32_t *out_route)
{
    if (!ui_router_valid(router)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (generation != router->generation) {
        return CANOPUS_UI_ERR_STALE;
    }
    return canopus_ui_router_pop(router, out_route);
}

int32_t canopus_ui_router_pop_to(
    struct canopus_ui_router_v1 *router, uint32_t route)
{
    uint16_t i;
    if (!ui_router_valid(router) || route == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    i = router->depth;
    while (i > 0u) {
        i--;
        if (router->routes[i] == route) {
            if (i + 1u != router->depth) {
                uint16_t clear = (uint16_t)(i + 1u);
                while (clear < router->depth) {
                    router->routes[clear++] = 0u;
                }
                router->depth = (uint16_t)(i + 1u);
                ui_router_advance(router);
            }
            return CANOPUS_UI_OK;
        }
    }
    return CANOPUS_UI_ERR_ARGUMENT;
}

int32_t canopus_ui_router_clear_to_root(
    struct canopus_ui_router_v1 *router)
{
    if (!ui_router_valid(router)) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (router->depth > 1u) {
        uint16_t i = 1u;
        while (i < router->depth) {
            router->routes[i++] = 0u;
        }
        router->depth = 1u;
        ui_router_advance(router);
    }
    return CANOPUS_UI_OK;
}

uint32_t canopus_ui_router_current(
    const struct canopus_ui_router_v1 *router)
{
    return ui_router_valid(router) ? router->routes[router->depth - 1u] : 0u;
}

uint16_t canopus_ui_router_depth(
    const struct canopus_ui_router_v1 *router)
{
    return ui_router_valid(router) ? router->depth : 0u;
}
