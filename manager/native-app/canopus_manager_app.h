/*
 * canopus_manager_app.h — resident native Manager application lifecycle.
 */
#ifndef CANOPUS_MANAGER_APP_H
#define CANOPUS_MANAGER_APP_H

#include <stdint.h>
#include "canopus_app.h"
#include "canopus_client.h"
#include "canopus_manager_native.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_MANAGER_APP_STATE_CONFIGURED 1u
#define CANOPUS_MANAGER_APP_STATE_CREATED    2u
#define CANOPUS_MANAGER_APP_STATE_RESUMED    3u
#define CANOPUS_MANAGER_APP_STATE_PAUSED     4u
#define CANOPUS_MANAGER_APP_STATE_DESTROYED  5u

struct canopus_manager_app_v1 {
    struct canopus_manager_model_v1 model;
    struct canopus_client_v1 client;
    struct canopus_manager_native_v1 native;
    const struct canopus_ui_backend_v1 *ui_backend;
    void *ui_backend_cookie;
    uint32_t state;
};

/* Configures the single resident Manager instance used by the descriptor
 * callbacks. Registration remains target-private and must happen afterward. */
int32_t canopus_manager_app_configure(
    struct canopus_manager_app_v1 *app,
    const struct canopus_client_io_v1 *client_io,
    void *client_io_cookie,
    const struct canopus_ui_backend_v1 *ui_backend,
    void *ui_backend_cookie);

void canopus_manager_app_set_identity(
    struct canopus_manager_app_v1 *app,
    const char *target_id,
    const char *firmware_version,
    const char *firmware_build,
    uint32_t framework_revision);

int32_t canopus_manager_app_set_stage_token(
    struct canopus_manager_app_v1 *app, const char *token);

const struct canopus_app_descriptor_v1 *canopus_manager_app_descriptor(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_APP_H */
