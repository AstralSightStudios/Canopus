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

int canopus_client_transport(const struct canopus_proto_request_v1 *request,
                             const void *payload,
                             struct canopus_proto_response_v1 *response,
                             void *cookie)
{
    struct canopus_client_v1 *client = (struct canopus_client_v1 *)cookie;
    uint32_t opcode = 0;
    uint32_t payload_offset = 0;
    int encoded;
    int32_t transferred;
    if (client == 0 || response == 0 || client->fd < 0 ||
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
    (void)payload_offset;
    if (response->request_id != request->request_id ||
        opcode != request->command) {
        canopus_memset(response, 0, sizeof(*response));
        return -1;
    }
    return 0;
}
