/* Host tests: Manager semantic native UI controller. */
#include "canopus_test.h"
#include "canopus_manager_native.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"
#include <string.h>

struct manager_native_backend {
    struct canopus_ui_snapshot_v1 snapshot;
    uint32_t applies;
};

static uint32_t native_transport_command;
static uint32_t native_transport_calls;
static char native_transport_payload[CANOPUS_MANAGER_STAGE_TOKEN_MAX];

static int32_t native_apply(void *cookie,
                            const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct manager_native_backend *backend =
        (struct manager_native_backend *)cookie;
    backend->snapshot = *snapshot;
    backend->applies++;
    return 0;
}

static const struct canopus_ui_backend_v1 native_backend_api = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    native_apply,
};

static int native_transport(const struct canopus_proto_request_v1 *request,
                            const void *payload,
                            struct canopus_proto_response_v1 *response,
                            void *cookie)
{
    uint32_t copy;
    (void)cookie;
    native_transport_command = request->command;
    native_transport_calls++;
    native_transport_payload[0] = '\0';
    if (payload != 0 && request->payload_size != 0u) {
        copy = request->payload_size;
        if (copy >= sizeof(native_transport_payload)) {
            copy = sizeof(native_transport_payload) - 1u;
        }
        canopus_memcpy(native_transport_payload, payload, copy);
        native_transport_payload[copy] = '\0';
    }
    canopus_proto_response_init(response, request->request_id,
                                CANOPUS_RESULT_ACCEPTED, 0);
    return 0;
}

static const struct canopus_ui_node_v1 *find_event(
    const struct canopus_ui_snapshot_v1 *snapshot, uint32_t event_id)
{
    uint16_t i;
    for (i = 0; i < snapshot->node_count; i++) {
        if (snapshot->nodes[i].event_id == event_id) {
            return &snapshot->nodes[i];
        }
    }
    return 0;
}

static const struct canopus_ui_node_v1 *find_primary(
    const struct canopus_ui_snapshot_v1 *snapshot, const char *text)
{
    uint16_t i;
    for (i = 0; i < snapshot->node_count; i++) {
        if (strcmp(snapshot->strings + snapshot->nodes[i].primary_off,
                   text) == 0) {
            return &snapshot->nodes[i];
        }
    }
    return 0;
}

static void add_native_module(struct canopus_manager_model_v1 *model,
                              uint32_t lifecycle_class)
{
    struct canopus_manager_module_v1 module;
    canopus_memset(&module, 0, sizeof(module));
    canopus_buf_copy(module.module_id, sizeof(module.module_id), "mod.hello");
    module.lifecycle_class = lifecycle_class;
    module.state = lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
        CANOPUS_STATE_ACTIVE : CANOPUS_STATE_BOOT_RESIDENT;
    module.version = 7;
    module.signature_ok = 1;
    module.has_previous = 1;
    CHECK(canopus_manager_upsert_module(model, &module) == 0);
}

TEST(manager_native_renders_device_prefabs)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *modules;
    const struct canopus_ui_node_v1 *manager;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    canopus_manager_set_identity(&model, "xiaomi-band-10-pro-3.101.030",
                                 "3.101.030", "CONBINE_LTALM078", 5);
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    CHECK(snapshot != 0);
    CHECK(snapshot->generation == 1);
    CHECK(snapshot->nodes[0].type == CANOPUS_UI_NODE_NAVIGATION_PAGE);
    CHECK(strcmp(snapshot->strings + snapshot->nodes[0].primary_off,
                 "Canopus") == 0);
    CHECK(snapshot->styles[0].text_style == CANOPUS_UI_TEXT_TITLE);
    manager = find_primary(snapshot, "Manager");
    CHECK(manager != 0);
    CHECK(strcmp(snapshot->strings + manager->secondary_off,
                 "Native UI ABI 1.4") == 0);
    CHECK(find_primary(snapshot, "Firmware") != 0);
    CHECK(find_primary(snapshot, "Build") != 0);
    CHECK(find_primary(snapshot, "Target") != 0);
    modules = find_event(snapshot, CANOPUS_MANAGER_EVENT_SHOW_MODULES);
    CHECK(modules != 0);
    CHECK(modules->type == CANOPUS_UI_NODE_ACTION_ROW);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_SAFE_MODE) == 0);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_INSTALL) != 0);
    CHECK((find_event(snapshot, CANOPUS_MANAGER_EVENT_INSTALL)->flags &
           CANOPUS_UI_NODE_FLAG_ENABLED) == 0u);
}

TEST(manager_native_overview_surfaces_supervisor_error)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *error;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    canopus_manager_set_identity(&model, "xiaomi-band-10-pro-3.101.030",
                                 "3.101.030", "CONBINE_LTALM078", 5);
    /* a silently-failed registry write/restore must be visible, not hidden */
    model.error_code = -11; /* CANOPUS_SUP_ERR_REGISTRY */
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    CHECK(snapshot != 0);
    error = find_primary(snapshot, "Error");
    CHECK(error != 0);
    CHECK(strcmp(snapshot->strings + error->secondary_off,
                 "err -11 registry corrupt") == 0);

    /* a clean supervisor shows no error row */
    model.error_code = 0;
    CHECK(canopus_manager_native_render(&native) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    CHECK(find_primary(snapshot, "Error") == 0);
}

TEST(manager_native_navigates_list_and_detail)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    add_native_module(&model, CANOPUS_LIFECYCLE_REMOVABLE);
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_SHOW_MODULES);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(model.view == CANOPUS_MANAGER_VIEW_MODULE_LIST);

    snapshot = canopus_ui_current(&native.ui);
    CHECK(snapshot->generation == 2);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE);
    CHECK(node != 0);
    CHECK(strcmp(snapshot->strings + node->primary_off, "mod.hello") == 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(model.view == CANOPUS_MANAGER_VIEW_MODULE_DETAIL);

    snapshot = canopus_ui_current(&native.ui);
    CHECK(snapshot->generation == 3);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_DISABLE) != 0);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_REMOVE) != 0);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_ROLLBACK) != 0);
}

TEST(manager_native_dispatches_real_model_operations)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;

    native_transport_calls = 0;
    native_transport_command = 0;
    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    add_native_module(&model, CANOPUS_LIFECYCLE_REMOVABLE);
    model.view = CANOPUS_MANAGER_VIEW_MODULE_DETAIL;
    model.selected = 0;
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_DISABLE);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 0);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 1);
    CHECK(native_transport_command == CANOPUS_CMD_DISABLE);
    CHECK(model.pending_op == CANOPUS_CMD_DISABLE);
    CHECK(backend.applies == 3);
}

TEST(manager_native_stage_token_is_bounded_and_installable)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;

    native_transport_calls = 0;
    native_transport_payload[0] = '\0';
    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    CHECK(canopus_manager_native_set_stage_token(&native, "../escape") ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_manager_native_set_stage_token(&native, ".") ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_manager_native_set_stage_token(&native, "pkg-001") ==
          CANOPUS_UI_OK);
    CHECK(canopus_manager_native_render(&native) == CANOPUS_UI_OK);

    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_INSTALL);
    CHECK(node != 0);
    CHECK((node->flags & CANOPUS_UI_NODE_FLAG_ENABLED) != 0u);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 0);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 1);
    CHECK(native_transport_command == CANOPUS_CMD_INSTALL);
    CHECK(strcmp(native_transport_payload, "pkg-001") == 0);
}

TEST(manager_native_enable_is_confirmable_and_dispatches)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_module_v1 module;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;

    native_transport_calls = 0;
    native_transport_command = 0;
    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    canopus_memset(&module, 0, sizeof(module));
    canopus_buf_copy(module.module_id, sizeof(module.module_id), "mod.hello");
    module.lifecycle_class = CANOPUS_LIFECYCLE_REMOVABLE;
    module.state = CANOPUS_STATE_INSTALLED; /* disabled by default */
    module.version = 7;
    module.signature_ok = 1;
    CHECK(canopus_manager_upsert_module(&model, &module) == 0);
    model.view = CANOPUS_MANAGER_VIEW_MODULE_DETAIL;
    model.selected = 0;
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);

    /* an INSTALLED (disabled) module offers Enable, never a fake disable */
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_ENABLE);
    CHECK(node != 0);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_DISABLE) == 0);

    /* Enable is next-boot: it goes through confirmation, then dispatches */
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 0);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_calls == 1);
    CHECK(native_transport_command == CANOPUS_CMD_ENABLE);
}

struct native_refresh_sink {
    uint32_t calls;
};

static int32_t native_refresh_sink_fn(void *cookie)
{
    struct native_refresh_sink *sink = (struct native_refresh_sink *)cookie;
    sink->calls++;
    return CANOPUS_UI_OK;
}

TEST(manager_native_refreshes_model_after_operation)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_module_v1 module;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    struct native_refresh_sink sink;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *node;

    native_transport_calls = 0;
    canopus_memset(&backend, 0, sizeof(backend));
    canopus_memset(&sink, 0, sizeof(sink));
    canopus_manager_init(&model, native_transport, 0);
    canopus_memset(&module, 0, sizeof(module));
    canopus_buf_copy(module.module_id, sizeof(module.module_id), "mod.hello");
    module.lifecycle_class = CANOPUS_LIFECYCLE_REMOVABLE;
    module.state = CANOPUS_STATE_ACTIVE;
    module.version = 7;
    module.signature_ok = 1;
    CHECK(canopus_manager_upsert_module(&model, &module) == 0);
    model.view = CANOPUS_MANAGER_VIEW_MODULE_DETAIL;
    model.selected = 0;
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    canopus_manager_native_set_refresh(&native, native_refresh_sink_fn, &sink);

    /* disable -> confirm -> the model is re-read before the re-render, so
     * the page reflects the committed disabled-next-boot state */
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_DISABLE);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    node = find_event(snapshot, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(node != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    node->key, node->event_id) == CANOPUS_UI_OK);
    CHECK(native_transport_command == CANOPUS_CMD_DISABLE);
    CHECK(sink.calls == 1); /* refresh ran after the committed operation */
}

struct native_route_sink {
    uint32_t last_route;
    uint32_t calls;
};

static int32_t native_route_sink_fn(void *cookie,
                                    struct canopus_manager_native_v1 *native,
                                    uint32_t route)
{
    struct native_route_sink *sink = (struct native_route_sink *)cookie;
    (void)native;
    sink->calls++;
    sink->last_route = route;
    return CANOPUS_UI_OK;
}

TEST(manager_native_hides_safe_mode_and_enforces_it)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *install;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    model.safe_mode = 1u;
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    CHECK(canopus_manager_native_set_stage_token(&native, "pkg-001") ==
          CANOPUS_UI_OK);
    CHECK(canopus_manager_native_render(&native) == CANOPUS_UI_OK);

    snapshot = canopus_ui_current(&native.ui);
    CHECK(find_event(snapshot, CANOPUS_MANAGER_EVENT_SAFE_MODE) == 0);
    CHECK(find_primary(snapshot, "Safe mode") == 0);
    install = find_event(snapshot, CANOPUS_MANAGER_EVENT_INSTALL);
    CHECK(install != 0);
    CHECK((install->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u);
}

TEST(manager_native_router_receives_navigation_routes)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    struct native_route_sink sink;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *modules;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_memset(&sink, 0, sizeof(sink));
    canopus_manager_init(&model, native_transport, 0);
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    canopus_manager_native_set_router(&native, native_route_sink_fn, &sink);
    snapshot = canopus_ui_current(&native.ui);
    modules = find_event(snapshot, CANOPUS_MANAGER_EVENT_SHOW_MODULES);
    CHECK(modules != 0);
    CHECK(canopus_ui_dispatch_event(&native.ui, snapshot->generation,
                                    modules->key, modules->event_id) ==
          CANOPUS_UI_OK);
    CHECK(sink.calls == 1);
    CHECK(sink.last_route == CANOPUS_MANAGER_ROUTE_MODULES);
    CHECK(model.view == CANOPUS_MANAGER_VIEW_MODULE_LIST);
}

TEST(manager_native_empty_module_list_is_valid)
{
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct manager_native_backend backend;
    const struct canopus_ui_snapshot_v1 *snapshot;
    uint16_t i;
    int found = 0;

    canopus_memset(&backend, 0, sizeof(backend));
    canopus_manager_init(&model, native_transport, 0);
    model.view = CANOPUS_MANAGER_VIEW_MODULE_LIST;
    CHECK(canopus_manager_native_init(&native, &model, &native_backend_api,
                                      &backend) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&native.ui);
    for (i = 0; i < snapshot->node_count; i++) {
        if (snapshot->nodes[i].type == CANOPUS_UI_NODE_TEXT &&
            strcmp(snapshot->strings + snapshot->nodes[i].primary_off,
                   "No external modules installed") == 0) {
            found = 1;
        }
    }
    CHECK(found);
    CHECK(canopus_manager_goto(&model, CANOPUS_MANAGER_VIEW_MODULE_LIST, 0) == 0);
}

static const struct test_registry manager_native_tests[] = {
    { "manager_native_renders_device_prefabs",
      manager_native_renders_device_prefabs_wrapper },
    { "manager_native_overview_surfaces_supervisor_error",
      manager_native_overview_surfaces_supervisor_error_wrapper },
    { "manager_native_navigates_list_and_detail",
      manager_native_navigates_list_and_detail_wrapper },
    { "manager_native_dispatches_real_model_operations",
      manager_native_dispatches_real_model_operations_wrapper },
    { "manager_native_enable_is_confirmable_and_dispatches",
      manager_native_enable_is_confirmable_and_dispatches_wrapper },
    { "manager_native_refreshes_model_after_operation",
      manager_native_refreshes_model_after_operation_wrapper },
    { "manager_native_stage_token_is_bounded_and_installable",
      manager_native_stage_token_is_bounded_and_installable_wrapper },
    { "manager_native_hides_safe_mode_and_enforces_it",
      manager_native_hides_safe_mode_and_enforces_it_wrapper },
    { "manager_native_router_receives_navigation_routes",
      manager_native_router_receives_navigation_routes_wrapper },
    { "manager_native_empty_module_list_is_valid",
      manager_native_empty_module_list_is_valid_wrapper },
};

int run_manager_native_tests(void)
{
    RUN_TESTS(manager_native_tests,
              sizeof(manager_native_tests) / sizeof(manager_native_tests[0]));
}
