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
    CANOPUS_CMD_ACTIVATE,
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

/* Read-only Manager query payloads carried inside CPC2 responses. They are
 * byte-encoded little-endian records, never cast to packed C structs. */
#define CANOPUS_QUERY_DEVICE_MAGIC 0x43514431u /* "CQD1" */
#define CANOPUS_QUERY_DEVICE_SIZE  32u
#define CANOPUS_QUERY_MODULE_MAGIC 0x43514D31u /* "CQM1" */
#define CANOPUS_QUERY_MODULE_SIZE  64u

/* Returns 0 when the request envelope is well-formed for the current ABI. */
int canopus_proto_validate_request(const struct canopus_proto_request_v1 *r,
                                   uint32_t buffer_capacity);
/* Returns 0 when the response envelope is well-formed. */
int canopus_proto_validate_response(const struct canopus_proto_response_v1 *r,
                                    uint32_t buffer_capacity);

void canopus_proto_response_init(struct canopus_proto_response_v1 *resp,
                                 uint32_t request_id, uint32_t result_state,
                                 uint32_t payload_size);

/* ------------------------------------------------------------------ */
/* v2 transport envelope (CAN-P0-008)                                  */
/*                                                                     */
/* The single versioned wire format for /dev/canopus. Legacy CPC1/CPS1  */
/* remain only as the compatibility path; new operations are v2. The    */
/* codec is byte-level and bounded: a wire buffer is never dereferenced */
/* as a packed C struct.                                                */
/* ------------------------------------------------------------------ */

#define CANOPUS_TRANSPORT_V2_MAGIC      0x43504332u /* "CPC2" */
#define CANOPUS_TRANSPORT_V2_HEADER_SIZE 36u
/* CAN-P2-003: only these request flag bits are known; any other bit makes a
 * v2 record fail closed instead of silently carrying unknown semantics. */
#define CANOPUS_TRANSPORT_V2_FLAGS_KNOWN 0u

enum canopus_transport_v2_kind {
    CANOPUS_TRANSPORT_V2_REQUEST = 1,
    CANOPUS_TRANSPORT_V2_RESPONSE,
    CANOPUS_TRANSPORT_V2_EVENT,
};

/* Decodes a v2 REQUEST record from a wire buffer into a request envelope.
 * Validates magic, header/total sizes, message kind, ABI major/minor,
 * payload bound and non-zero request id. Sets *payload_offset to where the
 * payload begins. Returns 0 on success, -1 on any malformed input. */
int canopus_transport_v2_decode_request(const uint8_t *buf, uint32_t len,
                                        struct canopus_proto_request_v1 *req,
                                        uint32_t *payload_offset);
/* Encodes a v2 REQUEST record. The input request uses the portable CPRT
 * envelope; the wire header is always CPC2. Returns bytes written or -1. */
int canopus_transport_v2_encode_request(const struct canopus_proto_request_v1 *req,
                                        const void *payload,
                                        uint8_t *out, uint32_t cap);
/* Decodes a complete v2 RESPONSE record. Returns its echoed opcode and payload
 * offset without exposing packed wire structs. */
int canopus_transport_v2_decode_response(const uint8_t *buf, uint32_t len,
                                         struct canopus_proto_response_v1 *resp,
                                         uint32_t *opcode,
                                         uint32_t *payload_offset);
/* Encodes a v2 RESPONSE record (header + payload) into `out`. `opcode`
 * echoes the request's opcode. Returns the number of bytes written, or -1
 * when the buffer is too small or the input is invalid. */
int canopus_transport_v2_encode_response(const struct canopus_proto_response_v1 *resp,
                                         uint32_t opcode,
                                         const void *payload,
                                         uint32_t payload_len,
                                         uint8_t *out, uint32_t cap);

/* ---- pending-request state machine (CAN-P1-002) -------------------- */
/* A request_id is tracked so the manager can report real async state.
 * Legal edges (see canopus_pending.c): ACCEPTED -> QUEUED/RUNNING/terminal,
 * QUEUED -> RUNNING/terminal, RUNNING -> terminal. Backwards, terminal ->
 * anything, REJECTED and unknown states are rejected. A terminal record is
 * retained (active stays 1) until the client ACKs it, so a late query still
 * observes the outcome. Slots carry the boot_id that accepted them; after a
 * boot_id change the old slots are stale and reject further operations. */
#define CANOPUS_PENDING_MAX 8u

struct canopus_pending_request_v1 {
    uint32_t request_id;
    uint32_t command;
    uint32_t state;   /* one of CANOPUS_RESULT_ACCEPTED/QUEUED/RUNNING/... */
    uint32_t active;  /* 1 until the terminal record is acked */
    uint32_t boot_id; /* boot that accepted this request */
    uint32_t error;   /* stable error when state == FAILED */
};

struct canopus_pending_table_v1 {
    uint32_t boot_id; /* current boot; slots from an older boot are stale */
    struct canopus_pending_request_v1 slots[CANOPUS_PENDING_MAX];
};

void canopus_pending_init(struct canopus_pending_table_v1 *t);
/* Records the current boot id. Requests accepted under an older boot are
 * rejected by set_state/finish/ack. */
void canopus_pending_set_boot(struct canopus_pending_table_v1 *t,
                              uint32_t boot_id);
/* Accepts a new request id (must be non-zero and not already pending). */
int canopus_pending_accept(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t command);
/* Advances an accepted request through a legal transition. Returns 0/-1. */
int canopus_pending_set_state(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t state);
/* Sets the per-request stable error (e.g. on FAILED). */
int canopus_pending_set_error(struct canopus_pending_table_v1 *t,
                              uint32_t request_id, uint32_t error);
/* Marks the request with an explicit terminal result. The terminal record
 * is RETAINED until canopus_pending_ack, so a query after completion still
 * finds it. Fails on a non-terminal result, a stale boot, or a terminal
 * result that contradicts an already-terminal state. */
int canopus_pending_finish(struct canopus_pending_table_v1 *t,
                           uint32_t request_id, uint32_t result);
/* Clears the terminal record after the client acknowledged it. */
int canopus_pending_ack(struct canopus_pending_table_v1 *t,
                        uint32_t request_id);
/* Finds a pending request by id, or 0 (read-only). */
const struct canopus_pending_request_v1 *canopus_pending_find(
    const struct canopus_pending_table_v1 *t, uint32_t request_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_PROTOCOL_H */
