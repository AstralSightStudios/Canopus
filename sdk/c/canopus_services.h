/*
 * canopus_services.h — Supervisor service table and Object system (ADR-CAN-006).
 *
 * Generic capabilities are implemented once per firmware inside the
 * Supervisor and handed to modules through `canopus_context_v1`. A module
 * that uses this header contains no firmware addresses and is portable
 * across every firmware whose Supervisor implements the required groups.
 *
 * All services are organized into distinct, domain-specific Service Objects:
 *   ctx->fs     : File system and directory operations
 *   ctx->mem    : 128 KiB heap memory management and quota tracking
 *   ctx->time   : Monotonic millisecond clock and delay
 *   ctx->hook   : Universal dynamic function hook engine (FPB / Inline / Slot)
 *   ctx->log    : Categorized structured logging
 *   ctx->notify : User-visible notifications
 *   ctx->system : Device model, firmware identity, and power
 *   ctx->bt     : Layer 2 transport and Bluetooth control
 *
 * Legacy v1 service calls via `ctx->services->*` remain fully supported.
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
    CANOPUS_SVC_MODULE    = 0, /* descriptor registration */
    CANOPUS_SVC_LOG       = 1, /* logging */
    CANOPUS_SVC_FS        = 2, /* files and directories */
    CANOPUS_SVC_TIME      = 3, /* monotonic clock */
    CANOPUS_SVC_MEM       = 4, /* budgeted heap (128 KiB) */
    CANOPUS_SVC_NOTIFY    = 5, /* user notifications */
    CANOPUS_SVC_HOOK      = 6, /* universal function hook engine */
    CANOPUS_SVC_SYSTEM    = 7, /* system and hardware status */
    CANOPUS_SVC_BLUETOOTH = 8, /* bluetooth transport and L2 */
    CANOPUS_SVC_GROUP_COUNT
};

/* Return values: 0 or a non-negative count on success, negative on failure. */
#define CANOPUS_SVC_OK              0
#define CANOPUS_SVC_ERR_ARGUMENT    (-22)
#define CANOPUS_SVC_ERR_UNSUPPORTED (-38)
#define CANOPUS_SVC_ERR_IO          (-5)
#define CANOPUS_SVC_ERR_STATE       (-16)
#define CANOPUS_SVC_ERR_NOMEM       (-12)
#define CANOPUS_SVC_ERR_NOTFOUND    (-2)
#define CANOPUS_SVC_ERR_BUSY        (-16)

/* -------------------------------------------------------------------------- */
/* LOG                                                                        */
/* -------------------------------------------------------------------------- */

#define CANOPUS_LOG_ERROR 0u
#define CANOPUS_LOG_WARN  1u
#define CANOPUS_LOG_INFO  2u
#define CANOPUS_LOG_DEBUG 3u
#define CANOPUS_LOG_MESSAGE_MAX 80u

/* -------------------------------------------------------------------------- */
/* FS (File System & Directories)                                             */
/* -------------------------------------------------------------------------- */

#define CANOPUS_FS_READ     0x0001u
#define CANOPUS_FS_WRITE    0x0002u /* create or truncate */
#define CANOPUS_FS_APPEND   0x0004u
#define CANOPUS_FS_CREATE   0x0008u
#define CANOPUS_FS_TRUNC    0x0010u
#define CANOPUS_FS_RDWR     (CANOPUS_FS_READ | CANOPUS_FS_WRITE)

#define CANOPUS_FS_SEEK_SET 0
#define CANOPUS_FS_SEEK_CUR 1
#define CANOPUS_FS_SEEK_END 2

#define CANOPUS_FS_MAX_OPEN 16u
#define CANOPUS_FS_NAME_MAX 63u
#define CANOPUS_FS_PATH_MAX 128u

struct canopus_fs_stat {
    uint32_t size;
    uint8_t is_dir;
    uint8_t _pad[3];
    uint32_t mtime;
};

struct canopus_fs_dirent {
    char name[64];
    uint8_t is_dir;
    uint8_t _pad[3];
};

/* -------------------------------------------------------------------------- */
/* MEM (128 KiB Budgeted Heap)                                                */
/* -------------------------------------------------------------------------- */

#define CANOPUS_MEM_MAX_BLOCKS 64u
#define CANOPUS_MEM_BUDGET     131072u /* 128 KiB */

/* -------------------------------------------------------------------------- */
/* NOTIFY                                                                     */
/* -------------------------------------------------------------------------- */

#define CANOPUS_NOTIFY_BODY_MAX 120u
#define CANOPUS_NOTIFY_MAX_POSTS 8u

/* -------------------------------------------------------------------------- */
/* HOOK (Universal Function Hook Engine)                                      */
/* -------------------------------------------------------------------------- */

#define CANOPUS_HOOK_MAX 16u

typedef enum canopus_hook_type {
    CANOPUS_HOOK_AUTO = 0,    /* Auto-detect: FPB for Flash, Trampoline for RAM, Slot for pointer */
    CANOPUS_HOOK_FPB = 1,     /* Cortex-M33 Flash Patch and Breakpoint hardware remap */
    CANOPUS_HOOK_INLINE = 2,  /* Thumb-2 Trampoline jump */
    CANOPUS_HOOK_POINTER = 3, /* Global function pointer table replacement */
} canopus_hook_type_t;

/* Forward declaration */
struct canopus_context_v1;

/* -------------------------------------------------------------------------- */
/* Service Objects                                                            */
/* -------------------------------------------------------------------------- */

struct canopus_fs_obj {
    int32_t (*open)(const struct canopus_context_v1 *ctx, const char *path, uint32_t flags);
    int32_t (*read)(const struct canopus_context_v1 *ctx, int32_t fd, void *buffer, uint32_t size);
    int32_t (*write)(const struct canopus_context_v1 *ctx, int32_t fd, const void *buffer, uint32_t size);
    int64_t (*seek)(const struct canopus_context_v1 *ctx, int32_t fd, int64_t offset, int32_t whence);
    int64_t (*tell)(const struct canopus_context_v1 *ctx, int32_t fd);
    int64_t (*size)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*sync)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*close)(const struct canopus_context_v1 *ctx, int32_t fd);
    int32_t (*remove)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*rename)(const struct canopus_context_v1 *ctx, const char *old_path, const char *new_path);
    int32_t (*stat)(const struct canopus_context_v1 *ctx, const char *path, struct canopus_fs_stat *out_stat);
    int32_t (*exists)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*mkdir)(const struct canopus_context_v1 *ctx, const char *path);
    void *(*opendir)(const struct canopus_context_v1 *ctx, const char *path);
    int32_t (*readdir)(const struct canopus_context_v1 *ctx, void *dir, struct canopus_fs_dirent *out_entry);
    int32_t (*closedir)(const struct canopus_context_v1 *ctx, void *dir);
};

struct canopus_mem_obj {
    void *(*alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void *(*realloc)(const struct canopus_context_v1 *ctx, void *ptr, uint32_t new_size);
    void (*free)(const struct canopus_context_v1 *ctx, void *ptr);
    uint32_t (*available)(const struct canopus_context_v1 *ctx);
    uint32_t (*used)(const struct canopus_context_v1 *ctx);
};

struct canopus_time_obj {
    uint32_t (*monotonic_ms)(const struct canopus_context_v1 *ctx);
    uint64_t (*realtime_ms)(const struct canopus_context_v1 *ctx);
    void (*sleep_ms)(const struct canopus_context_v1 *ctx, uint32_t ms);
};

struct canopus_hook_obj {
    int32_t (*install)(const struct canopus_context_v1 *ctx,
                       uintptr_t target_fn,
                       uintptr_t hook_fn,
                       uintptr_t *orig_fn,
                       canopus_hook_type_t hook_type);
    int32_t (*uninstall)(const struct canopus_context_v1 *ctx, int32_t hook_id);
};

struct canopus_log_obj {
    void (*write)(const struct canopus_context_v1 *ctx, uint32_t level, const char *message);
    void (*info)(const struct canopus_context_v1 *ctx, const char *message);
    void (*warn)(const struct canopus_context_v1 *ctx, const char *message);
    void (*error)(const struct canopus_context_v1 *ctx, const char *message);
    void (*debug)(const struct canopus_context_v1 *ctx, const char *message);
};

struct canopus_notify_obj {
    int32_t (*post)(const struct canopus_context_v1 *ctx, const char *title, const char *body);
};

struct canopus_system_obj {
    const char *(*target_id)(const struct canopus_context_v1 *ctx);
    uint32_t (*framework_revision)(const struct canopus_context_v1 *ctx);
    int (*is_safe_mode)(const struct canopus_context_v1 *ctx);
    int32_t (*reboot)(const struct canopus_context_v1 *ctx);
};

struct canopus_bluetooth_obj {
    int32_t (*l2_send)(const struct canopus_context_v1 *ctx, uint8_t channel, const void *payload, uint16_t len);
    uint32_t (*adapter_state)(const struct canopus_context_v1 *ctx);
};

/* -------------------------------------------------------------------------- */
/* Legacy Services v1 Table (Maintained for Backward Compatibility)           */
/* -------------------------------------------------------------------------- */

struct canopus_services_v1 {
    uint32_t struct_size;
    uint8_t group_version[CANOPUS_SVC_GROUP_COUNT]; /* 0 = absent */

    int32_t (*module_register)(const struct canopus_context_v1 *ctx,
                               const struct canopus_module_descriptor_v1 *descriptor);
    void (*log_write)(const struct canopus_context_v1 *ctx, uint32_t level,
                      const char *message);
    int32_t (*fs_open)(const struct canopus_context_v1 *ctx, const char *name,
                       uint32_t flags);
    int32_t (*fs_read)(const struct canopus_context_v1 *ctx, int32_t handle,
                       void *buffer, uint32_t size);
    int32_t (*fs_write)(const struct canopus_context_v1 *ctx, int32_t handle,
                        const void *buffer, uint32_t size);
    int32_t (*fs_close)(const struct canopus_context_v1 *ctx, int32_t handle);
    int32_t (*fs_remove)(const struct canopus_context_v1 *ctx, const char *name);
    uint32_t (*time_monotonic_ms)(const struct canopus_context_v1 *ctx);
    void *(*mem_alloc)(const struct canopus_context_v1 *ctx, uint32_t size);
    void (*mem_free)(const struct canopus_context_v1 *ctx, void *ptr);
    int32_t (*notify_post)(const struct canopus_context_v1 *ctx, const char *body);
};

/* -------------------------------------------------------------------------- */
/* Context v1 (with First-Class Service Objects)                              */
/* -------------------------------------------------------------------------- */

struct canopus_context_v1 {
    uint32_t struct_size;
    uint32_t abi; /* (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR */
    const struct canopus_services_v1 *services;
    uint32_t slot; /* Supervisor-internal */
    const char *target_id;
    const char *module_id;

    /* Object-oriented Service Objects */
    const struct canopus_fs_obj        *fs;
    const struct canopus_mem_obj       *mem;
    const struct canopus_time_obj      *time;
    const struct canopus_hook_obj      *hook;
    const struct canopus_log_obj       *log;
    const struct canopus_notify_obj    *notify;
    const struct canopus_system_obj    *system;
    const struct canopus_bluetooth_obj *bt;
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
