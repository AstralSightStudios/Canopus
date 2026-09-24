/* P65 Manager controller core using injected LVX operations and local CPC2. */
#include "canopus_manager_p65_backend.h"
#include "canopus_manager_p65_transport.h"
#include "canopus_supervisor.h"
#include "canopus_memory.h"

#define CANOPUS_P65_MODULE_FLAG_SIGNATURE_OK (1u << 0)
#define CANOPUS_P65_QUERY_REQUEST_BASE 0x80000000u

static int32_t p65_ui_apply(void *cookie,
                            const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_manager_p65_backend_v1 *backend =
        (struct canopus_manager_p65_backend_v1 *)cookie;
    if (backend == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    return canopus_manager_p65_lvx_list_apply(&backend->lvx_list, snapshot);
}

static uint32_t p65_next_query_request_id(
    struct canopus_manager_p65_backend_v1 *backend)
{
    uint32_t id = backend->next_query_request_id++;
    if (id == 0u) {
        id = backend->next_query_request_id++;
    }
    return id;
}

static int p65_module_id_equal(const char a[CANOPUS_MANAGER_MODULE_ID_MAX],
                               const char b[CANOPUS_MANAGER_MODULE_ID_MAX])
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_MANAGER_MODULE_ID_MAX; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

int32_t canopus_manager_p65_backend_refresh(void *cookie)
{
    struct canopus_manager_p65_backend_v1 *backend =
        (struct canopus_manager_p65_backend_v1 *)cookie;
    struct canopus_client_device_snapshot_v1 device;
    struct canopus_client_module_snapshot_v1 query_module;
    struct canopus_manager_module_v1 modules[CANOPUS_MANAGER_MAX_MODULES];
    char selected_id[CANOPUS_MANAGER_MODULE_ID_MAX];
    uint32_t selected = 0u;
    uint32_t module_count = 0u;
    uint32_t slot;
    int32_t rc;

    if (backend == 0 || !backend->client_open) {
        return CANOPUS_UI_ERR_STATE;
    }
    canopus_memset(selected_id, 0, sizeof(selected_id));
    if (backend->model.selected < backend->model.module_count) {
        canopus_memcpy(selected_id,
                       backend->model.modules[backend->model.selected].module_id,
                       sizeof(selected_id));
    }
    canopus_memset(modules, 0, sizeof(modules));
    rc = canopus_client_query_device(
        &backend->client, p65_next_query_request_id(backend), &device);
    if (rc != CANOPUS_CLIENT_OK) {
        backend->model.error_code = rc;
        return CANOPUS_UI_ERR_BACKEND;
    }

    backend->model.framework_revision = device.framework_revision;
    backend->model.safe_mode = device.safe_mode;
    backend->model.error_code = device.error_code;
    backend->model.supervisor_flags = device.flags;
    backend->model.reported_module_count = device.module_count;
    backend->model.module_query_error = 0;
    backend->model.module_query_error_slot = 0u;

    for (slot = 0u; slot < CANOPUS_SUP_MODULE_SLOTS; slot++) {
        struct canopus_manager_module_v1 *module;
        rc = canopus_client_query_module(
            &backend->client, p65_next_query_request_id(backend), slot,
            &query_module);
        if (rc == CANOPUS_CLIENT_ERR_NOT_FOUND) {
            continue;
        }
        if (rc != CANOPUS_CLIENT_OK) {
            if (backend->model.module_query_error == 0) {
                backend->model.module_query_error = rc;
                backend->model.module_query_error_slot = slot;
            }
            continue;
        }
        if (module_count >= CANOPUS_MANAGER_MAX_MODULES) {
            backend->model.module_query_error = CANOPUS_CLIENT_ERR_PROTOCOL;
            backend->model.module_query_error_slot = slot;
            break;
        }

        module = &modules[module_count];
        canopus_memcpy(module->module_id, query_module.module_id,
                       sizeof(module->module_id));
        module->state = query_module.state;
        module->lifecycle_class = query_module.lifecycle_class;
        module->version = query_module.version;
        module->flags = query_module.flags;
        module->signature_ok =
            (query_module.flags & CANOPUS_P65_MODULE_FLAG_SIGNATURE_OK) != 0u;
        module->risk =
            query_module.lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE
                ? (module->signature_ok ? CANOPUS_MANAGER_RISK_HARMLESS
                                        : CANOPUS_MANAGER_RISK_MODERATE)
                : CANOPUS_MANAGER_RISK_RESIDENT_CRITICAL;
        module->has_previous = 0u;
        module->activation_error = query_module.activation_error;
        module_count++;
    }

    canopus_memset(backend->model.modules, 0,
                   sizeof(backend->model.modules));
    if (module_count != 0u) {
        canopus_memcpy(backend->model.modules, modules,
                       module_count * sizeof(modules[0]));
    }
    backend->model.module_count = module_count;
    if (selected_id[0] != '\0') {
        for (slot = 0u; slot < module_count; slot++) {
            if (p65_module_id_equal(selected_id,
                                    backend->model.modules[slot].module_id)) {
                selected = slot;
                break;
            }
        }
    }
    backend->model.selected = selected;
    return CANOPUS_UI_OK;
}

int32_t canopus_manager_p65_backend_init(
    struct canopus_manager_p65_backend_v1 *backend,
    struct canopus_supervisor_v1 *supervisor,
    const struct canopus_p65_lvx_list_ops_v1 *lvx_ops,
    void *lvx_cookie, void *page_root, const void *list_descriptor,
    const char *target_id, const char *firmware_version,
    const char *firmware_build, uint32_t framework_revision)
{
    int32_t rc;

    if (backend == 0 || supervisor == 0 || target_id == 0 ||
        firmware_version == 0 || firmware_build == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    canopus_memset(backend, 0, sizeof(*backend));
    backend->supervisor = supervisor;
    backend->next_query_request_id = CANOPUS_P65_QUERY_REQUEST_BASE;
    rc = canopus_client_init(&backend->client,
                             canopus_manager_p65_local_client_io(),
                             supervisor);
    if (rc != CANOPUS_CLIENT_OK) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    rc = canopus_client_open(&backend->client);
    if (rc != CANOPUS_CLIENT_OK) {
        return CANOPUS_UI_ERR_BACKEND;
    }
    backend->client_open = 1u;
    canopus_manager_init(&backend->model, canopus_client_transport,
                         &backend->client);
    canopus_manager_set_identity(&backend->model, target_id,
                                 firmware_version, firmware_build,
                                 framework_revision);
    rc = canopus_manager_p65_backend_refresh(backend);
    if (rc != CANOPUS_UI_OK) {
        (void)canopus_manager_p65_backend_close(backend);
        return rc;
    }
    rc = canopus_manager_p65_lvx_list_init(
        &backend->lvx_list, &backend->native.ui, lvx_ops, lvx_cookie,
        page_root, list_descriptor);
    if (rc != CANOPUS_UI_OK) {
        (void)canopus_manager_p65_backend_close(backend);
        return rc;
    }
    backend->ui_backend.struct_size = sizeof(backend->ui_backend);
    backend->ui_backend.abi_major = CANOPUS_UI_ABI_MAJOR;
    backend->ui_backend.abi_minor = CANOPUS_UI_ABI_MINOR;
    backend->ui_backend.apply = p65_ui_apply;
    rc = canopus_manager_native_init(&backend->native, &backend->model,
                                     &backend->ui_backend, backend);
    if (rc != CANOPUS_UI_OK) {
        (void)canopus_manager_p65_backend_close(backend);
        return rc;
    }
    canopus_manager_native_set_refresh(&backend->native,
                                       canopus_manager_p65_backend_refresh,
                                       backend);
    backend->initialized = 1u;
    return CANOPUS_UI_OK;
}

int32_t canopus_manager_p65_backend_close(
    struct canopus_manager_p65_backend_v1 *backend)
{
    if (backend == 0) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (backend->client_open) {
        if (canopus_client_close(&backend->client) != CANOPUS_CLIENT_OK) {
            return CANOPUS_UI_ERR_BACKEND;
        }
        backend->client_open = 0u;
    }
    backend->initialized = 0u;
    /* LVX page/list teardown is owned by the firmware page manager and must
     * not be freed here until P65 callback-drain ordering is proven. */
    return CANOPUS_UI_OK;
}
