/*
 * canopus_manager_native.h — semantic native UI controller for Manager.
 */
#ifndef CANOPUS_MANAGER_NATIVE_H
#define CANOPUS_MANAGER_NATIVE_H

#include <stdint.h>
#include "canopus_manager.h"
#include "canopus_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_MANAGER_STAGE_TOKEN_MAX 64u

struct canopus_manager_native_v1;

enum canopus_manager_native_route {
    CANOPUS_MANAGER_ROUTE_OVERVIEW = 1,
    CANOPUS_MANAGER_ROUTE_MODULES,
    CANOPUS_MANAGER_ROUTE_MODULE_DETAIL,
    CANOPUS_MANAGER_ROUTE_CONFIRMATION,
};

typedef int32_t (*canopus_manager_native_route_v1)(
    void *cookie, struct canopus_manager_native_v1 *native,
    uint32_t route);

enum canopus_manager_native_event {
    CANOPUS_MANAGER_EVENT_SHOW_DEVICE = 1,
    CANOPUS_MANAGER_EVENT_SHOW_MODULES,
    CANOPUS_MANAGER_EVENT_INSTALL,
    CANOPUS_MANAGER_EVENT_SAFE_MODE,
    CANOPUS_MANAGER_EVENT_ENABLE,
    CANOPUS_MANAGER_EVENT_DISABLE,
    CANOPUS_MANAGER_EVENT_UPDATE,
    CANOPUS_MANAGER_EVENT_ROLLBACK,
    CANOPUS_MANAGER_EVENT_REMOVE,
    CANOPUS_MANAGER_EVENT_CONFIRM,
    CANOPUS_MANAGER_EVENT_CANCEL,
    CANOPUS_MANAGER_EVENT_OPEN_MODULE_BASE = 0x100u,
};

struct canopus_manager_native_v1 {
    struct canopus_manager_model_v1 *model;
    struct canopus_ui_context_v1 ui;
    char stage_token[CANOPUS_MANAGER_STAGE_TOKEN_MAX];
    uint32_t confirm_event;
    uint32_t confirm_return_view;
    canopus_manager_native_route_v1 route;
    void *route_cookie;
};

int32_t canopus_manager_native_init(
    struct canopus_manager_native_v1 *native,
    struct canopus_manager_model_v1 *model,
    const struct canopus_ui_backend_v1 *backend,
    void *backend_cookie);

/* Sets the already-verified bounded staging token used by the Install button.
 * NULL clears the token. Arbitrary filesystem paths are not accepted here. */
int32_t canopus_manager_native_set_stage_token(
    struct canopus_manager_native_v1 *native, const char *token);

void canopus_manager_native_set_router(
    struct canopus_manager_native_v1 *native,
    canopus_manager_native_route_v1 route,
    void *route_cookie);

int32_t canopus_manager_native_render(struct canopus_manager_native_v1 *native);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_NATIVE_H */
