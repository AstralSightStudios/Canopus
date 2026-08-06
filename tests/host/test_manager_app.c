/* Host test: native Manager app lifecycle through UI and /dev/canopus. */
#include "canopus_test.h"
#include "canopus_manager_app.h"
#include "canopus_supervisor.h"
#include "canopus_memory.h"

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
    app_ui_apply,
};

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
    const struct canopus_ui_node_v1 *safe_mode;

    canopus_memset(&fixture, 0, sizeof(fixture));
    CHECK(canopus_supervisor_init(&fixture.supervisor, 12, 0, 0) == 0);
    descriptor = canopus_manager_app_descriptor();
    CHECK(canopus_app_descriptor_check(descriptor) == 0);
    CHECK((descriptor->flags & CANOPUS_APP_FLAG_LAUNCHER_VISIBLE) != 0u);
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
    safe_mode = app_find_event(snapshot, CANOPUS_MANAGER_EVENT_SAFE_MODE);
    CHECK(safe_mode != 0);
    CHECK((safe_mode->flags & CANOPUS_UI_NODE_FLAG_CHECKED) == 0u);
    CHECK((safe_mode->flags & CANOPUS_UI_NODE_FLAG_ENABLED) != 0u);
    /* The stock switch toggles directly without an intermediate confirmation. */
    CHECK(canopus_ui_dispatch_event(&app.native.ui, snapshot->generation,
                                    safe_mode->key, safe_mode->event_id) == 0);
    CHECK(fixture.supervisor.safe_mode == 1);
    CHECK(app.model.safe_mode == 1);
    CHECK(app.model.pending_op == CANOPUS_CMD_ENTER_SAFE_MODE);
    /* Once active the switch renders checked and disabled: it cannot pretend
     * that an active safe mode is an instant exit. */
    snapshot = canopus_ui_current(&app.native.ui);
    safe_mode = app_find_event(snapshot, CANOPUS_MANAGER_EVENT_SAFE_MODE);
    CHECK(safe_mode != 0);
    CHECK((safe_mode->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u);
    CHECK((safe_mode->flags & CANOPUS_UI_NODE_FLAG_ENABLED) == 0u);

    CHECK(descriptor->on_pause(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_PAUSED);
    CHECK(descriptor->on_destroy(0) == 0);
    CHECK(app.state == CANOPUS_MANAGER_APP_STATE_DESTROYED);
    CHECK(fixture.closes == 1);
}

static const struct test_registry manager_app_tests[] = {
    { "manager_app_runs_native_end_to_end_path",
      manager_app_runs_native_end_to_end_path_wrapper },
};

int run_manager_app_tests(void)
{
    RUN_TESTS(manager_app_tests,
              sizeof(manager_app_tests) / sizeof(manager_app_tests[0]));
}
