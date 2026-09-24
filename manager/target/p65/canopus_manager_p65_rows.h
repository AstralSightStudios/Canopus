/* Bounded semantic-row view for the P65 callback-driven LVX list adapter. */
#ifndef CANOPUS_MANAGER_P65_ROWS_H
#define CANOPUS_MANAGER_P65_ROWS_H

#include <stdint.h>
#include "canopus_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_P65_UI_MAX_ROWS CANOPUS_UI_MAX_NODES

struct canopus_p65_ui_row_data_v1 {
    struct canopus_ui_node_v1 node;
    struct canopus_ui_style_v1 style;
    struct canopus_ui_layout_v1 layout;
    struct canopus_ui_value_v1 value;
    uint16_t source_index;
};

struct canopus_p65_ui_rows_v1 {
    uint32_t generation;
    uint16_t count;
    uint16_t string_used;
    struct canopus_p65_ui_row_data_v1 rows[CANOPUS_P65_UI_MAX_ROWS];
    char strings[CANOPUS_UI_STRING_CAPACITY];
};

struct canopus_p65_ui_row_view_v1 {
    const struct canopus_ui_node_v1 *node;
    const struct canopus_ui_style_v1 *style;
    const struct canopus_ui_layout_v1 *layout;
    const struct canopus_ui_value_v1 *value;
    const char *primary;
    const char *secondary;
    uint32_t generation;
    uint32_t event_id;
    uint16_t source_index;
    uint8_t cache_key;
};

#define CANOPUS_P65_UI_EVENT_BINDING_MAGIC 0x50363545u /* "P65E" */

/* Stable context passed as LVGL event user_data. Keep it alive until the
 * firmware removes the row callback and all page references have drained. */
struct canopus_p65_ui_event_binding_v1 {
    uint32_t magic;
    struct canopus_ui_context_v1 *ui;
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
    void *row_object;
    uint8_t used;
    uint8_t reserved[3];
};

/* Flatten visible row-like nodes in snapshot order and copy their bounded
 * metadata/strings into `rows`. This is required because UI `apply` receives a
 * staging snapshot whose storage may be reused as soon as the call returns.
 * Navigation/list/scroll nodes are containers; visible descendants remain
 * eligible rows. Invalid input leaves the previous `rows` value intact. */
int32_t canopus_p65_ui_rows_build(
    struct canopus_p65_ui_rows_v1 *rows,
    const struct canopus_ui_snapshot_v1 *snapshot);

/* Resolve one flattened row; string pointers refer to adapter-owned storage. */
int32_t canopus_p65_ui_rows_get(
    const struct canopus_p65_ui_rows_v1 *rows, uint16_t row_index,
    struct canopus_p65_ui_row_view_v1 *out);

int32_t canopus_p65_ui_event_binding_init(
    struct canopus_p65_ui_event_binding_v1 *binding,
    struct canopus_ui_context_v1 *ui,
    const struct canopus_p65_ui_row_view_v1 *row);

/* LVGL event callback used by the P65 row adapter. It accepts only event 7
 * (click), extracts user_data from the verified P65 event offsets +8/+12,
 * and lets canopus_ui_dispatch_event reject stale/disabled rows. */
void canopus_p65_ui_row_click_event(void *event);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_ROWS_H */
