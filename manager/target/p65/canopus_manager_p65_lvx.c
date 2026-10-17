/* Host-testable bridge from semantic row storage to the P65 LVX list ABI. */
#include <stddef.h>
#include "canopus_manager_p65_lvx.h"
#include "canopus_memory.h"

#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(struct canopus_p65_lvx_callback_table_v1) == 40u,
               "P65 LVX list callback table must be 10 words");
_Static_assert(offsetof(struct canopus_p65_lvx_callback_table_v1, row_class) == 4u,
               "P65 row-class callback offset");
_Static_assert(offsetof(struct canopus_p65_lvx_callback_table_v1, row_create) == 8u,
               "P65 row-create callback offset");
_Static_assert(offsetof(struct canopus_p65_lvx_callback_table_v1, row_update) == 12u,
               "P65 row-update callback offset");
_Static_assert(offsetof(struct canopus_p65_lvx_callback_table_v1, row_count) == 20u,
               "P65 row-count callback offset");
#endif

static uint32_t p65_row_class(void *list, uint32_t index, void *context)
{
    struct canopus_manager_p65_lvx_list_v1 *adapter =
        (struct canopus_manager_p65_lvx_list_v1 *)context;
    struct canopus_p65_ui_row_view_v1 view;

    if (adapter == 0 || list != adapter->list || !adapter->snapshot_ready ||
        index > 0xFFFFu ||
        canopus_p65_ui_rows_get(&adapter->rows, (uint16_t)index, &view) !=
            CANOPUS_UI_OK) {
        if (adapter != 0) {
            adapter->callback_error = CANOPUS_UI_ERR_STATE;
        }
        return 0u;
    }
    return view.cache_key;
}

static void p65_binding_set(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    struct canopus_p65_ui_event_binding_v1 *binding,
    const struct canopus_p65_ui_row_view_v1 *view)
{
    if (view->event_id != 0u) {
        if (canopus_p65_ui_event_binding_init(binding, adapter->ui, view) !=
            CANOPUS_UI_OK) {
            adapter->callback_error = CANOPUS_UI_ERR_STATE;
        }
        return;
    }
    binding->magic = 0u;
    binding->ui = adapter->ui;
    binding->generation = view->generation;
    binding->key = view->node->key;
    binding->event_id = 0u;
}

static struct canopus_p65_ui_event_binding_v1 *p65_binding_allocate(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    const struct canopus_p65_ui_row_view_v1 *view)
{
    uint32_t i;

    for (i = 0u; i < CANOPUS_P65_UI_MAX_ROWS; i++) {
        struct canopus_p65_ui_event_binding_v1 *binding =
            &adapter->bindings[i];
        if (binding->used == 0u) {
            canopus_memset(binding, 0, sizeof(*binding));
            binding->used = 1u;
            p65_binding_set(adapter, binding, view);
            if (adapter->callback_error != 0) {
                binding->used = 0u;
                return 0;
            }
            return binding;
        }
    }
    adapter->callback_error = CANOPUS_UI_ERR_CAPACITY;
    return 0;
}

static struct canopus_p65_ui_event_binding_v1 *p65_binding_for_object(
    struct canopus_manager_p65_lvx_list_v1 *adapter, void *row)
{
    uint32_t i;

    for (i = 0u; i < CANOPUS_P65_UI_MAX_ROWS; i++) {
        struct canopus_p65_ui_event_binding_v1 *binding =
            &adapter->bindings[i];
        if (binding->used != 0u && binding->row_object == row) {
            return binding;
        }
    }
    return 0;
}

static void *p65_row_create(void *list, uint32_t index, void *context)
{
    struct canopus_manager_p65_lvx_list_v1 *adapter =
        (struct canopus_manager_p65_lvx_list_v1 *)context;
    struct canopus_p65_ui_row_view_v1 view;
    struct canopus_p65_ui_event_binding_v1 *binding;
    void *row;

    if (adapter == 0 || list != adapter->list || !adapter->snapshot_ready ||
        index > 0xFFFFu ||
        canopus_p65_ui_rows_get(&adapter->rows, (uint16_t)index, &view) !=
            CANOPUS_UI_OK) {
        if (adapter != 0) {
            adapter->callback_error = CANOPUS_UI_ERR_STATE;
        }
        return 0;
    }
    binding = p65_binding_allocate(adapter, &view);
    if (binding == 0) {
        return 0;
    }
    row = adapter->ops->create_row(adapter->ops_cookie, list, &view, binding);
    if (row == 0) {
        binding->magic = 0u;
        binding->used = 0u;
        adapter->callback_error = CANOPUS_UI_ERR_BACKEND;
        return 0;
    }
    binding->row_object = row;
    return row;
}

static void p65_row_update(void *list, void *row, uint32_t index,
                           void *context)
{
    struct canopus_manager_p65_lvx_list_v1 *adapter =
        (struct canopus_manager_p65_lvx_list_v1 *)context;
    struct canopus_p65_ui_row_view_v1 view;
    struct canopus_p65_ui_event_binding_v1 *binding;

    if (adapter == 0 || list != adapter->list || row == 0 ||
        !adapter->snapshot_ready || index > 0xFFFFu ||
        canopus_p65_ui_rows_get(&adapter->rows, (uint16_t)index, &view) !=
            CANOPUS_UI_OK) {
        if (adapter != 0) {
            adapter->callback_error = CANOPUS_UI_ERR_STATE;
        }
        return;
    }
    binding = p65_binding_for_object(adapter, row);
    if (binding == 0) {
        /* A structural rebuild can reuse a firmware cache object whose old
         * callback was removed before the rebuild. Give it a fresh binding. */
        binding = p65_binding_allocate(adapter, &view);
        if (binding == 0) {
            return;
        }
        binding->row_object = row;
    }
    adapter->ops->remove_row_event(adapter->ops_cookie, list, row);
    p65_binding_set(adapter, binding, &view);
    if (adapter->callback_error != 0) {
        return;
    }
    adapter->ops->update_row(adapter->ops_cookie, list, row, &view, binding);
}

static uint32_t p65_row_count(void *list, void *context)
{
    struct canopus_manager_p65_lvx_list_v1 *adapter =
        (struct canopus_manager_p65_lvx_list_v1 *)context;

    if (adapter == 0 || list != adapter->list) {
        if (adapter != 0) {
            adapter->callback_error = CANOPUS_UI_ERR_STATE;
        }
        return 0u;
    }
    return adapter->snapshot_ready ? adapter->rows.count : 0u;
}

static int p65_ops_valid(const struct canopus_p65_lvx_list_ops_v1 *ops)
{
    return ops != 0 && ops->struct_size == sizeof(*ops) &&
           ops->abi_major == CANOPUS_P65_LVX_ABI_MAJOR &&
           ops->abi_minor <= CANOPUS_P65_LVX_ABI_MINOR &&
           ops->create_list != 0 && ops->configure_list != 0 &&
           ops->finalize_list != 0 && ops->refresh_list != 0 &&
           ops->remove_row_event != 0 && ops->create_row != 0 &&
           ops->update_row != 0;
}

int32_t canopus_manager_p65_lvx_list_init(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    struct canopus_ui_context_v1 *ui,
    const struct canopus_p65_lvx_list_ops_v1 *ops, void *ops_cookie,
    void *parent, const void *opaque_descriptor)
{
    int32_t rc;

    if (adapter == 0 || ui == 0 || !p65_ops_valid(ops) || parent == 0 ||
        opaque_descriptor == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memset(adapter, 0, sizeof(*adapter));
    adapter->ops = ops;
    adapter->ops_cookie = ops_cookie;
    adapter->ui = ui;
    adapter->callbacks.context = adapter;
    adapter->callbacks.row_class = p65_row_class;
    adapter->callbacks.row_create = p65_row_create;
    adapter->callbacks.row_update = p65_row_update;
    adapter->callbacks.row_count = p65_row_count;

    rc = ops->create_list(ops_cookie, parent, opaque_descriptor,
                          &adapter->list);
    if (rc != 0 || adapter->list == 0) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    rc = ops->configure_list(ops_cookie, adapter->list,
                             &adapter->callbacks);
    if (rc != 0) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    rc = ops->finalize_list(ops_cookie, adapter->list);
    if (rc != 0) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    adapter->configured = 1u;
    return CANOPUS_UI_OK;
}

int32_t canopus_manager_p65_lvx_list_apply(
    struct canopus_manager_p65_lvx_list_v1 *adapter,
    const struct canopus_ui_snapshot_v1 *snapshot)
{
    int32_t rc;
    uint8_t old_classes[CANOPUS_P65_UI_MAX_ROWS];
    uint16_t old_count;
    uint16_t i;
    int rebuild = 0;

    if (adapter == 0 || snapshot == 0 || !adapter->configured ||
        adapter->list == 0) {
        return CANOPUS_UI_ERR_STATE;
    }
    old_count = adapter->rows.count;
    for (i = 0u; i < old_count; i++) {
        struct canopus_p65_ui_row_view_v1 view;
        rc = canopus_p65_ui_rows_get(&adapter->rows, i, &view);
        if (rc != CANOPUS_UI_OK) {
            return rc;
        }
        old_classes[i] = view.cache_key;
    }
    rc = canopus_p65_ui_rows_build(&adapter->rows, snapshot);
    if (rc != CANOPUS_UI_OK) {
        return rc;
    }
    adapter->callback_error = 0;
    if (adapter->snapshot_ready) {
        rebuild = old_count != adapter->rows.count;
        for (i = 0u; !rebuild && i < old_count; i++) {
            struct canopus_p65_ui_row_view_v1 view;
            (void)canopus_p65_ui_rows_get(&adapter->rows, i, &view);
            rebuild = old_classes[i] != view.cache_key;
        }
    }
    adapter->snapshot_ready = 1u;
    if (rebuild) {
        /* Mode-0 refresh updates rows by index without checking their class.
         * A label must never be passed to a settings-card updater (or vice
         * versa). Use the stock rebuild path when the shape changes.
         * Detach old callbacks before firmware deletes/recycles the objects;
         * otherwise dead rows consume the bounded binding pool forever. */
        for (i = 0u; i < CANOPUS_P65_UI_MAX_ROWS; i++) {
            adapter->bindings[i].magic = 0u;
        }
        for (i = 0u; i < CANOPUS_P65_UI_MAX_ROWS; i++) {
            struct canopus_p65_ui_event_binding_v1 *binding =
                &adapter->bindings[i];
            if (binding->used && binding->row_object != 0) {
                adapter->ops->remove_row_event(adapter->ops_cookie,
                                               adapter->list,
                                               binding->row_object);
            }
        }
        canopus_memset(adapter->bindings, 0, sizeof(adapter->bindings));
        rc = adapter->ops->finalize_list(adapter->ops_cookie, adapter->list);
        if (rc != 0) {
            return CANOPUS_UI_ERR_BACKEND;
        }
    } else {
        adapter->ops->refresh_list(adapter->ops_cookie, adapter->list);
    }
    return adapter->callback_error == 0
               ? CANOPUS_UI_OK
               : CANOPUS_UI_ERR_BACKEND;
}

void *canopus_manager_p65_lvx_list_object(
    const struct canopus_manager_p65_lvx_list_v1 *adapter)
{
    return adapter == 0 ? 0 : adapter->list;
}
