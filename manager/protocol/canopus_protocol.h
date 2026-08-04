/*
 * canopus_protocol.h — versioned supervisor control protocol (CAN-DEV-002).
 *
 * Framing over the proven char-device control channel. Requests and
 * responses are fixed-width envelopes; the payload is opaque to the
 * framing layer. A request is identified by request_id and moves through
 * ACCEPTED -> QUEUED -> RUNNING -> COMPLETED/FAILED (or DISALLOWED /
 * REBOOT_REQUIRED), never describing a queued request as completed.
 */
#ifndef CANOPUS_PROTOCOL_H
#define CANOPUS_PROTOCOL_H

#include <stdint.h>
#include "canopus_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_PROTO_MAGIC 0x43505254u /* "CPRT" */

/* Supervisor commands. */
enum canopus_command {
    CANOPUS_CMD_ECHO = 1,
    CANOPUS_CMD_INSTALL,
    CANOPUS_CMD_ENABLE,
    CANOPUS_CMD_DISABLE,
    CANOPUS_CMD_REMOVE,
    CANOPUS_CMD_UPDATE,
    CANOPUS_CMD_ROLLBACK,
    CANOPUS_CMD_QUERY_MODULE,
    CANOPUS_CMD_QUERY_DEVICE,
    CANOPUS_CMD_ENTER_SAFE_MODE,
};

struct canopus_proto_request_v1 {
    uint32_t magic;
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t command;
    uint32_t request_id;
    uint32_t payload_size;
    uint32_t flags;
};

struct canopus_proto_response_v1 {
    uint32_t magic;
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t request_id;
    uint32_t result_state; /* CANOPUS_RESULT_* */
    uint32_t payload_size;
    uint32_t flags;
};

#define CANOPUS_PROTO_REQUEST_SIZE  sizeof(struct canopus_proto_request_v1)
#define CANOPUS_PROTO_RESPONSE_SIZE sizeof(struct canopus_proto_response_v1)

/* Fixed max control payload; bulk data must use a separate stream. */
#define CANOPUS_PROTO_MAX_PAYLOAD 512u

/* Returns 0 when the request envelope is well-formed for the current ABI. */
int canopus_proto_validate_request(const struct canopus_proto_request_v1 *r,
                                   uint32_t buffer_capacity);
/* Returns 0 when the response envelope is well-formed. */
int canopus_proto_validate_response(const struct canopus_proto_response_v1 *r,
                                    uint32_t buffer_capacity);

void canopus_proto_response_init(struct canopus_proto_response_v1 *resp,
                                 uint32_t request_id, uint32_t result_state,
                                 uint32_t payload_size);

/* ---- pending-request state machine -------------------------------- */
/* A request_id is tracked so the manager can report real async state. */
#define CANOPUS_PENDING_MAX 8u

struct canopus_pending_request_v1 {
    uint32_t request_id;
    uint32_t command;
    uint32_t state;   /* one of CANOPUS_RESULT_ACCEPTED/QUEUED/RUNNING/... */
    uint32_t active;
};

struct canopus_pending_table_v1 {
    struct canopus_pending_request_v1 slots[CANOPUS_PENDING_MAX];
};

void canopus_pending_init(struct canopus_pending_table_v1 *t);
/* Accepts a new request id. Fails (-1) if the id is already pending. */
int canopus_pending_accept(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t command);
/* Advances an accepted request to a later state. Returns 0/-1. */
int canopus_pending_set_state(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t state);
/* Finds a pending request by id, or 0. */
const struct canopus_pending_request_v1 *canopus_pending_find(
    const struct canopus_pending_table_v1 *t, uint32_t request_id);
/* Marks terminal (COMPLETED/FAILED/DISALLOWED/REBOOT_REQUIRED). */
int canopus_pending_finish(struct canopus_pending_table_v1 *t,
                           uint32_t request_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_PROTOCOL_H */
