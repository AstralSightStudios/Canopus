/*
 * canopus_protocol.c — versioned supervisor control protocol.
 */
#include "canopus_protocol.h"
#include "canopus_memory.h"

int canopus_proto_validate_request(const struct canopus_proto_request_v1 *r,
                                   uint32_t buffer_capacity)
{
    if (r == 0) {
        return -1;
    }
    if (r->magic != CANOPUS_PROTO_MAGIC) {
        return -1;
    }
    if (r->struct_size != CANOPUS_PROTO_REQUEST_SIZE) {
        return -1;
    }
    if (r->abi_major != CANOPUS_ABI_MAJOR) {
        return -1;
    }
    if (r->payload_size > CANOPUS_PROTO_MAX_PAYLOAD) {
        return -1;
    }
    if (buffer_capacity < CANOPUS_PROTO_REQUEST_SIZE + r->payload_size) {
        return -1;
    }
    return 0;
}

int canopus_proto_validate_response(const struct canopus_proto_response_v1 *r,
                                    uint32_t buffer_capacity)
{
    if (r == 0) {
        return -1;
    }
    if (r->magic != CANOPUS_PROTO_MAGIC) {
        return -1;
    }
    if (r->struct_size != CANOPUS_PROTO_RESPONSE_SIZE) {
        return -1;
    }
    if (r->abi_major != CANOPUS_ABI_MAJOR) {
        return -1;
    }
    if (r->payload_size > CANOPUS_PROTO_MAX_PAYLOAD) {
        return -1;
    }
    if (buffer_capacity < CANOPUS_PROTO_RESPONSE_SIZE + r->payload_size) {
        return -1;
    }
    return 0;
}

void canopus_proto_response_init(struct canopus_proto_response_v1 *resp,
                                 uint32_t request_id, uint32_t result_state,
                                 uint32_t payload_size)
{
    resp->magic = CANOPUS_PROTO_MAGIC;
    resp->struct_size = CANOPUS_PROTO_RESPONSE_SIZE;
    resp->abi_major = CANOPUS_ABI_MAJOR;
    resp->abi_minor = CANOPUS_ABI_MINOR;
    resp->request_id = request_id;
    resp->result_state = result_state;
    resp->payload_size = payload_size;
    resp->flags = 0;
}

/* ------------------------------------------------------------------ */
/* v2 transport envelope (CAN-P0-008)                                  */
/* ------------------------------------------------------------------ */

static uint16_t v2_u16(const uint8_t *b)
{
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static uint32_t v2_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void v2_put_u32(uint8_t *out, uint32_t offset, uint32_t value)
{
    out[offset] = (uint8_t)(value & 0xffu);
    out[offset + 1u] = (uint8_t)((value >> 8) & 0xffu);
    out[offset + 2u] = (uint8_t)((value >> 16) & 0xffu);
    out[offset + 3u] = (uint8_t)((value >> 24) & 0xffu);
}

int canopus_transport_v2_decode_request(const uint8_t *buf, uint32_t len,
                                        struct canopus_proto_request_v1 *req,
                                        uint32_t *payload_offset)
{
    uint32_t magic, total_size, payload_size, request_id;
    uint16_t header_size, kind, major, minor;
    if (buf == 0 || req == 0 || payload_offset == 0) {
        return -1;
    }
    if (len < CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        return -1;
    }
    magic = v2_u32(buf + 0);
    header_size = v2_u16(buf + 4);
    kind = v2_u16(buf + 6);
    major = v2_u16(buf + 8);
    minor = v2_u16(buf + 10);
    total_size = v2_u32(buf + 12);
    payload_size = v2_u32(buf + 32);
    request_id = v2_u32(buf + 20);
    if (magic != CANOPUS_TRANSPORT_V2_MAGIC) {
        return -1;
    }
    if (header_size < CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        return -1; /* header must at least span the known fields */
    }
    if (kind != CANOPUS_TRANSPORT_V2_REQUEST) {
        return -1;
    }
    if (major != CANOPUS_ABI_MAJOR) {
        return -1;
    }
    if (minor > CANOPUS_ABI_MINOR) {
        return -1; /* append-only minor: unknown newer minor fails closed */
    }
    if (payload_size > CANOPUS_PROTO_MAX_PAYLOAD) {
        return -1;
    }
    /* total_size must equal header + payload and fit the caller buffer */
    if (total_size != (uint32_t)header_size + payload_size) {
        return -1;
    }
    if (len < total_size) {
        return -1;
    }
    if (request_id == 0) {
        return -1; /* 0 is reserved for unsolicited events */
    }
    /* CAN-P2-003: unknown flag bits fail closed rather than carry
     * uninterpreted semantics. */
    if ((v2_u32(buf + 24) & ~CANOPUS_TRANSPORT_V2_FLAGS_KNOWN) != 0u) {
        return -1;
    }
    req->magic = CANOPUS_TRANSPORT_V2_MAGIC;
    req->struct_size = CANOPUS_PROTO_REQUEST_SIZE;
    req->abi_major = major;
    req->abi_minor = minor;
    req->command = v2_u32(buf + 16);
    req->request_id = request_id;
    req->flags = v2_u32(buf + 24);
    req->payload_size = payload_size;
    *payload_offset = (uint32_t)header_size;
    return 0;
}

int canopus_transport_v2_encode_request(const struct canopus_proto_request_v1 *req,
                                        const void *payload,
                                        uint8_t *out, uint32_t cap)
{
    uint32_t total;
    if (req == 0 || out == 0 || req->magic != CANOPUS_PROTO_MAGIC ||
        req->struct_size != CANOPUS_PROTO_REQUEST_SIZE ||
        req->abi_major != CANOPUS_ABI_MAJOR ||
        req->abi_minor > CANOPUS_ABI_MINOR || req->request_id == 0u ||
        req->payload_size > CANOPUS_PROTO_MAX_PAYLOAD ||
        (req->flags & ~CANOPUS_TRANSPORT_V2_FLAGS_KNOWN) != 0u ||
        (req->payload_size != 0u && payload == 0)) {
        return -1;
    }
    total = CANOPUS_TRANSPORT_V2_HEADER_SIZE + req->payload_size;
    if (cap < total) {
        return -1;
    }
    canopus_memset(out, 0, total);
    v2_put_u32(out, 0, CANOPUS_TRANSPORT_V2_MAGIC);
    out[4] = (uint8_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE & 0xffu);
    out[5] = (uint8_t)((CANOPUS_TRANSPORT_V2_HEADER_SIZE >> 8) & 0xffu);
    out[6] = (uint8_t)CANOPUS_TRANSPORT_V2_REQUEST;
    out[8] = (uint8_t)(req->abi_major & 0xffu);
    out[9] = (uint8_t)((req->abi_major >> 8) & 0xffu);
    out[10] = (uint8_t)(req->abi_minor & 0xffu);
    out[11] = (uint8_t)((req->abi_minor >> 8) & 0xffu);
    v2_put_u32(out, 12, total);
    v2_put_u32(out, 16, req->command);
    v2_put_u32(out, 20, req->request_id);
    v2_put_u32(out, 24, req->flags);
    v2_put_u32(out, 28, 0);
    v2_put_u32(out, 32, req->payload_size);
    if (req->payload_size != 0u) {
        canopus_memcpy(out + CANOPUS_TRANSPORT_V2_HEADER_SIZE, payload,
                       req->payload_size);
    }
    return (int)total;
}

int canopus_transport_v2_decode_response(const uint8_t *buf, uint32_t len,
                                         struct canopus_proto_response_v1 *resp,
                                         uint32_t *opcode,
                                         uint32_t *payload_offset)
{
    uint32_t total_size, payload_size, request_id;
    uint16_t header_size, kind, major, minor;
    if (buf == 0 || resp == 0 || opcode == 0 || payload_offset == 0 ||
        len < CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        return -1;
    }
    header_size = v2_u16(buf + 4);
    kind = v2_u16(buf + 6);
    major = v2_u16(buf + 8);
    minor = v2_u16(buf + 10);
    total_size = v2_u32(buf + 12);
    request_id = v2_u32(buf + 20);
    payload_size = v2_u32(buf + 32);
    if (v2_u32(buf) != CANOPUS_TRANSPORT_V2_MAGIC ||
        header_size < CANOPUS_TRANSPORT_V2_HEADER_SIZE ||
        kind != CANOPUS_TRANSPORT_V2_RESPONSE ||
        major != CANOPUS_ABI_MAJOR || minor > CANOPUS_ABI_MINOR ||
        request_id == 0u || payload_size > CANOPUS_PROTO_MAX_PAYLOAD ||
        (v2_u32(buf + 24) & ~CANOPUS_TRANSPORT_V2_FLAGS_KNOWN) != 0u ||
        v2_u32(buf + 16) == 0u ||
        v2_u32(buf + 28) < CANOPUS_RESULT_REJECTED ||
        v2_u32(buf + 28) > CANOPUS_RESULT_REBOOT_REQUIRED ||
        total_size != (uint32_t)header_size + payload_size ||
        len != total_size) {
        return -1;
    }
    resp->magic = CANOPUS_PROTO_MAGIC;
    resp->struct_size = CANOPUS_PROTO_RESPONSE_SIZE;
    resp->abi_major = major;
    resp->abi_minor = minor;
    resp->request_id = request_id;
    resp->result_state = v2_u32(buf + 28);
    resp->payload_size = payload_size;
    resp->flags = v2_u32(buf + 24);
    *opcode = v2_u32(buf + 16);
    *payload_offset = (uint32_t)header_size;
    return 0;
}

int canopus_transport_v2_encode_response(const struct canopus_proto_response_v1 *resp,
                                         uint32_t opcode,
                                         const void *payload,
                                         uint32_t payload_len,
                                         uint8_t *out, uint32_t cap)
{
    uint32_t total;
    if (resp == 0 || out == 0 || payload_len > CANOPUS_PROTO_MAX_PAYLOAD ||
        (payload_len > 0 && payload == 0)) {
        return -1;
    }
    total = CANOPUS_TRANSPORT_V2_HEADER_SIZE + payload_len;
    if (cap < total) {
        return -1;
    }
    canopus_memset(out, 0, total);
    out[0] = (uint8_t)(CANOPUS_TRANSPORT_V2_MAGIC & 0xff);
    out[1] = (uint8_t)((CANOPUS_TRANSPORT_V2_MAGIC >> 8) & 0xff);
    out[2] = (uint8_t)((CANOPUS_TRANSPORT_V2_MAGIC >> 16) & 0xff);
    out[3] = (uint8_t)((CANOPUS_TRANSPORT_V2_MAGIC >> 24) & 0xff);
    out[4] = (uint8_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE & 0xff);
    out[5] = (uint8_t)((CANOPUS_TRANSPORT_V2_HEADER_SIZE >> 8) & 0xff);
    out[6] = (uint8_t)CANOPUS_TRANSPORT_V2_RESPONSE;
    out[7] = 0;
    out[8] = (uint8_t)(resp->abi_major & 0xff);
    out[9] = (uint8_t)((resp->abi_major >> 8) & 0xff);
    out[10] = (uint8_t)(resp->abi_minor & 0xff);
    out[11] = (uint8_t)((resp->abi_minor >> 8) & 0xff);
#define V2_PUT32(o, v) \
    do { \
        uint32_t _v = (uint32_t)(v); \
        out[(o)] = (uint8_t)(_v & 0xff); \
        out[(o) + 1] = (uint8_t)((_v >> 8) & 0xff); \
        out[(o) + 2] = (uint8_t)((_v >> 16) & 0xff); \
        out[(o) + 3] = (uint8_t)((_v >> 24) & 0xff); \
    } while (0)
    V2_PUT32(12, total);
    V2_PUT32(16, opcode);
    V2_PUT32(20, resp->request_id);
    V2_PUT32(24, resp->flags);
    V2_PUT32(28, resp->result_state);
    V2_PUT32(32, payload_len);
#undef V2_PUT32
    if (payload_len > 0) {
        canopus_memcpy(out + CANOPUS_TRANSPORT_V2_HEADER_SIZE, payload, payload_len);
    }
    return (int)total;
}

/* ------------------------------------------------------------------ */
/* Pending-request table (CAN-P1-002)                                  */
/* ------------------------------------------------------------------ */

static int state_valid(uint32_t s)
{
    return s >= CANOPUS_RESULT_REJECTED && s <= CANOPUS_RESULT_REBOOT_REQUIRED;
}

static int state_is_terminal(uint32_t s)
{
    return s == CANOPUS_RESULT_COMPLETED || s == CANOPUS_RESULT_FAILED ||
           s == CANOPUS_RESULT_DISALLOWED || s == CANOPUS_RESULT_REBOOT_REQUIRED;
}

/* The only legal edges. ACCEPTED may go straight to a terminal state for a
 * synchronously-completed operation; everything else must pass through
 * RUNNING before reaching a terminal state. REJECTED is never a tracked
 * state (a rejected request is refused synchronously). */
static int pending_legal(uint32_t from, uint32_t to)
{
    if (!state_valid(to) || to == CANOPUS_RESULT_REJECTED) {
        return 0;
    }
    if (from == CANOPUS_RESULT_ACCEPTED) {
        return to == CANOPUS_RESULT_QUEUED || to == CANOPUS_RESULT_RUNNING ||
               state_is_terminal(to);
    }
    if (from == CANOPUS_RESULT_QUEUED) {
        return to == CANOPUS_RESULT_RUNNING || state_is_terminal(to);
    }
    if (from == CANOPUS_RESULT_RUNNING) {
        return state_is_terminal(to);
    }
    return 0; /* REJECTED, terminal or unknown from-state: no transitions */
}

void canopus_pending_init(struct canopus_pending_table_v1 *t)
{
    canopus_memset(t, 0, sizeof(*t));
}

void canopus_pending_set_boot(struct canopus_pending_table_v1 *t,
                              uint32_t boot_id)
{
    if (t != 0) {
        t->boot_id = boot_id;
    }
}

static struct canopus_pending_request_v1 *find_slot(
    struct canopus_pending_table_v1 *t, uint32_t request_id)
{
    uint32_t i;
    if (t == 0) {
        return 0;
    }
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            return &t->slots[i];
        }
    }
    return 0;
}

int canopus_pending_accept(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t command)
{
    uint32_t i;
    if (t == 0 || request_id == 0) {
        return -1; /* 0 is reserved; never a valid client id */
    }
    if (find_slot(t, request_id) != 0) {
        return -1; /* duplicate id */
    }
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (!t->slots[i].active) {
            t->slots[i].request_id = request_id;
            t->slots[i].command = command;
            t->slots[i].state = CANOPUS_RESULT_ACCEPTED;
            t->slots[i].active = 1;
            t->slots[i].boot_id = t->boot_id;
            t->slots[i].error = 0;
            return 0;
        }
    }
    return -1; /* table full */
}

int canopus_pending_set_state(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t state)
{
    struct canopus_pending_request_v1 *p = find_slot(t, request_id);
    if (p == 0 || p->boot_id != t->boot_id) {
        return -1; /* unknown id or stale boot */
    }
    if (!pending_legal(p->state, state)) {
        return -1; /* backwards, terminal->anything, REJECTED or unknown */
    }
    p->state = state;
    return 0;
}

int canopus_pending_set_error(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t error)
{
    struct canopus_pending_request_v1 *p = find_slot(t, request_id);
    if (p == 0 || p->boot_id != t->boot_id) {
        return -1;
    }
    p->error = error;
    return 0;
}

int canopus_pending_finish(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t result)
{
    struct canopus_pending_request_v1 *p = find_slot(t, request_id);
    if (p == 0 || p->boot_id != t->boot_id) {
        return -1;
    }
    if (!state_is_terminal(result)) {
        return -1; /* finish must carry an explicit terminal result */
    }
    if (state_is_terminal(p->state)) {
        /* already terminal: the same result is idempotent, a contradicting
         * terminal result is rejected rather than silently overwritten */
        return p->state == result ? 0 : -1;
    }
    if (!pending_legal(p->state, result)) {
        return -1;
    }
    p->state = result;
    /* terminal record is RETAINED until ack, so a late query still finds it */
    return 0;
}

int canopus_pending_ack(struct canopus_pending_table_v1 *t,
                        uint32_t request_id)
{
    struct canopus_pending_request_v1 *p = find_slot(t, request_id);
    if (p == 0 || p->boot_id != t->boot_id) {
        return -1;
    }
    p->active = 0;
    return 0;
}

const struct canopus_pending_request_v1 *canopus_pending_find(
    const struct canopus_pending_table_v1 *t, uint32_t request_id)
{
    uint32_t i;
    if (t == 0) {
        return 0;
    }
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            return &t->slots[i];
        }
    }
    return 0;
}
