/*
 * canopus_app.h — public native-app descriptor ABI v1 (CAN-APP-008).
 *
 * The public App SDK exposes stable Canopus types only. Firmware-private
 * launcher structs, addresses and calling constraints live in the target pack
 * and generated code (architecture §3.5), never here.
 *
 * A native app carried by a module registers this descriptor through the
 * target's launcher adapter (the generated target crate / veneer); the
 * descriptor tells the launcher how to create, resume, pause and destroy the
 * app, and how to deliver System/UI messages.
 *
 * Layout: fixed-width fields, natural alignment, validated with
 * CANOPUS_STATIC_ASSERT (same discipline as sdk/c/canopus_abi.h).
 */
#ifndef CANOPUS_APP_H
#define CANOPUS_APP_H

#include <stddef.h>
#include <stdint.h>

#include "canopus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_APP_ABI_MAJOR 1u
#define CANOPUS_APP_ABI_MINOR 0u

#define CANOPUS_APP_MAGIC 0x43415032u /* "CAP2" */

/* App descriptor flags. */
#define CANOPUS_APP_FLAG_MANAGER_PAGE      (1u << 0) /* acts as a Manager page */
#define CANOPUS_APP_FLAG_FULLSCREEN        (1u << 1)
#define CANOPUS_APP_FLAG_NO_STATUS_BAR     (1u << 2)
#define CANOPUS_APP_FLAG_LAUNCHER_VISIBLE  (1u << 3)

/* UI message ids delivered via on_dispatch. */
#define CANOPUS_APP_MSG_CREATE   1u
#define CANOPUS_APP_MSG_RESUME   2u
#define CANOPUS_APP_MSG_PAUSE    3u
#define CANOPUS_APP_MSG_DESTROY  4u
/* System 26/27 are the launcher hide/show appid messages (EVID-APP-002). */
#define CANOPUS_APP_MSG_LAUNCHER_HIDE 26u
#define CANOPUS_APP_MSG_LAUNCHER_SHOW 27u

struct canopus_app_context_v1;

typedef int32_t (*canopus_app_cb)(const struct canopus_app_context_v1 *ctx);
typedef int32_t (*canopus_app_dispatch_cb)(
    const struct canopus_app_context_v1 *ctx,
    uint32_t msg,
    const void *payload,
    uint32_t payload_len);

struct canopus_app_descriptor_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t flags;
    uint8_t app_id[32];      /* reverse-dns app id */
    uint8_t app_name[32];    /* launcher-displayed name */
    uint8_t icon_ref[32];    /* icon resource reference */

    canopus_app_cb on_create;
    canopus_app_cb on_resume;
    canopus_app_cb on_pause;
    canopus_app_cb on_destroy;
    canopus_app_dispatch_cb on_dispatch;
};

/* Opaque app context; only ever used behind a pointer. */
struct canopus_app_context_v1 {
    uint32_t opaque[4];
};

/* Validates descriptor header + ABI version. Returns 0 when well-formed. */
int canopus_app_descriptor_check(const struct canopus_app_descriptor_v1 *d);

/* Writes a versioned app status record through the portable status writer
 * (CAN-APP-011): "APP2" magic + abi + app_state + app_flags, then publishes.
 * Returns 0 on success, -1 on a full writer. */
int canopus_app_status_write(struct canopus_status_writer_v1 *w,
                             uint32_t app_state,
                             uint32_t app_flags);

/* Static layout checks (mirrors canopus_abi.h discipline). */
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_app_descriptor_v1, app_id) == 12,
                      "canopus_app_descriptor_v1 app_id offset");
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_app_descriptor_v1, icon_ref) == 76,
                      "canopus_app_descriptor_v1 icon_ref offset");
#if UINTPTR_MAX == 0xffffffffu
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_app_descriptor_v1, on_create) == 108,
                      "canopus_app_descriptor_v1 on_create offset (32-bit)");
CANOPUS_STATIC_ASSERT(sizeof(struct canopus_app_descriptor_v1) == 128,
                      "canopus_app_descriptor_v1 32-bit size");
#else
CANOPUS_STATIC_ASSERT(offsetof(struct canopus_app_descriptor_v1, on_create) == 112,
                      "canopus_app_descriptor_v1 on_create offset (64-bit)");
CANOPUS_STATIC_ASSERT(sizeof(struct canopus_app_descriptor_v1) == 152,
                      "canopus_app_descriptor_v1 64-bit size");
#endif

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_APP_H */
