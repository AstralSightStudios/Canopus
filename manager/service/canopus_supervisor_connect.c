/*
 * CanopusConnect (ADR-CAN-009): text/binary messages between one module and
 * the remote plugins (AstroBox) listening to its module id.
 *
 * The frames ride the Supervisor's L2 0x43 stream as their own CPC2 opcodes.
 * They are handled before the Manager dispatch and never touch the pending
 * table, the status snapshot or the error code, and the outgoing events use a
 * queue of their own that the backend drains after Manager responses.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "canopus_services.h"

#define CONNECT_LISTENERS 8u
#define CONNECT_HEADER 2u /* kind-or-enable, id length */
#define CONNECT_FRAME_MAX (CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_PROTO_MAX_PAYLOAD)

/* A remote listener, valid only in the phone session that registered it. */
struct connect_listener {
    uint8_t id[CANOPUS_CONNECT_ID_MAX];
    uint8_t len; /* 0: unused */
    uint32_t peer;
};

static struct canopus_supervisor_v1 *s_sup;
static struct connect_listener s_listeners[CONNECT_LISTENERS];
static struct {
    canopus_connect_callback_t cb;
    void *user_data;
} s_rx[CANOPUS_SUP_MODULE_SLOTS];
static uint8_t s_text[CANOPUS_CONNECT_DATA_MAX + 1u];

static uint32_t connect_peer(void)
{
    return s_sup->platform->l2_peer(s_sup->platform_cookie);
}

static int connect_same_id(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static int connect_id_ok(const uint8_t *id, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < len; i++) {
        if (!((id[i] >= 'a' && id[i] <= 'z') || (id[i] >= '0' && id[i] <= '9') || id[i] == '_')) {
            return 0;
        }
    }
    return 1;
}

static struct connect_listener *connect_find(const uint8_t *id, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < CONNECT_LISTENERS; i++) {
        if (s_listeners[i].len == len && connect_same_id(s_listeners[i].id, id, len)) {
            return &s_listeners[i];
        }
    }
    return 0;
}

static int connect_slot_for(const uint8_t *id, uint32_t len)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        const uint8_t *stored = s_sup->modules[i].module_id;
        if (s_sup->modules[i].state != 0u && stored[len] == 0u && connect_same_id(stored, id, len)) {
            return (int)i;
        }
    }
    return -1;
}

/* CONNECT_LISTEN: payload = enable, id length, id. */
static uint32_t connect_listen_request(const uint8_t *id, uint32_t len, uint32_t enable,
                                       uint8_t *status)
{
    struct connect_listener *entry = connect_find(id, len);
    uint32_t i, peer = connect_peer();
    if (!enable) {
        if (entry != 0) canopus_memset(entry, 0, sizeof(*entry));
        *status = CANOPUS_CONNECT_STATUS_OK;
        return CANOPUS_RESULT_COMPLETED;
    }
    for (i = 0u; entry == 0 && i < CONNECT_LISTENERS; i++) {
        /* A listener of an older phone session is gone; its entry is free. */
        if (s_listeners[i].len == 0u || s_listeners[i].peer != peer) entry = &s_listeners[i];
    }
    if (entry == 0) {
        *status = CANOPUS_CONNECT_STATUS_FULL;
        return CANOPUS_RESULT_DISALLOWED;
    }
    canopus_memset(entry, 0, sizeof(*entry));
    canopus_memcpy(entry->id, id, len);
    entry->len = (uint8_t)len;
    entry->peer = peer;
    *status = CANOPUS_CONNECT_STATUS_OK;
    return CANOPUS_RESULT_COMPLETED;
}

/* CONNECT_SEND: payload = kind, id length, id, data. Runs on the module/UI task. */
static uint32_t connect_send_request(const uint8_t *id, uint32_t len, uint32_t kind,
                                     const uint8_t *data, uint32_t data_len, uint8_t *status)
{
    const void *delivered = data;
    int slot = connect_slot_for(id, len);
    if (slot < 0) {
        *status = CANOPUS_CONNECT_STATUS_MODULE;
        return CANOPUS_RESULT_DISALLOWED;
    }
    if (s_rx[slot].cb == 0) {
        *status = CANOPUS_CONNECT_STATUS_NOT_LISTENING;
        return CANOPUS_RESULT_DISALLOWED;
    }
    if (kind == CANOPUS_CONNECT_TEXT) {
        canopus_memcpy(s_text, data, data_len);
        s_text[data_len] = 0u;
        delivered = s_text;
    }
    s_rx[slot].cb(canopus_supervisor_context(s_sup, (uint32_t)slot), kind, delivered, data_len,
                  s_rx[slot].user_data);
    *status = CANOPUS_CONNECT_STATUS_OK;
    return CANOPUS_RESULT_COMPLETED;
}

uint32_t canopus_supervisor_connect_request(const struct canopus_proto_request_v1 *req,
                                            const uint8_t *payload, uint8_t *status)
{
    uint32_t id_len, data_len;
    *status = CANOPUS_CONNECT_STATUS_FORMAT;
    if (s_sup == 0 || req->payload_size < CONNECT_HEADER) return CANOPUS_RESULT_REJECTED;
    id_len = payload[1];
    if (id_len == 0u || id_len > CANOPUS_CONNECT_ID_MAX || req->payload_size < CONNECT_HEADER + id_len ||
        !connect_id_ok(payload + CONNECT_HEADER, id_len)) {
        return CANOPUS_RESULT_REJECTED;
    }
    data_len = req->payload_size - CONNECT_HEADER - id_len;
    if (req->command == CANOPUS_CMD_CONNECT_LISTEN) {
        if (payload[0] > 1u || data_len != 0u) return CANOPUS_RESULT_REJECTED;
        return connect_listen_request(payload + CONNECT_HEADER, id_len, payload[0], status);
    }
    if ((payload[0] != CANOPUS_CONNECT_TEXT && payload[0] != CANOPUS_CONNECT_BINARY) ||
        data_len > CANOPUS_CONNECT_DATA_MAX) {
        return CANOPUS_RESULT_REJECTED;
    }
    return connect_send_request(payload + CONNECT_HEADER, id_len, payload[0],
                                payload + CONNECT_HEADER + id_len, data_len, status);
}

/* ---- module-facing object -------------------------------------------- */

static int connect_ctx_ok(const struct canopus_context_v1 *ctx)
{
    return s_sup != 0 && ctx != 0 && ctx->slot < CANOPUS_SUP_MODULE_SLOTS &&
           ctx == canopus_supervisor_context(s_sup, ctx->slot) &&
           s_sup->modules[ctx->slot].state != 0u;
}

/* Ids the wire cannot carry (longer than CANOPUS_CONNECT_ID_MAX) are never
 * addressed remotely. */
static uint32_t connect_id_len(uint32_t slot)
{
    return (uint32_t)canopus_strnlen((const char *)s_sup->modules[slot].module_id,
                                     CANOPUS_SUP_MODULE_ID_MAX);
}

static int connect_listening(uint32_t slot)
{
    uint32_t len = connect_id_len(slot), peer = connect_peer();
    struct connect_listener *entry =
        len <= CANOPUS_CONNECT_ID_MAX ? connect_find(s_sup->modules[slot].module_id, len) : 0;
    return peer != 0u && entry != 0 && entry->peer == peer;
}

static int32_t connect_send(const struct canopus_context_v1 *ctx, uint32_t kind,
                            const void *data, uint32_t len)
{
    uint8_t payload[CONNECT_HEADER + CANOPUS_CONNECT_ID_MAX + CANOPUS_CONNECT_DATA_MAX];
    uint8_t frame[CONNECT_FRAME_MAX];
    uint32_t id_len;
    int total;
    if (!connect_ctx_ok(ctx)) return CANOPUS_SVC_ERR_STATE;
    if ((kind != CANOPUS_CONNECT_TEXT && kind != CANOPUS_CONNECT_BINARY) ||
        len > CANOPUS_CONNECT_DATA_MAX || (len != 0u && data == 0)) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (!connect_listening(ctx->slot)) return CANOPUS_CONNECT_NOT_LISTENING;
    id_len = connect_id_len(ctx->slot);
    payload[0] = (uint8_t)kind;
    payload[1] = (uint8_t)id_len;
    canopus_memcpy(payload + CONNECT_HEADER, s_sup->modules[ctx->slot].module_id, id_len);
    canopus_memcpy(payload + CONNECT_HEADER + id_len, data, len);
    total = canopus_transport_v2_encode_event(CANOPUS_CMD_CONNECT_MESSAGE, payload,
                                              CONNECT_HEADER + id_len + len, frame, sizeof(frame));
    return s_sup->platform->l2_event(s_sup->platform_cookie, frame, (uint16_t)total);
}

static int32_t connect_listen(const struct canopus_context_v1 *ctx, canopus_connect_callback_t cb,
                              void *user_data)
{
    if (!connect_ctx_ok(ctx)) return CANOPUS_SVC_ERR_STATE;
    s_rx[ctx->slot].cb = cb;
    s_rx[ctx->slot].user_data = cb != 0 ? user_data : 0;
    return CANOPUS_SVC_OK;
}

static int connect_remote_listening(const struct canopus_context_v1 *ctx)
{
    if (!connect_ctx_ok(ctx)) return CANOPUS_SVC_ERR_STATE;
    return connect_listening(ctx->slot);
}

static const struct canopus_connect_obj s_connect_obj = {
    connect_send,
    connect_listen,
    connect_remote_listening,
};

const struct canopus_connect_obj *canopus_supervisor_connect_bind(struct canopus_supervisor_v1 *sup)
{
    s_sup = sup;
    canopus_memset(s_listeners, 0, sizeof(s_listeners));
    canopus_memset(s_rx, 0, sizeof(s_rx));
    return sup != 0 && sup->platform != 0 && sup->platform->l2_event != 0 &&
                   sup->platform->l2_peer != 0
               ? &s_connect_obj : 0;
}

void canopus_supervisor_connect_release(uint32_t slot)
{
    if (slot < CANOPUS_SUP_MODULE_SLOTS) canopus_memset(&s_rx[slot], 0, sizeof(s_rx[slot]));
}
