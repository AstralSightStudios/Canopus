/* Host tests: injected P65 LVX callback-table adapter. */
#include "canopus_test.h"
#include "canopus_manager_p65_lvx.h"
#include "canopus_manager_p65_backend.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_runtime.h"
#include <string.h>

struct fake_p65_row {
    canopus_ui_node_id key;
    uint32_t event_id;
    uint32_t generation;
    uint16_t type;
    struct canopus_p65_ui_event_binding_v1 *binding;
    char primary[32];
    char secondary[32];
};

struct fake_p65_lvx {
    uint32_t create_list_count;
    uint32_t configure_count;
    uint32_t finalize_count;
    uint32_t refresh_count;
    uint32_t remove_event_count;
    uint32_t create_row_count;
    uint32_t update_row_count;
    uint32_t row_count;
    int list_token;
    struct canopus_p65_lvx_callback_table_v1 callbacks;
    struct fake_p65_row rows[CANOPUS_P65_UI_MAX_ROWS];
};

struct fake_p65_events {
    uint32_t calls;
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
};

static int32_t fake_list_create(void *cookie, void *parent,
                                const void *descriptor, void **out_list)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    CHECK(parent != 0);
    CHECK(descriptor != 0);
    fake->create_list_count++;
    *out_list = &fake->list_token;
    return 0;
}

static int32_t fake_list_configure(
    void *cookie, void *list,
    const struct canopus_p65_lvx_callback_table_v1 *table)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    CHECK(list == &fake->list_token);
    CHECK(table != 0);
    if (table == 0) {
        return -1;
    }
    fake->callbacks = *table;
    fake->configure_count++;
    return 0;
}

static int32_t fake_list_finalize(void *cookie, void *list)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    CHECK(list == &fake->list_token);
    CHECK(fake->callbacks.row_count != 0);
    CHECK_EQ(fake->callbacks.row_count(list, fake->callbacks.context), 0u);
    fake->finalize_count++;
    return 0;
}

static void fake_store_row(struct fake_p65_row *row,
                           const struct canopus_p65_ui_row_view_v1 *view,
                           struct canopus_p65_ui_event_binding_v1 *binding)
{
    size_t primary_len = strlen(view->primary);
    size_t secondary_len = strlen(view->secondary);
    if (primary_len >= sizeof(row->primary)) {
        primary_len = sizeof(row->primary) - 1u;
    }
    if (secondary_len >= sizeof(row->secondary)) {
        secondary_len = sizeof(row->secondary) - 1u;
    }
    memcpy(row->primary, view->primary, primary_len);
    row->primary[primary_len] = '\0';
    memcpy(row->secondary, view->secondary, secondary_len);
    row->secondary[secondary_len] = '\0';
    row->key = view->node->key;
    row->event_id = view->event_id;
    row->generation = view->generation;
    row->type = view->node->type;
    row->binding = binding;
}

static void *fake_row_create(
    void *cookie, void *list,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    struct fake_p65_row *row;
    CHECK(list == &fake->list_token);
    if (fake->create_row_count >= CANOPUS_P65_UI_MAX_ROWS) {
        return 0;
    }
    row = &fake->rows[fake->create_row_count++];
    fake_store_row(row, view, binding);
    return row;
}

static void fake_row_update(
    void *cookie, void *list, void *row_object,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    CHECK(list == &fake->list_token);
    CHECK(row_object != 0);
    if (row_object != 0) {
        fake_store_row((struct fake_p65_row *)row_object, view, binding);
        fake->update_row_count++;
    }
}

static void fake_remove_row_event(void *cookie, void *list, void *row)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    CHECK(list == &fake->list_token);
    CHECK(row != 0);
    fake->remove_event_count++;
}

static void fake_list_refresh(void *cookie, void *list)
{
    struct fake_p65_lvx *fake = (struct fake_p65_lvx *)cookie;
    uint32_t count;
    uint32_t i;

    CHECK(list == &fake->list_token);
    fake->refresh_count++;
    count = fake->callbacks.row_count(list, fake->callbacks.context);
    fake->row_count = count;
    for (i = 0u; i < count; i++) {
        uint32_t key = fake->callbacks.row_class(
            list, i, fake->callbacks.context);
        CHECK(key < CANOPUS_UI_MAX_NODES);
        if (i < fake->create_row_count) {
            fake->callbacks.row_update(list, &fake->rows[i], i,
                                       fake->callbacks.context);
        } else {
            void *row = fake->callbacks.row_create(
                list, i, fake->callbacks.context);
            CHECK(row != 0);
        }
    }
}

static const struct canopus_p65_lvx_list_ops_v1 fake_ops = {
    sizeof(struct canopus_p65_lvx_list_ops_v1),
    CANOPUS_P65_LVX_ABI_MAJOR,
    CANOPUS_P65_LVX_ABI_MINOR,
    fake_list_create,
    fake_list_configure,
    fake_list_finalize,
    fake_list_refresh,
    fake_remove_row_event,
    fake_row_create,
    fake_row_update,
};

static int32_t fake_ui_apply(void *cookie,
                             const struct canopus_ui_snapshot_v1 *snapshot)
{
    (void)cookie;
    (void)snapshot;
    return CANOPUS_UI_OK;
}

static int32_t fake_ui_event(void *cookie, uint32_t generation,
                             canopus_ui_node_id key, uint32_t event_id)
{
    struct fake_p65_events *events = (struct fake_p65_events *)cookie;
    events->calls++;
    events->generation = generation;
    events->key = key;
    events->event_id = event_id;
    return CANOPUS_UI_OK;
}

static const struct canopus_ui_backend_v1 fake_ui_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    fake_ui_apply,
};

static int build_snapshot(struct canopus_ui_context_v1 *ui,
                          const char *section_text,
                          const char *action_text)
{
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 page = {
        sizeof(page), "Manager", 7u
    };
    struct canopus_ui_section_props_v1 section = {
        sizeof(section), section_text, (uint32_t)strlen(section_text)
    };
    struct canopus_ui_action_row_props_v1 action = {
        sizeof(action), action_text, (uint32_t)strlen(action_text),
        "Open", 4u, 77u, 1u
    };
    struct canopus_ui_action_row_props_v1 second_action = {
        sizeof(second_action), "Second module", 13u,
        "Open second", 11u, 78u, 1u
    };

    if (canopus_ui_tree_begin(ui, &tree) != CANOPUS_UI_OK ||
        canopus_ui_navigation_page(tree, 1u, &page) != CANOPUS_UI_OK ||
        canopus_ui_section(tree, 2u, &section) != CANOPUS_UI_OK ||
        canopus_ui_action_row(tree, 3u, &action) != CANOPUS_UI_OK ||
        canopus_ui_action_row(tree, 4u, &second_action) != CANOPUS_UI_OK ||
        canopus_ui_end(tree) != CANOPUS_UI_OK ||
        canopus_ui_end(tree) != CANOPUS_UI_OK ||
        canopus_ui_tree_commit(tree) != CANOPUS_UI_OK) {
        return -1;
    }
    return 0;
}

TEST(p65_lvx_adapter_binds_row_callbacks_and_refreshes_owned_snapshot)
{
    struct canopus_ui_context_v1 ui;
    struct fake_p65_events events = {0};
    struct fake_p65_lvx fake = {0};
    struct canopus_manager_p65_lvx_list_v1 adapter;
    struct fake_p65_row *action_row;
    uint8_t event[12u + sizeof(uintptr_t)] = {0};
    uintptr_t user_data;
    uint32_t i;

    CHECK(canopus_ui_context_init(&ui, &fake_ui_backend, 0,
                                  fake_ui_event, &events) == CANOPUS_UI_OK);
    CHECK(canopus_manager_p65_lvx_list_init(
              &adapter, &ui, &fake_ops, &fake, &fake, &fake.list_token) ==
          CANOPUS_UI_OK);
    CHECK_EQ(fake.create_list_count, 1u);
    CHECK_EQ(fake.configure_count, 1u);
    CHECK_EQ(fake.finalize_count, 1u);
    CHECK(fake.callbacks.context == &adapter);
    CHECK(fake.callbacks.row_class != 0);
    CHECK(fake.callbacks.row_create != 0);
    CHECK(fake.callbacks.row_update != 0);
    CHECK(fake.callbacks.row_count != 0);
    CHECK(fake.callbacks.reserved_108 == 0);
    CHECK(fake.callbacks.scroll_start == 0);
    CHECK(fake.callbacks.scroll_end == 0);
    CHECK(fake.callbacks.extent == 0);
    CHECK(fake.callbacks.geometry == 0);

    CHECK(build_snapshot(&ui, "Modules", "Example module") == 0);
    CHECK(canopus_manager_p65_lvx_list_apply(
              &adapter, canopus_ui_current(&ui)) == CANOPUS_UI_OK);
    CHECK_EQ(fake.row_count, 3u);
    CHECK_EQ(fake.create_row_count, 3u);
    CHECK(strcmp(fake.rows[0].primary, "Modules") == 0);
    CHECK(strcmp(fake.rows[1].primary, "Example module") == 0);
    CHECK(strcmp(fake.rows[2].primary, "Second module") == 0);
    CHECK_EQ(fake.rows[1].event_id, 77u);
    CHECK_EQ(fake.rows[1].generation, 1u);
    CHECK_EQ(fake.rows[1].type, CANOPUS_UI_NODE_ACTION_ROW);
    CHECK(fake.rows[1].binding != fake.rows[2].binding);

    action_row = &fake.rows[1];
    event[8] = 7u;
    user_data = (uintptr_t)action_row->binding;
    for (i = 0u; i < sizeof(user_data); i++) {
        event[12u + i] = (uint8_t)(user_data >> (i * 8u));
    }
    canopus_p65_ui_row_click_event(event);
    CHECK_EQ(events.calls, 1u);
    CHECK_EQ(events.key, 3u);
    CHECK_EQ(events.event_id, 77u);

    CHECK(build_snapshot(&ui, "Modules", "Updated module") == 0);
    CHECK(canopus_manager_p65_lvx_list_apply(
              &adapter, canopus_ui_current(&ui)) == CANOPUS_UI_OK);
    CHECK_EQ(fake.refresh_count, 2u);
    CHECK(fake.update_row_count >= 3u);
    CHECK(strcmp(fake.rows[1].primary, "Updated module") == 0);
    CHECK_EQ(fake.rows[1].generation, 2u);
}

static struct fake_p65_row *fake_find_event(struct fake_p65_lvx *fake,
                                             uint32_t event_id)
{
    uint32_t i;
    for (i = 0u; i < fake->row_count; i++) {
        if (fake->rows[i].event_id == event_id) {
            return &fake->rows[i];
        }
    }
    return 0;
}

static void dispatch_fake_click(struct fake_p65_row *row)
{
    uint8_t event[12u + sizeof(uintptr_t)] = {0};
    uintptr_t user_data = (uintptr_t)row->binding;
    uint32_t i;
    event[8] = 7u;
    for (i = 0u; i < sizeof(user_data); i++) {
        event[12u + i] = (uint8_t)(user_data >> (i * 8u));
    }
    canopus_p65_ui_row_click_event(event);
}

static int p65_backend_test_persist(void *cookie, const uint8_t *data,
                                    uint32_t length)
{
    (void)cookie;
    return data != 0 && length != 0u ? 0 : -1;
}

static int p65_backend_test_restore(void *cookie, uint8_t *data,
                                   uint32_t length)
{
    (void)cookie;
    (void)data;
    (void)length;
    return 1;
}

static const struct canopus_sup_platform_v1 p65_backend_test_platform = {
    "xiaomi-p65-3.100.043", 0, 0, 0, 0, 0, p65_backend_test_persist,
    p65_backend_test_restore
};

TEST(p65_manager_backend_queries_and_routes_through_local_client)
{
    struct canopus_supervisor_v1 supervisor;
    struct canopus_manager_p65_backend_v1 backend;
    struct fake_p65_lvx fake = {0};
    struct fake_p65_row *row;

    CHECK(canopus_supervisor_init(&supervisor, 9u,
                                  &p65_backend_test_platform, 0) == 0);
    CHECK(canopus_supervisor_add_module(
              &supervisor, CANOPUS_LIFECYCLE_REMOVABLE, 3u, 1u,
              "mod.example") == 0);
    CHECK(canopus_manager_p65_backend_init(
              &backend, &supervisor, &fake_ops, &fake, &fake,
              &fake.list_token, "xiaomi-p65-3.100.043", "3.100.043",
              "d516af0:nx_best1502p_ap", 9u) == CANOPUS_UI_OK);
    CHECK_EQ(backend.model.module_count, 1u);
    CHECK_EQ(backend.model.framework_revision, 9u);
    CHECK(strcmp(backend.model.modules[0].module_id, "mod.example") == 0);
    CHECK_EQ(backend.model.view, CANOPUS_MANAGER_VIEW_DEVICE);
    CHECK_EQ(fake.refresh_count, 1u);

    row = fake_find_event(&fake, CANOPUS_MANAGER_EVENT_SHOW_MODULES);
    CHECK(row != 0);
    if (row != 0) {
        dispatch_fake_click(row);
    }
    CHECK_EQ(backend.model.view, CANOPUS_MANAGER_VIEW_MODULE_LIST);
    row = fake_find_event(&fake, CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE);
    CHECK(row != 0);
    if (row != 0) {
        dispatch_fake_click(row);
    }
    CHECK_EQ(backend.model.view, CANOPUS_MANAGER_VIEW_MODULE_DETAIL);
    CHECK_EQ(backend.model.selected, 0u);
    row = fake_find_event(&fake, CANOPUS_MANAGER_EVENT_ENABLE);
    CHECK(row != 0);
    if (row != 0) {
        dispatch_fake_click(row);
    }
    CHECK_EQ(backend.native.confirm_event, CANOPUS_MANAGER_EVENT_ENABLE);
    row = fake_find_event(&fake, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(row != 0);
    if (row != 0) {
        dispatch_fake_click(row);
    }
    CHECK_EQ(supervisor.modules[0].intent, CANOPUS_SUP_INTENT_ENABLED);
    CHECK_EQ(backend.model.modules[0].state, CANOPUS_STATE_ENABLED);
    CHECK_EQ(fake.refresh_count, 5u);
    CHECK(canopus_manager_p65_backend_close(&backend) == CANOPUS_UI_OK);
}

static const struct test_registry p65_lvx_adapter_tests[] = {
    { "p65_lvx_adapter_binds_row_callbacks_and_refreshes_owned_snapshot",
      p65_lvx_adapter_binds_row_callbacks_and_refreshes_owned_snapshot_wrapper },
    { "p65_manager_backend_queries_and_routes_through_local_client",
      p65_manager_backend_queries_and_routes_through_local_client_wrapper },
};

int run_p65_lvx_adapter_tests(void)
{
    RUN_TESTS(p65_lvx_adapter_tests,
              sizeof(p65_lvx_adapter_tests) /
                  sizeof(p65_lvx_adapter_tests[0]));
}
