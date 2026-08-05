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
