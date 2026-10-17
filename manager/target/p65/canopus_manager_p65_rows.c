/* P65 row adapter core: no firmware pointers or target calls. */
#include "canopus_manager_p65_rows.h"

static int p65_is_container(uint16_t type)
{
    return type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
           type == CANOPUS_UI_NODE_LIST ||
           type == CANOPUS_UI_NODE_SCROLL;
}

static int p65_is_row_type(uint16_t type)
{
    switch (type) {
    case CANOPUS_UI_NODE_SECTION:
    case CANOPUS_UI_NODE_TEXT:
    case CANOPUS_UI_NODE_STATUS_ROW:
    case CANOPUS_UI_NODE_BUTTON:
    case CANOPUS_UI_NODE_ACTION_ROW:
    case CANOPUS_UI_NODE_SWITCH_ROW:
    case CANOPUS_UI_NODE_DIALOG:
    case CANOPUS_UI_NODE_TOAST:
    case CANOPUS_UI_NODE_IMAGE:
    case CANOPUS_UI_NODE_ICON:
    case CANOPUS_UI_NODE_RICH_TEXT:
    case CANOPUS_UI_NODE_CHECKBOX:
    case CANOPUS_UI_NODE_RADIO_ROW:
    case CANOPUS_UI_NODE_SLIDER:
    case CANOPUS_UI_NODE_PROGRESS:
    case CANOPUS_UI_NODE_DIVIDER:
    case CANOPUS_UI_NODE_SPACER:
    case CANOPUS_UI_NODE_NAVIGATION_HEADER:
        return 1;
    default:
        return 0;
    }
}

static int p65_is_interactive(uint16_t type)
{
    switch (type) {
    case CANOPUS_UI_NODE_BUTTON:
    case CANOPUS_UI_NODE_ACTION_ROW:
    case CANOPUS_UI_NODE_SWITCH_ROW:
    case CANOPUS_UI_NODE_ICON:
    case CANOPUS_UI_NODE_CHECKBOX:
    case CANOPUS_UI_NODE_RADIO_ROW:
    case CANOPUS_UI_NODE_SLIDER:
    case CANOPUS_UI_NODE_NAVIGATION_HEADER:
        return 1;
    default:
        return 0;
    }
}

/* Returns 1 for visible, 0 for hidden, and -1 for an invalid parent chain. */
static int p65_node_visible(const struct canopus_ui_snapshot_v1 *snapshot,
                            uint16_t node_index)
{
    uint16_t current = node_index;
    uint16_t visited = 0u;

    while (current != CANOPUS_UI_NO_NODE) {
        if (current >= snapshot->node_count ||
            visited >= snapshot->node_count) {
            return -1;
        }
        if ((snapshot->nodes[current].flags &
             CANOPUS_UI_NODE_FLAG_VISIBLE) == 0u) {
            return 0;
        }
        current = snapshot->nodes[current].parent;
        visited++;
    }
    return 1;
}

static int p65_string_valid(const char *strings, uint16_t string_used,
                            uint16_t offset, uint16_t length)
{
    uint32_t end = (uint32_t)offset + (uint32_t)length;

    if (length == 0u) {
        return 1;
    }
    return end < string_used && strings[end] == '\0';
}

static int p65_is_included_node(
    const struct canopus_ui_snapshot_v1 *snapshot, uint16_t node_index,
    int *out_included)
{
    const struct canopus_ui_node_v1 *node = &snapshot->nodes[node_index];
    int visible = p65_node_visible(snapshot, node_index);

    if (visible < 0) {
        return CANOPUS_UI_ERR_STATE;
    }
    if (visible == 0 || p65_is_container(node->type)) {
        *out_included = 0;
        return CANOPUS_UI_OK;
    }
    if (!p65_is_row_type(node->type)) {
        return CANOPUS_UI_ERR_UNSUPPORTED;
    }
    if (!p65_string_valid(snapshot->strings, snapshot->string_used,
                          node->primary_off, node->primary_len) ||
        !p65_string_valid(snapshot->strings, snapshot->string_used,
                          node->secondary_off, node->secondary_len)) {
        return CANOPUS_UI_ERR_STATE;
    }
    *out_included = 1;
    return CANOPUS_UI_OK;
}

int32_t canopus_p65_ui_rows_build(
    struct canopus_p65_ui_rows_v1 *rows,
    const struct canopus_ui_snapshot_v1 *snapshot)
{
    uint16_t source_indices[CANOPUS_P65_UI_MAX_ROWS];
    uint16_t i;
    uint16_t count = 0u;

    if (rows == 0 || snapshot == 0 ||
        snapshot->abi_major != CANOPUS_UI_ABI_MAJOR ||
        snapshot->abi_minor > CANOPUS_UI_ABI_MINOR ||
        snapshot->node_count > CANOPUS_UI_MAX_NODES ||
        snapshot->string_used > CANOPUS_UI_STRING_CAPACITY) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }

    /* Validate and index first so an invalid staging snapshot never partially
     * replaces row storage used by callbacks after apply() returns. */
    for (i = 0u; i < snapshot->node_count; i++) {
        int included;
        int32_t rc = p65_is_included_node(snapshot, i, &included);
        if (rc != CANOPUS_UI_OK) {
            return rc;
        }
        if (included) {
            if (count >= CANOPUS_P65_UI_MAX_ROWS) {
                return CANOPUS_UI_ERR_CAPACITY;
            }
            source_indices[count++] = i;
        }
    }

    /* No failure path remains after this point: copy only selected rows and
     * the shared string arena into adapter-owned storage. */
    for (i = 0u; i < count; i++) {
        uint16_t source_index = source_indices[i];
        struct canopus_p65_ui_row_data_v1 *row = &rows->rows[i];
        row->node = snapshot->nodes[source_index];
        row->style = snapshot->styles[source_index];
        row->layout = snapshot->layouts[source_index];
        row->value = snapshot->values[source_index];
        row->source_index = source_index;
    }
    for (i = 0u; i < snapshot->string_used; i++) {
        rows->strings[i] = snapshot->strings[i];
    }
    rows->generation = snapshot->generation;
    rows->string_used = snapshot->string_used;
    rows->count = count;
    return CANOPUS_UI_OK;
}

int32_t canopus_p65_ui_rows_get(
    const struct canopus_p65_ui_rows_v1 *rows, uint16_t row_index,
    struct canopus_p65_ui_row_view_v1 *out)
{
    const struct canopus_p65_ui_row_data_v1 *row;
    const struct canopus_ui_node_v1 *node;

    if (rows == 0 || out == 0 || row_index >= rows->count ||
        rows->string_used > CANOPUS_UI_STRING_CAPACITY) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    row = &rows->rows[row_index];
    node = &row->node;
    if (!p65_string_valid(rows->strings, rows->string_used,
                          node->primary_off, node->primary_len) ||
        !p65_string_valid(rows->strings, rows->string_used,
                          node->secondary_off, node->secondary_len)) {
        return CANOPUS_UI_ERR_STATE;
    }

    out->node = node;
    out->style = &row->style;
    out->layout = &row->layout;
    out->value = &row->value;
    out->primary = node->primary_len == 0u
                       ? ""
                       : rows->strings + node->primary_off;
    out->secondary = node->secondary_len == 0u
                         ? ""
                         : rows->strings + node->secondary_off;
    out->generation = rows->generation;
    out->event_id =
        p65_is_interactive(node->type) &&
                (node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) != 0u
            ? node->event_id
            : 0u;
    out->source_index = row->source_index;
    /* Trailing affordances are constructed once. Do not recycle a disabled
     * action without an arrow as an enabled action with an arrow. */
    out->cache_key = (uint8_t)(node->type |
                              (out->event_id != 0u ? 0x40u : 0u));
    return CANOPUS_UI_OK;
}

int32_t canopus_p65_ui_event_binding_init(
    struct canopus_p65_ui_event_binding_v1 *binding,
    struct canopus_ui_context_v1 *ui,
    const struct canopus_p65_ui_row_view_v1 *row)
{
    if (binding == 0 || ui == 0 || row == 0 || row->node == 0 ||
        row->node->key == 0u || row->event_id == 0u) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    binding->magic = CANOPUS_P65_UI_EVENT_BINDING_MAGIC;
    binding->ui = ui;
    binding->generation = row->generation;
    binding->key = row->node->key;
    binding->event_id = row->event_id;
    return CANOPUS_UI_OK;
}

static uintptr_t p65_read_event_context(const uint8_t *event)
{
    uintptr_t value = 0u;
    uint32_t i;

    for (i = 0u; i < sizeof(value); i++) {
        value |= (uintptr_t)event[12u + i] << (i * 8u);
    }
    return value;
}

void canopus_p65_ui_row_click_event(void *event)
{
    const uint8_t *bytes = (const uint8_t *)event;
    uint16_t event_code;
    uintptr_t context_value;
    struct canopus_p65_ui_event_binding_v1 *binding;

    if (bytes == 0) {
        return;
    }
    event_code = (uint16_t)bytes[8] | ((uint16_t)bytes[9] << 8);
    if ((event_code & 0x7FFFu) != 7u) {
        return;
    }
    context_value = p65_read_event_context(bytes);
    if (context_value == 0u) {
        return;
    }
    binding = (struct canopus_p65_ui_event_binding_v1 *)context_value;
    if (binding->magic != CANOPUS_P65_UI_EVENT_BINDING_MAGIC ||
        binding->ui == 0 || binding->generation == 0u ||
        binding->key == 0u || binding->event_id == 0u) {
        return;
    }
    (void)canopus_ui_dispatch_event(binding->ui, binding->generation,
                                    binding->key, binding->event_id);
}
