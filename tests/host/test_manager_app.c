/* Host test: native Manager app lifecycle through UI and /dev/canopus. */
#include "canopus_test.h"
#include "canopus_manager_app.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include <string.h>

struct manager_app_fixture {
    struct canopus_supervisor_v1 supervisor;
    struct canopus_ui_snapshot_v1 snapshot;
    uint32_t applies;
    uint32_t opens;
    uint32_t closes;
};

static int32_t app_device_open(void *cookie, const char *path)
{
    struct manager_app_fixture *fixture = (struct manager_app_fixture *)cookie;
    (void)path;
    fixture->opens++;
    return 9;
}

static int32_t app_device_close(void *cookie, int32_t fd)
{
    struct manager_app_fixture *fixture = (struct manager_app_fixture *)cookie;
    fixture->closes++;
    return fd == 9 ? 0 : -1;
}

static int32_t app_device_read(void *cookie, int32_t fd, void *buffer,
                               uint32_t count)
{
    struct manager_app_fixture *fixture = (struct manager_app_fixture *)cookie;
    if (fd != 9) return -1;
    return canopus_supervisor_device_read(&fixture->supervisor, buffer, count);
}

static int32_t app_device_write(void *cookie, int32_t fd, const void *buffer,
                                uint32_t count)
{
    struct manager_app_fixture *fixture = (struct manager_app_fixture *)cookie;
    if (fd != 9) return -1;
    return canopus_supervisor_device_write(&fixture->supervisor, buffer, count);
}

static int32_t app_ui_apply(void *cookie,
                            const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct manager_app_fixture *fixture = (struct manager_app_fixture *)cookie;
    fixture->snapshot = *snapshot;
    fixture->applies++;
    return 0;
}

static const struct canopus_client_io_v1 app_device_io = {
    sizeof(struct canopus_client_io_v1),
    CANOPUS_CLIENT_ABI_MAJOR,
    CANOPUS_CLIENT_ABI_MINOR,
    app_device_open,
    app_device_close,
    app_device_read,
    app_device_write,
};

static const struct canopus_ui_backend_v1 app_ui_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    CANOPUS_UI_CAP_EXTENDED_COMPONENTS | CANOPUS_UI_CAP_STYLE |
        CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VALUES |
        CANOPUS_UI_CAP_NAVIGATION_HEADER,
    app_ui_apply,
    0,
};

static int32_t dispatch_activation(struct canopus_ui_context_v1 *ui,
                                   uint32_t generation, uint32_t key,
                                   uint32_t event_id)
{
    struct canopus_ui_event_v1 event = {0};
    event.struct_size = sizeof(event);
    event.generation = generation;
    event.key = key;
    event.event_id = event_id;
    event.type = CANOPUS_UI_EVENT_ACTIVATE;
    return canopus_ui_dispatch_event(ui, &event);
}

static const struct canopus_ui_node_v1 *app_find_event(
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

TEST(manager_app_runs_native_end_to_end_path)
{
    struct manager_app_fixture fixture;
    struct canopus_manager_app_v1 app;
    const struct canopus_app_descriptor_v1 *descriptor;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *modules;

    canopus_memset(&fixture, 0, sizeof(fixture));
    CHECK(canopus_supervisor_init(&fixture.supervisor, 12, 0, 0) == 0);
    descriptor = canopus_manager_app_descriptor();
    CHECK(canopus_app_descriptor_check(descriptor) == 0);
    CHECK((descriptor->flags & CANOPUS_APP_FLAG_LAUNCHER_VISIBLE) != 0u);
    CHECK(strcmp((const char *)descriptor->app_name, "Canopus 管理器") == 0);
    CHECK(canopus_manager_app_configure(&app, &app_device_io, &fixture,
                                        &app_ui_backend, &fixture) == 0);
    canopus_manager_app_set_identity(&app, "xiaomi-band-10-pro-3.101.030",
                                     "3.101.030", "CONBINE_LTALM078", 12);

    CHECK(descriptor->on_create(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_CREATED);
    CHECK(fixture.opens == 1);
    CHECK(fixture.applies == 1);
    CHECK(descriptor->on_resume(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_RESUMED);

    snapshot = canopus_ui_current(&app.native.ui);
    CHECK(app_find_event(snapshot, CANOPUS_MANAGER_EVENT_SAFE_MODE) == 0);
    CHECK(app_find_event(snapshot, CANOPUS_MANAGER_EVENT_EXIT_SAFE_KEEP) == 0);
    CHECK(app_find_event(snapshot, CANOPUS_MANAGER_EVENT_EXIT_SAFE_DISABLE) == 0);
    modules = app_find_event(snapshot, CANOPUS_MANAGER_EVENT_SHOW_MODULES);
    CHECK(modules != 0);
    CHECK(dispatch_activation(&app.native.ui, snapshot->generation,
                                    modules->key, modules->event_id) == 0);
    CHECK(app.model.view == CANOPUS_MANAGER_VIEW_MODULE_LIST);
    snapshot = canopus_ui_current(&app.native.ui);
    CHECK(snapshot->generation == 3u);
    CHECK(strcmp(snapshot->strings + snapshot->nodes[0].primary_off,
                 "模块") == 0);
    CHECK(fixture.supervisor.safe_mode == 0u);

    CHECK(descriptor->on_pause(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_PAUSED);
    CHECK(descriptor->on_destroy(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_DESTROYED);
    CHECK(fixture.closes == 1);
}

static int safe_persist(void *cookie, const uint8_t *data, uint32_t size)
{
    (void)cookie; (void)data; (void)size;
    return 0;
}

static const struct canopus_sup_platform_v1 safe_platform = {
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .persist = safe_persist,
};

TEST(manager_app_offers_the_way_out_of_safe_mode)
{
    struct manager_app_fixture fixture;
    struct canopus_manager_app_v1 app;
    const struct canopus_app_descriptor_v1 *descriptor;
    const struct canopus_ui_snapshot_v1 *snapshot;
    const struct canopus_ui_node_v1 *leave;
    const struct canopus_ui_node_v1 *confirm;

    canopus_memset(&fixture, 0, sizeof(fixture));
    CHECK(canopus_supervisor_init(&fixture.supervisor, 12, &safe_platform, 0) == 0);
    fixture.supervisor.safe_mode = 1u;
    fixture.supervisor.safe_mode_reason = CANOPUS_SAFE_MODE_UNFINISHED_BOOT;
    descriptor = canopus_manager_app_descriptor();
    CHECK(canopus_manager_app_configure(&app, &app_device_io, &fixture,
                                        &app_ui_backend, &fixture) == 0);
    canopus_manager_app_set_identity(&app, "xiaomi-band-10-pro-3.101.030",
                                     "3.101.030", "CONBINE_LTALM078", 12);
    app.model.safe_mode = 1u; /* the target backend copies it from QUERY_DEVICE */
    CHECK(descriptor->on_create(0) == 0);
    CHECK(descriptor->on_resume(0) == 0);

    snapshot = canopus_ui_current(&app.native.ui);
    CHECK(app_find_event(snapshot, CANOPUS_MANAGER_EVENT_EXIT_SAFE_KEEP) != 0);
    leave = app_find_event(snapshot, CANOPUS_MANAGER_EVENT_EXIT_SAFE_DISABLE);
    CHECK(leave != 0);
    /* installing is not offered while the device is in safe mode */
    CHECK(canopus_manager_op_install(&app.model, "x") == CANOPUS_RESULT_DISALLOWED);

    /* choosing the action asks first, and only the confirmation sends it */
    CHECK(dispatch_activation(&app.native.ui, snapshot->generation,
                                    leave->key, leave->event_id) == 0);
    CHECK(fixture.supervisor.exit_pending == 0u);
    snapshot = canopus_ui_current(&app.native.ui);
    confirm = app_find_event(snapshot, CANOPUS_MANAGER_EVENT_CONFIRM);
    CHECK(confirm != 0);
    CHECK(dispatch_activation(&app.native.ui, snapshot->generation,
                                    confirm->key, confirm->event_id) == 0);
    CHECK(fixture.supervisor.exit_pending == 1u);
    CHECK(fixture.supervisor.safe_mode == 1u); /* this run stays safe */
    CHECK(descriptor->on_destroy(0) == 0);
}

static const struct test_registry manager_app_tests[] = {
    { "manager_app_runs_native_end_to_end_path",
      manager_app_runs_native_end_to_end_path_wrapper },
    { "manager_app_offers_the_way_out_of_safe_mode",
      manager_app_offers_the_way_out_of_safe_mode_wrapper },
};

int run_manager_app_tests(void)
{
    RUN_TESTS(manager_app_tests,
              sizeof(manager_app_tests) / sizeof(manager_app_tests[0]));
}
