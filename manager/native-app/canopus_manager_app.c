/* canopus_manager_app.c — native Manager app lifecycle and descriptor. */
#include "canopus_manager_app.h"
#include "canopus_memory.h"

static struct canopus_manager_app_v1 *manager_app;

static int32_t manager_on_create(const struct canopus_app_context_v1 *context)
{
    int32_t rc;
    (void)context;
    if (manager_app == 0 ||
        manager_app->state != CANOPUS_MANAGER_APP_STATE_CONFIGURED) {
        return -1;
    }
    rc = canopus_client_open(&manager_app->client);
    if (rc != CANOPUS_CLIENT_OK) {
        return rc;
    }
    rc = canopus_manager_native_init(&manager_app->native,
                                     &manager_app->model,
                                     manager_app->ui_backend,
                                     manager_app->ui_backend_cookie);
    if (rc != CANOPUS_UI_OK) {
        (void)canopus_client_close(&manager_app->client);
        return rc;
    }
    manager_app->state = CANOPUS_MANAGER_APP_STATE_CREATED;
    return 0;
}

static int32_t manager_on_resume(const struct canopus_app_context_v1 *context)
{
    int32_t rc;
    (void)context;
    if (manager_app == 0 ||
        (manager_app->state != CANOPUS_MANAGER_APP_STATE_CREATED &&
         manager_app->state != CANOPUS_MANAGER_APP_STATE_PAUSED &&
         manager_app->state != CANOPUS_MANAGER_APP_STATE_RESUMED)) {
        return -1;
    }
    rc = canopus_manager_native_render(&manager_app->native);
    if (rc == CANOPUS_UI_OK) {
        manager_app->state = CANOPUS_MANAGER_APP_STATE_RESUMED;
    }
    return rc;
}

static int32_t manager_on_pause(const struct canopus_app_context_v1 *context)
{
    (void)context;
    if (manager_app == 0 ||
        manager_app->state != CANOPUS_MANAGER_APP_STATE_RESUMED) {
        return -1;
    }
    manager_app->state = CANOPUS_MANAGER_APP_STATE_PAUSED;
    return 0;
}

static int32_t manager_on_destroy(const struct canopus_app_context_v1 *context)
{
    int32_t rc;
    (void)context;
    if (manager_app == 0 ||
        (manager_app->state != CANOPUS_MANAGER_APP_STATE_CREATED &&
         manager_app->state != CANOPUS_MANAGER_APP_STATE_RESUMED &&
         manager_app->state != CANOPUS_MANAGER_APP_STATE_PAUSED)) {
        return -1;
    }
    rc = canopus_client_close(&manager_app->client);
    manager_app->state = CANOPUS_MANAGER_APP_STATE_DESTROYED;
    return rc;
}

static int32_t manager_on_dispatch(const struct canopus_app_context_v1 *context,
                                   uint32_t message, const void *payload,
                                   uint32_t payload_len)
{
    (void)payload;
    (void)payload_len;
    switch (message) {
    case CANOPUS_APP_MSG_CREATE:
        return manager_on_create(context);
    case CANOPUS_APP_MSG_RESUME:
        return manager_on_resume(context);
    case CANOPUS_APP_MSG_PAUSE:
        return manager_on_pause(context);
    case CANOPUS_APP_MSG_DESTROY:
        return manager_on_destroy(context);
    default:
        return -1;
    }
}

static const struct canopus_app_descriptor_v1 descriptor = {
    sizeof(struct canopus_app_descriptor_v1),
    CANOPUS_APP_ABI_MAJOR,
    CANOPUS_APP_ABI_MINOR,
    CANOPUS_APP_FLAG_MANAGER_PAGE | CANOPUS_APP_FLAG_LAUNCHER_VISIBLE,
    "com.canopus.manager",
    "Canopus Manager",
    "canopus/manager",
    manager_on_create,
    manager_on_resume,
    manager_on_pause,
    manager_on_destroy,
    manager_on_dispatch,
};

int32_t canopus_manager_app_configure(
    struct canopus_manager_app_v1 *app,
    const struct canopus_client_io_v1 *client_io,
    void *client_io_cookie,
    const struct canopus_ui_backend_v1 *ui_backend,
    void *ui_backend_cookie)
{
    int32_t rc;
    if (app == 0 || ui_backend == 0 ||
        (manager_app != 0 && manager_app != app &&
         manager_app->state != CANOPUS_MANAGER_APP_STATE_DESTROYED)) {
        return -1;
    }
    canopus_memset(app, 0, sizeof(*app));
    rc = canopus_client_init(&app->client, client_io, client_io_cookie);
    if (rc != CANOPUS_CLIENT_OK) {
        return rc;
    }
    canopus_manager_init(&app->model, canopus_client_transport, &app->client);
    app->ui_backend = ui_backend;
    app->ui_backend_cookie = ui_backend_cookie;
    app->state = CANOPUS_MANAGER_APP_STATE_CONFIGURED;
    manager_app = app;
    return 0;
}

void canopus_manager_app_set_identity(
    struct canopus_manager_app_v1 *app,
    const char *target_id,
    const char *firmware_version,
    const char *firmware_build,
    uint32_t framework_revision)
{
    if (app != 0) {
        canopus_manager_set_identity(&app->model, target_id, firmware_version,
                                     firmware_build, framework_revision);
    }
}

int32_t canopus_manager_app_set_stage_token(
    struct canopus_manager_app_v1 *app, const char *token)
{
    if (app == 0 || app->state < CANOPUS_MANAGER_APP_STATE_CREATED) {
        return CANOPUS_UI_ERR_STATE;
    }
    return canopus_manager_native_set_stage_token(&app->native, token);
}

const struct canopus_app_descriptor_v1 *canopus_manager_app_descriptor(void)
{
    return &descriptor;
}
