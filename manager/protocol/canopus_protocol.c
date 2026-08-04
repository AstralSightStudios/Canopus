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
/* Pending-request table                                               */
/* ------------------------------------------------------------------ */

static int state_is_terminal(uint32_t s)
{
    return s == CANOPUS_RESULT_COMPLETED || s == CANOPUS_RESULT_FAILED ||
           s == CANOPUS_RESULT_DISALLOWED || s == CANOPUS_RESULT_REBOOT_REQUIRED;
}

void canopus_pending_init(struct canopus_pending_table_v1 *t)
{
    canopus_memset(t, 0, sizeof(*t));
}

int canopus_pending_accept(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t command)
{
    uint32_t i;
    if (request_id == 0) {
        return -1;
    }
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            return -1; /* duplicate id */
        }
    }
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (!t->slots[i].active) {
            t->slots[i].request_id = request_id;
            t->slots[i].command = command;
            t->slots[i].state = CANOPUS_RESULT_ACCEPTED;
            t->slots[i].active = 1;
            return 0;
        }
    }
    return -1; /* table full */
}

int canopus_pending_set_state(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t state)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            if (state_is_terminal(t->slots[i].state)) {
                return -1; /* already terminal */
            }
            t->slots[i].state = state;
            return 0;
        }
    }
    return -1;
}

const struct canopus_pending_request_v1 *canopus_pending_find(
    const struct canopus_pending_table_v1 *t, uint32_t request_id)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            return &t->slots[i];
        }
    }
    return 0;
}

int canopus_pending_finish(struct canopus_pending_table_v1 *t,
                           uint32_t request_id)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        if (t->slots[i].active && t->slots[i].request_id == request_id) {
            if (!state_is_terminal(t->slots[i].state)) {
                t->slots[i].state = CANOPUS_RESULT_COMPLETED;
            }
            t->slots[i].active = 0;
            return 0;
        }
    }
    return -1;
}
