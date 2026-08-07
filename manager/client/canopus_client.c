/* canopus_client.c — bounded, fail-closed CPC2 client core. */
#include "canopus_client.h"
#include "canopus_memory.h"

static int client_io_ok(const struct canopus_client_io_v1 *io)
{
    return io != 0 && io->struct_size == sizeof(*io) &&
           io->abi_major == CANOPUS_CLIENT_ABI_MAJOR &&
           io->abi_minor <= CANOPUS_CLIENT_ABI_MINOR && io->open != 0 &&
           io->close != 0 && io->read != 0 && io->write != 0;
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

int32_t canopus_client_init(struct canopus_client_v1 *client,
                            const struct canopus_client_io_v1 *io,
                            void *io_cookie)
{
    if (client == 0 || !client_io_ok(io)) {
        return CANOPUS_CLIENT_ERR_ARGUMENT;
    }
    canopus_memset(client, 0, sizeof(*client));
    client->io = io;
    client->io_cookie = io_cookie;
    client->fd = -1;
    return CANOPUS_CLIENT_OK;
}

int32_t canopus_client_open(struct canopus_client_v1 *client)
{
    int32_t fd;
    if (client == 0 || !client_io_ok(client->io)) {
        return CANOPUS_CLIENT_ERR_ARGUMENT;
    }
    if (client->fd >= 0) {
        return CANOPUS_CLIENT_ERR_STATE;
    }
    fd = client->io->open(client->io_cookie, CANOPUS_CLIENT_DEVICE_PATH);
    if (fd < 0) {
        return CANOPUS_CLIENT_ERR_IO;
    }
    client->fd = fd;
    return CANOPUS_CLIENT_OK;
}

int32_t canopus_client_close(struct canopus_client_v1 *client)
{
    int32_t rc;
    if (client == 0 || !client_io_ok(client->io)) {
        return CANOPUS_CLIENT_ERR_ARGUMENT;
    }
    if (client->fd < 0) {
        return CANOPUS_CLIENT_ERR_STATE;
    }
    rc = client->io->close(client->io_cookie, client->fd);
    client->fd = -1;
    return rc == 0 ? CANOPUS_CLIENT_OK : CANOPUS_CLIENT_ERR_IO;
}

static int client_exchange(struct canopus_client_v1 *client,
                           const struct canopus_proto_request_v1 *request,
                           const void *payload,
                           struct canopus_proto_response_v1 *response,
                           void *response_payload, uint32_t response_capacity,
                           uint32_t *response_size)
{
    uint32_t opcode = 0;
    uint32_t payload_offset = 0;
    int encoded;
    int32_t transferred;
    if (response_size != 0) {
        *response_size = 0u;
    }
    if (client == 0 || request == 0 || response == 0 || client->fd < 0 ||
        !client_io_ok(client->io)) {
        return -1;
    }
    canopus_memset(response, 0, sizeof(*response));
    encoded = canopus_transport_v2_encode_request(request, payload, client->tx,
                                                   sizeof(client->tx));
    if (encoded < 0) {
        return -1;
    }
    transferred = client->io->write(client->io_cookie, client->fd, client->tx,
                                    (uint32_t)encoded);
    if (transferred != encoded) {
        return -1;
    }
    transferred = client->io->read(client->io_cookie, client->fd, client->rx,
                                   sizeof(client->rx));
    if (transferred < 0 ||
        canopus_transport_v2_decode_response(client->rx,
                                             (uint32_t)transferred,
                                             response, &opcode,
                                             &payload_offset) != 0) {
        canopus_memset(response, 0, sizeof(*response));
        return -1;
    }
    if (response->request_id != request->request_id ||
        opcode != request->command ||
        payload_offset > (uint32_t)transferred ||
        response->payload_size > (uint32_t)transferred - payload_offset) {
        canopus_memset(response, 0, sizeof(*response));
        return -1;
    }
    if (response->payload_size != 0u && response_payload != 0) {
        if (response->payload_size > response_capacity) {
            canopus_memset(response, 0, sizeof(*response));
            return -1;
        }
        canopus_memcpy(response_payload, client->rx + payload_offset,
                       response->payload_size);
    }
    if (response_size != 0) {
        *response_size = response->payload_size;
    }
    return 0;
}

int canopus_client_transport(const struct canopus_proto_request_v1 *request,
                             const void *payload,
                             struct canopus_proto_response_v1 *response,
                             void *cookie)
{
    return client_exchange((struct canopus_client_v1 *)cookie, request, payload,
                           response, 0, 0u, 0);
}

static void init_query(struct canopus_proto_request_v1 *request,
                       uint32_t command, uint32_t request_id,
                       uint32_t payload_size)
{
    canopus_memset(request, 0, sizeof(*request));
    request->magic = CANOPUS_PROTO_MAGIC;
    request->struct_size = sizeof(*request);
    request->abi_major = CANOPUS_ABI_MAJOR;
    request->abi_minor = CANOPUS_ABI_MINOR;
    request->command = command;
    request->request_id = request_id;
    request->payload_size = payload_size;
}

int32_t canopus_client_query_device(
    struct canopus_client_v1 *client, uint32_t request_id,
    struct canopus_client_device_snapshot_v1 *snapshot)
{
    struct canopus_proto_request_v1 request;
    struct canopus_proto_response_v1 response;
    uint8_t payload[CANOPUS_QUERY_DEVICE_SIZE];
    uint32_t size;
    if (client == 0 || snapshot == 0 || request_id == 0u) {
        return CANOPUS_CLIENT_ERR_ARGUMENT;
    }
    init_query(&request, CANOPUS_CMD_QUERY_DEVICE, request_id, 0u);
    if (client_exchange(client, &request, 0, &response, payload,
                        sizeof(payload), &size) != 0) {
        return CANOPUS_CLIENT_ERR_IO;
    }
    if (response.result_state != CANOPUS_RESULT_COMPLETED ||
        size != CANOPUS_QUERY_DEVICE_SIZE ||
        read_u32(payload) != CANOPUS_QUERY_DEVICE_MAGIC ||
        read_u32(payload + 4u) != CANOPUS_QUERY_DEVICE_SIZE) {
        return CANOPUS_CLIENT_ERR_PROTOCOL;
    }
    snapshot->framework_revision = read_u32(payload + 8u);
    snapshot->safe_mode = read_u32(payload + 12u);
    snapshot->module_count = read_u32(payload + 16u);
    snapshot->safe_mode_reason = read_u32(payload + 20u);
    snapshot->error_code = (int32_t)read_u32(payload + 24u);
    snapshot->flags = read_u32(payload + 28u);
    return CANOPUS_CLIENT_OK;
}

int32_t canopus_client_query_module(
    struct canopus_client_v1 *client, uint32_t request_id, uint32_t slot,
    struct canopus_client_module_snapshot_v1 *snapshot)
{
    struct canopus_proto_request_v1 request;
    struct canopus_proto_response_v1 response;
    uint8_t request_payload[4];
    uint8_t payload[CANOPUS_QUERY_MODULE_SIZE];
    uint32_t size;
    uint32_t i;
    if (client == 0 || snapshot == 0 || request_id == 0u || slot >= 16u) {
        return CANOPUS_CLIENT_ERR_ARGUMENT;
    }
    write_u32(request_payload, slot);
    init_query(&request, CANOPUS_CMD_QUERY_MODULE, request_id,
               sizeof(request_payload));
    if (client_exchange(client, &request, request_payload, &response, payload,
                        sizeof(payload), &size) != 0) {
        return CANOPUS_CLIENT_ERR_IO;
    }
    if (response.result_state == CANOPUS_RESULT_DISALLOWED && size == 0u) {
        return CANOPUS_CLIENT_ERR_NOT_FOUND;
    }
    if (response.result_state != CANOPUS_RESULT_COMPLETED ||
        size != CANOPUS_QUERY_MODULE_SIZE ||
        read_u32(payload) != CANOPUS_QUERY_MODULE_MAGIC ||
        read_u32(payload + 4u) != CANOPUS_QUERY_MODULE_SIZE ||
        read_u32(payload + 8u) != slot) {
        return CANOPUS_CLIENT_ERR_PROTOCOL;
    }
    snapshot->slot = slot;
    snapshot->state = read_u32(payload + 12u);
    snapshot->lifecycle_class = read_u32(payload + 16u);
    snapshot->version = read_u32(payload + 20u);
    snapshot->flags = read_u32(payload + 24u);
    snapshot->activation_error = (int32_t)read_u32(payload + 60u);
    canopus_memcpy(snapshot->module_id, payload + 28u,
                   sizeof(snapshot->module_id));
    for (i = 0; i < sizeof(snapshot->module_id); i++) {
        if (snapshot->module_id[i] == '\0') {
            return CANOPUS_CLIENT_OK;
        }
    }
    canopus_memset(snapshot, 0, sizeof(*snapshot));
    return CANOPUS_CLIENT_ERR_PROTOCOL;
}
