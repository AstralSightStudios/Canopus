/*
 * canopus_interconnect_private.h — C-facing private bindings for the firmware
 * miwear/interconnect connection framework (xiaomi-band-10-pro-3.101.030).
 *
 * Mirror of the Rust surface in
 * `sdk/rust/canopus-target-private/.../xiaomi_band_10_pro_3_101_030.rs`
 * (interconnect section). This is a PRIVATE full-trust header: unlike the
 * audited `generated/canopus_veneer.h` it is NOT evidence-gated, and it is
 * valid only for firmware SHA-256 f701a84f…dccd225b. Modules that include it
 * must run the identity guard first and must never share these addresses.
 *
 * Firmware map (IDA vela_ap.bin.i64):
 *
 *   Phone app (com.xiaomi.miwear.interconnect / Mi Fitness)
 *     ↕ BLE GATT (miwear private protocol)
 *   "btserver"       start_btmsg_server  @0x0CAA37E8
 *   "miwear-server"  quickapp_proxy_server_start @0x0C526628
 *   connection framework (named servers over a polled socket/msq transport)
 *     connect sub_C2D2034   send sub_C2D20C4   close sub_C2D2198
 *   quickapp JS: system.interconnect → jse_miwear.cpp → these calls.
 *
 * Every function address carries the Thumb bit, matching canopus_veneer.h.
 * All calls are unsafe: the module owns connection-object memory, message
 * lifetime, and callback threading discipline.
 */
#ifndef CANOPUS_INTERCONNECT_PRIVATE_H
#define CANOPUS_INTERCONNECT_PRIVATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Message header (uv_miwear_message_t), 20 bytes, pointer-free.       */
/* ------------------------------------------------------------------ */

#define CANOPUS_CONN_MSG_TYPE_EVENT 2u
#define CANOPUS_CONN_MSG_TYPE_DATA  0x83u

/* Event status-word values delivered to the recv callback. The raw socket
 * layer uses 1 for "connected"; the miwear proxy re-stamps these codes
 * through byte_2CCF98F4, which is what a peer observes. */
#define CANOPUS_CONN_STATUS_CONNECTED    5
#define CANOPUS_CONN_STATUS_DISCONNECTED 6
#define CANOPUS_CONN_STATUS_UNINSTALLED  7
#define CANOPUS_CONN_STATUS_FAILED       2
#define CANOPUS_CONN_STATUS_CLOSED       3

/* Connection object layout written by canopus_interconnect_connect:
 * conn[0] = firmware node, conn[4] = recv cb, conn[8] = active flag.
 * The module owns a buffer of at least 12 bytes for the link lifetime. */
#define CANOPUS_CONN_RECV_CB_OFFSET 4u

/* Default interconnect phone-side package. The firmware reads it from the
 * `interconnect.appname` config property (property_get at 0x0C66B8C0); every
 * `*.appname` property holds a com.xiaomi.miwear.* package name. The phone
 * routes messages to a connection by package name only - the app display name
 * is not part of the routing. A native module is not limited to this value:
 * it may register and connect its own package name (register_app). */
#define CANOPUS_INTERCONNECT_APK_PACKAGE "com.xiaomi.miwear.interconnect"

/* App descriptor passed to canopus_interconnect_register_app. Matches the
 * firmware quickapp_app_info layout (36 bytes on the 32-bit target):
 *   +0  package name  (phone-side routing key)
 *   +4  display name  (not used for routing)
 *   +8  icon file name under /data/app/<package>/
 *   +12 extra string slot
 *   +16 20-byte fingerprint verified by quickapp_get_appinfo. */
typedef struct {
    const char *package_name;   /* +0x0 */
    const char *display_name;   /* +0x4 */
    const char *icon_file;      /* +0x8 */
    const char *extra;          /* +0xc */
    uint8_t fingerprint[20];    /* +0x10 */
} canopus_interconnect_app_info;

/* Registers a package in the quickapp routing registry. The firmware seeds the
 * registry at bootup from rpk_info.json (quickapp_bootup_register); the
 * watch->phone send path (quickapp_send_wearmsg) looks a package up here, so a
 * native module that wants its own package name must register it first.
 * app_id is a u16 identifier (stock registrar hands out sequential ids).
 * Returns 0 on success, -1 on allocation failure. */
typedef int32_t (*canopus_interconnect_register_app_fn)(
    uint16_t app_id, const canopus_interconnect_app_info *info);
static inline int32_t canopus_interconnect_register_app(
    uint16_t app_id, const canopus_interconnect_app_info *info)
{
    return ((canopus_interconnect_register_app_fn)(uintptr_t)0x0C527E39)(
        app_id, info);
}

typedef struct {
    uint8_t type;           /* +0x0  2=event, 0x83=data */
    uint8_t _pad_type[3];
    uint32_t length;        /* +0x4  payload length; 8 for events */
    uint32_t _reserved[2];  /* +0x8  +0xc */
    uint32_t value;         /* +0x10 payload addr (data) / status-word addr (event) */
} canopus_interconnect_message;

/* Receives connection events and data. Runs on the connection-framework owner
 * thread: never block, re-enter the module core only through a non-blocking
 * lock. `name` is the connection name registered at connect. */
typedef void (*canopus_interconnect_recv_cb)(
    void *conn, int32_t status, const canopus_interconnect_message *msg,
    const char *name);

/* Completion for canopus_interconnect_send: (conn, status, msg, arg).
 * status is 0 on success. The message payload must stay valid until this
 * fires. */
typedef void (*canopus_interconnect_send_done)(
    void *conn, int32_t status, const canopus_interconnect_message *msg,
    void *arg);

/* ------------------------------------------------------------------ */
/* Typed veneers (private).                                            */
/* ------------------------------------------------------------------ */

/* Reads the global connection-framework loop handle. Every named server
 * ("btserver", "miwear-server") and connection lives on this registry. */
static inline void *canopus_interconnect_loop(void)
{
    return *(void *volatile *)(uintptr_t)0x20121F90;
}

/* Registers a named connection and attaches it to "miwear-server". `conn` is
 * a module-owned buffer of at least 12 bytes for the link lifetime. `name` is
 * the phone-side PACKAGE NAME - the routing key the phone uses to deliver
 * messages (e.g. CANOPUS_INTERCONNECT_APK_PACKAGE); the app display name is
 * not part of the routing. Returns 0 on accepted registration. */
typedef int32_t (*canopus_interconnect_connect_fn)(
    void *loop, void *conn, const char *name, canopus_interconnect_recv_cb cb);
static inline int32_t canopus_interconnect_connect(
    void *loop, void *conn, const char *name, canopus_interconnect_recv_cb cb)
{
    return ((canopus_interconnect_connect_fn)(uintptr_t)0x0C2D2035)(
        loop, conn, name, cb);
}

typedef int32_t (*canopus_interconnect_send_fn)(
    void *handle, const char *name, const canopus_interconnect_message *msg,
    canopus_interconnect_send_done done, void *arg);
static inline int32_t canopus_interconnect_send(
    void *handle, const char *name, const canopus_interconnect_message *msg,
    canopus_interconnect_send_done done, void *arg)
{
    return ((canopus_interconnect_send_fn)(uintptr_t)0x0C2D20C5)(
        handle, name, msg, done, arg);
}

typedef int32_t (*canopus_interconnect_close_fn)(void *conn);
static inline int32_t canopus_interconnect_close(void *conn)
{
    return ((canopus_interconnect_close_fn)(uintptr_t)0x0C2D2199)(conn);
}

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_INTERCONNECT_PRIVATE_H */
