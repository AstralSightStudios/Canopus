/* Host-testable callback bridge for the P65 LVX native list object. */
#ifndef CANOPUS_MANAGER_P65_LVX_H
#define CANOPUS_MANAGER_P65_LVX_H

#include <stdint.h>
#include "canopus_manager_p65_rows.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_P65_LVX_ABI_MAJOR 1u
#define CANOPUS_P65_LVX_ABI_MINOR 0u

/* Exact 10-word table copied by P65 LVX to list offsets +92..+128. On the
 * 32-bit target this structure must remain exactly 40 bytes. */
typedef uint32_t (*canopus_p65_lvx_row_class_fn)(
    void *list, uint32_t index, void *context);
typedef void *(*canopus_p65_lvx_row_create_fn)(
    void *list, uint32_t index, void *context);
typedef void (*canopus_p65_lvx_row_update_fn)(
    void *list, void *row, uint32_t index, void *context);
typedef uint32_t (*canopus_p65_lvx_row_count_fn)(void *list, void *context);

struct canopus_p65_lvx_callback_table_v1 {
    void *context;                              /* +0 */
    canopus_p65_lvx_row_class_fn row_class;     /* +4 / list+96 */
    canopus_p65_lvx_row_create_fn row_create;   /* +8 / list+100 */
    canopus_p65_lvx_row_update_fn row_update;   /* +12 / list+104 */
    void *reserved_108;                         /* +16 */
    canopus_p65_lvx_row_count_fn row_count;     /* +20 / list+112 */
    void *scroll_start;                         /* +24 / list+116 */
    void *scroll_end;                           /* +28 / list+120 */
    void *extent;                               /* +32 / list+124 */
    void *geometry;                             /* +36 / list+128 */
};

struct canopus_p65_lvx_list_ops_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    int32_t (*create_list)(void *cookie, void *parent,
                           const void *opaque_descriptor, void **out_list);
    int32_t (*configure_list)(void *cookie, void *list,
                              const struct canopus_p65_lvx_callback_table_v1 *table);
    /* Stock initialization/rebuild: also reconstructs incompatible row types
     * and resets the viewport when the semantic list shape changes. */
    int32_t (*finalize_list)(void *cookie, void *list);
    /* Stock in-place update for a snapshot with the same row count/classes. */
    void (*refresh_list)(void *cookie, void *list);
    void (*remove_row_event)(void *cookie, void *list, void *row);
    void *(*create_row)(void *cookie, void *list,
                        const struct canopus_p65_ui_row_view_v1 *view,
                        struct canopus_p65_ui_event_binding_v1 *binding);
    void (*update_row)(void *cookie, void *list, void *row,
                       const struct canopus_p65_ui_row_view_v1 *view,
                       struct canopus_p65_ui_event_binding_v1 *binding);
};

struct canopus_manager_p65_lvx_list_v1 {
    const struct canopus_p65_lvx_list_ops_v1 *ops;
    void *ops_cookie;
    struct canopus_ui_context_v1 *ui;
    struct canopus_p65_ui_rows_v1 rows;
    struct canopus_p65_ui_event_binding_v1 bindings[CANOPUS_UI_MAX_NODES];
    struct canopus_p65_lvx_callback_table_v1 callbacks;
    void *list;
    int32_t callback_error;
    uint8_t snapshot_ready;
    uint8_t configured;
};

int32_t canopus_manager_p65_lvx_list_init(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    struct canopus_ui_context_v1 *ui,
    const struct canopus_p65_lvx_list_ops_v1 *ops, void *ops_cookie,
    void *parent, const void *opaque_descriptor);

int32_t canopus_manager_p65_lvx_list_apply(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    const struct canopus_ui_snapshot_v1 *snapshot);

void *canopus_manager_p65_lvx_list_object(
    const struct canopus_manager_p65_lvx_list_v1 *adapter);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_LVX_H */
