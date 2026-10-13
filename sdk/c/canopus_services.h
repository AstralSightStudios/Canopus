/*
 * canopus_services.h — Supervisor service table (ADR-CAN-006, K7).
 *
 * Generic capabilities are implemented once per firmware inside the
 * Supervisor and handed to modules through `canopus_context_v1`. A module
 * that only uses this header contains no firmware address and is portable
 * across every firmware whose Supervisor implements the groups it needs.
 *
 * The context is per module. Every service takes the context it was given,
 * which is how the Supervisor knows the caller (private file namespace, log
 * attribution). Modules must not copy or retain the context beyond the
 * module's lifetime.
 */
#ifndef CANOPUS_SERVICES_H
#define CANOPUS_SERVICES_H

#include <stdint.h>
#include "canopus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Service groups. Each has an independent version; a zero version means the
 * group is absent on this Supervisor. */
enum canopus_service_group {
    CANOPUS_SVC_MODULE = 0, /* descriptor registration */
    CANOPUS_SVC_LOG    = 1,
    CANOPUS_SVC_FS     = 2, /* module-private files */
    CANOPUS_SVC_GROUP_COUNT
};

/* Return values: 0 or a non-negative count on success, negative on failure. */
#define CANOPUS_SVC_OK            0
#define CANOPUS_SVC_ERR_ARGUMENT  (-22)
#define CANOPUS_SVC_ERR_UNSUPPORTED (-38)
#define CANOPUS_SVC_ERR_IO        (-5)
#define CANOPUS_SVC_ERR_STATE     (-16)

/* Log levels. */
#define CANOPUS_LOG_ERROR 0u
#define CANOPUS_LOG_WARN  1u
#define CANOPUS_LOG_INFO  2u
#define CANOPUS_LOG_DEBUG 3u
#define CANOPUS_LOG_MESSAGE_MAX 80u /* longer messages are truncated */

/* fs open flags. Files live in a flat namespace private to the calling
 * module; names are [a-z0-9_.-]{1,31} (no separators). */
#define CANOPUS_FS_READ   1u
#define CANOPUS_FS_WRITE  2u /* create or truncate */
#define CANOPUS_FS_NAME_MAX 31u

struct canopus_context_v1;

struct canopus_services_v1 {
    uint32_t struct_size;
    uint8_t group_version[CANOPUS_SVC_GROUP_COUNT]; /* 0 = absent */

    /* MODULE v1: publish the module's descriptor. Only valid while the
     * Supervisor is loading this module (from the module's init entry). */
    int32_t (*module_register)(const struct canopus_context_v1 *ctx,
                               const struct canopus_module_descriptor_v1 *descriptor);

    /* LOG v1: NUL-terminated message, truncated to CANOPUS_LOG_MESSAGE_MAX. */
    void (*log_write)(const struct canopus_context_v1 *ctx, uint32_t level,
                      const char *message);

    /* FS v1: open returns a small non-negative handle or a negative error.
     * At most CANOPUS_FS_MAX_OPEN handles per module are open at once. */
    int32_t (*fs_open)(const struct canopus_context_v1 *ctx, const char *name,
                       uint32_t flags);
    int32_t (*fs_read)(const struct canopus_context_v1 *ctx, int32_t handle,
                       void *buffer, uint32_t size);
    int32_t (*fs_write)(const struct canopus_context_v1 *ctx, int32_t handle,
                        const void *buffer, uint32_t size);
    int32_t (*fs_close)(const struct canopus_context_v1 *ctx, int32_t handle);
    int32_t (*fs_remove)(const struct canopus_context_v1 *ctx, const char *name);
};

#define CANOPUS_FS_MAX_OPEN 4u

struct canopus_context_v1 {
    uint32_t struct_size;
    uint32_t abi; /* (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR */
    const struct canopus_services_v1 *services;
    uint32_t slot; /* Supervisor-internal; modules must not interpret it */
};

/* Non-zero when the group is available at least at `min_version`. */
static inline int canopus_services_has(const struct canopus_context_v1 *ctx,
                                       uint32_t group, uint32_t min_version)
{
    return ctx != 0 && ctx->services != 0 &&
           ctx->struct_size >= sizeof(struct canopus_context_v1) &&
           group < CANOPUS_SVC_GROUP_COUNT &&
           ctx->services->group_version[group] >= min_version &&
           ctx->services->group_version[group] != 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_SERVICES_H */
