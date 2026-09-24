/* Bounded semantic-row view for the P65 callback-driven LVX list adapter. */
#ifndef CANOPUS_MANAGER_P65_ROWS_H
#define CANOPUS_MANAGER_P65_ROWS_H

#include <stdint.h>
#include "canopus_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_P65_UI_MAX_ROWS CANOPUS_UI_MAX_NODES

struct canopus_p65_ui_rows_v1 {
    const struct canopus_ui_snapshot_v1 *snapshot;
    uint32_t generation;
    uint16_t count;
    uint16_t source_indices[CANOPUS_P65_UI_MAX_ROWS];
};

struct canopus_p65_ui_row_view_v1 {
    const struct canopus_ui_node_v1 *node;
    const struct canopus_ui_style_v1 *style;
    const struct canopus_ui_value_v1 *value;
    const char *primary;
    const char *secondary;
    uint32_t generation;
    uint32_t event_id;
    uint16_t source_index;
    uint8_t cache_key;
};

/* Flatten visible row-like nodes in snapshot order. Navigation/list/scroll
 * nodes are containers; their visible descendants remain eligible rows. */
int32_t canopus_p65_ui_rows_build(
    struct canopus_p65_ui_rows_v1 *rows,
    const struct canopus_ui_snapshot_v1 *snapshot);

/* Resolve one flattened row without copying snapshot-owned strings. */
int32_t canopus_p65_ui_rows_get(
    const struct canopus_p65_ui_rows_v1 *rows, uint16_t row_index,
    struct canopus_p65_ui_row_view_v1 *out);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_ROWS_H */
