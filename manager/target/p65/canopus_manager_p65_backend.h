/* Host-testable P65 Manager controller core; no firmware addresses. */
#ifndef CANOPUS_MANAGER_P65_BACKEND_H
#define CANOPUS_MANAGER_P65_BACKEND_H

#include <stdint.h>
#include "canopus_client.h"
#include "canopus_manager_native.h"
#include "canopus_manager_p65_lvx.h"

#ifdef __cplusplus
extern "C" {
#endif

struct canopus_supervisor_v1;

struct canopus_manager_p65_backend_v1 {
    struct canopus_supervisor_v1 *supervisor;
    struct canopus_client_v1 client;
    struct canopus_manager_model_v1 model;
    struct canopus_manager_native_v1 native;
    struct canopus_ui_backend_v1 ui_backend;
    struct canopus_manager_p65_lvx_list_v1 lvx_list;
    uint32_t next_query_request_id;
    uint8_t client_open;
    uint8_t initialized;
};

/* Initialize an in-process Manager controller after a caller-owned page root
 * exists. The LVX operations are injected; this function performs no P65
 * firmware call or app/page registration. */
int32_t canopus_manager_p65_backend_init(
    struct canopus_manager_p65_backend_v1 *backend,
    struct canopus_supervisor_v1 *supervisor,
    const struct canopus_p65_lvx_list_ops_v1 *lvx_ops,
    void *lvx_cookie, void *page_root, const void *list_descriptor,
    const char *target_id, const char *firmware_version,
    const char *firmware_build, uint32_t framework_revision);

int32_t canopus_manager_p65_backend_refresh(void *cookie);
int32_t canopus_manager_p65_backend_close(
    struct canopus_manager_p65_backend_v1 *backend);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_BACKEND_H */
