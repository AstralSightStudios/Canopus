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

static int p65_string_valid(const struct canopus_ui_snapshot_v1 *snapshot,
                            uint16_t offset, uint16_t length)
{
    uint32_t end = (uint32_t)offset + (uint32_t)length;

    if (length == 0u) {
        return 1;
    }
    return end < snapshot->string_used &&
           snapshot->strings[end] == '\0';
}

int32_t canopus_p65_ui_rows_build(
    struct canopus_p65_ui_rows_v1 *rows,
    const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_p65_ui_rows_v1 next = {0};
    uint16_t i;

    if (rows == 0 || snapshot == 0 ||
        snapshot->abi_major != CANOPUS_UI_ABI_MAJOR ||
        snapshot->abi_minor > CANOPUS_UI_ABI_MINOR ||
        snapshot->node_count > CANOPUS_UI_MAX_NODES ||
        snapshot->string_used > CANOPUS_UI_STRING_CAPACITY) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }

    next.snapshot = snapshot;
    next.generation = snapshot->generation;
    for (i = 0u; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        int visible = p65_node_visible(snapshot, i);

        if (visible < 0) {
            return CANOPUS_UI_ERR_STATE;
        }
        if (visible == 0 || p65_is_container(node->type)) {
            continue;
        }
        if (!p65_is_row_type(node->type)) {
            return CANOPUS_UI_ERR_UNSUPPORTED;
        }
        if (!p65_string_valid(snapshot, node->primary_off,
                              node->primary_len) ||
            !p65_string_valid(snapshot, node->secondary_off,
                              node->secondary_len)) {
            return CANOPUS_UI_ERR_STATE;
        }
        if (next.count >= CANOPUS_P65_UI_MAX_ROWS) {
            return CANOPUS_UI_ERR_CAPACITY;
        }
        next.source_indices[next.count++] = i;
    }

    *rows = next;
    return CANOPUS_UI_OK;
}

int32_t canopus_p65_ui_rows_get(
    const struct canopus_p65_ui_rows_v1 *rows, uint16_t row_index,
    struct canopus_p65_ui_row_view_v1 *out)
{
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;
    uint16_t source_index;

    if (rows == 0 || out == 0 || rows->snapshot == 0 ||
        row_index >= rows->count) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    snapshot = rows->snapshot;
    source_index = rows->source_indices[row_index];
    if (source_index >= snapshot->node_count ||
        !p65_string_valid(snapshot, snapshot->nodes[source_index].primary_off,
                          snapshot->nodes[source_index].primary_len) ||
        !p65_string_valid(snapshot,
                          snapshot->nodes[source_index].secondary_off,
                          snapshot->nodes[source_index].secondary_len)) {
        return CANOPUS_UI_ERR_STATE;
    }

    node = &snapshot->nodes[source_index];
    out->node = node;
    out->style = &snapshot->styles[source_index];
    out->value = &snapshot->values[source_index];
    out->primary = node->primary_len == 0u
                       ? ""
                       : snapshot->strings + node->primary_off;
    out->secondary = node->secondary_len == 0u
                         ? ""
                         : snapshot->strings + node->secondary_off;
    out->generation = rows->generation;
    out->event_id =
        p65_is_interactive(node->type) &&
                (node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) != 0u
            ? node->event_id
            : 0u;
    out->source_index = source_index;
    /* P65 +96 is a byte-sized row-class/cache discriminator. One semantic
     * node type maps to one recycled native list row class. */
    out->cache_key = (uint8_t)node->type;
    return CANOPUS_UI_OK;
}
