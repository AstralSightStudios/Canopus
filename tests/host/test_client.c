/* Host tests: CPC2 /dev/canopus client and real supervisor endpoint. */
#include "canopus_test.h"
#include "canopus_client.h"
#include "canopus_supervisor.h"
#include "canopus_memory.h"

struct fake_client_device {
    struct canopus_supervisor_v1 supervisor;
    int open_calls;
    int close_calls;
    int short_write;
    int short_read;
};

static int32_t device_open(void *cookie, const char *path)
{
    struct fake_client_device *device = (struct fake_client_device *)cookie;
    device->open_calls++;
    return path != 0 && path[0] == '/' ? 7 : -1;
}

static int32_t device_close(void *cookie, int32_t fd)
{
    struct fake_client_device *device = (struct fake_client_device *)cookie;
    device->close_calls++;
    return fd == 7 ? 0 : -1;
}

static int32_t device_read(void *cookie, int32_t fd, void *buffer,
                           uint32_t count)
{
    struct fake_client_device *device = (struct fake_client_device *)cookie;
    int32_t rc;
    if (fd != 7) return -1;
    rc = canopus_supervisor_device_read(&device->supervisor, buffer, count);
    if (device->short_read && rc > 0) return rc - 1;
    return rc;
}

static int32_t device_write(void *cookie, int32_t fd, const void *buffer,
                            uint32_t count)
{
    struct fake_client_device *device = (struct fake_client_device *)cookie;
    int32_t rc;
    if (fd != 7) return -1;
    rc = canopus_supervisor_device_write(&device->supervisor, buffer, count);
    if (device->short_write && rc > 0) return rc - 1;
    return rc;
}

static const struct canopus_client_io_v1 device_io = {
    sizeof(struct canopus_client_io_v1),
    CANOPUS_CLIENT_ABI_MAJOR,
    CANOPUS_CLIENT_ABI_MINOR,
    device_open,
    device_close,
    device_read,
    device_write,
};

static void request_init(struct canopus_proto_request_v1 *request,
                         uint32_t command, uint32_t request_id,
                         uint32_t payload_size)
{
    canopus_memset(request, 0, sizeof(*request));
    request->magic = CANOPUS_PROTO_MAGIC;
    request->struct_size = CANOPUS_PROTO_REQUEST_SIZE;
    request->abi_major = CANOPUS_ABI_MAJOR;
    request->abi_minor = CANOPUS_ABI_MINOR;
    request->command = command;
    request->request_id = request_id;
    request->payload_size = payload_size;
}

TEST(client_cpc2_roundtrip_reaches_supervisor)
{
    struct fake_client_device device;
    struct canopus_client_v1 client;
    struct canopus_proto_request_v1 request;
    struct canopus_proto_response_v1 response;
    const char payload[] = "hello";

    canopus_memset(&device, 0, sizeof(device));
    CHECK(canopus_supervisor_init(&device.supervisor, 9, 0, 0) == 0);
    CHECK(canopus_client_init(&client, &device_io, &device) == CANOPUS_CLIENT_OK);
    CHECK(canopus_client_open(&client) == CANOPUS_CLIENT_OK);
    request_init(&request, CANOPUS_CMD_ECHO, 41, sizeof(payload));
    CHECK(canopus_client_transport(&request, payload, &response, &client) == 0);
    CHECK(response.request_id == 41);
    CHECK(response.result_state == CANOPUS_RESULT_COMPLETED);
    CHECK(response.payload_size == 0);
    CHECK(canopus_client_close(&client) == CANOPUS_CLIENT_OK);
    CHECK(device.open_calls == 1);
    CHECK(device.close_calls == 1);
}

TEST(client_fails_closed_on_short_atomic_transfers)
{
    struct fake_client_device device;
    struct canopus_client_v1 client;
    struct canopus_proto_request_v1 request;
    struct canopus_proto_response_v1 response;

    canopus_memset(&device, 0, sizeof(device));
    CHECK(canopus_supervisor_init(&device.supervisor, 1, 0, 0) == 0);
    CHECK(canopus_client_init(&client, &device_io, &device) == CANOPUS_CLIENT_OK);
    CHECK(canopus_client_open(&client) == CANOPUS_CLIENT_OK);
    request_init(&request, CANOPUS_CMD_ECHO, 1, 0);

    device.short_write = 1;
    CHECK(canopus_client_transport(&request, 0, &response, &client) == -1);
    CHECK(response.magic == 0);
    device.short_write = 0;
    device.short_read = 1;
    request.request_id = 2;
    CHECK(canopus_client_transport(&request, 0, &response, &client) == -1);
    CHECK(response.magic == 0);
}

TEST(client_rejects_malformed_and_mismatched_responses)
{
    struct canopus_proto_request_v1 request;
    struct canopus_proto_request_v1 decoded;
    struct canopus_proto_response_v1 response;
    struct canopus_proto_response_v1 decoded_response;
    uint8_t wire[CANOPUS_CLIENT_WIRE_CAPACITY];
    uint32_t offset = 0;
    uint32_t opcode = 0;
    int len;

    request_init(&request, CANOPUS_CMD_QUERY_DEVICE, 77, 0);
    len = canopus_transport_v2_encode_request(&request, 0, wire, sizeof(wire));
    CHECK(len == (int)CANOPUS_TRANSPORT_V2_HEADER_SIZE);
    CHECK(canopus_transport_v2_decode_request(wire, (uint32_t)len, &decoded,
                                              &offset) == 0);
    CHECK(decoded.command == CANOPUS_CMD_QUERY_DEVICE);
    CHECK(decoded.request_id == 77);
    CHECK(offset == CANOPUS_TRANSPORT_V2_HEADER_SIZE);

    canopus_proto_response_init(&response, 77, CANOPUS_RESULT_ACCEPTED, 0);
    len = canopus_transport_v2_encode_response(&response,
                                               CANOPUS_CMD_QUERY_DEVICE,
                                               0, 0, wire, sizeof(wire));
    CHECK(len == (int)CANOPUS_TRANSPORT_V2_HEADER_SIZE);
    CHECK(canopus_transport_v2_decode_response(wire, (uint32_t)len,
                                               &decoded_response, &opcode,
                                               &offset) == 0);
    CHECK(opcode == CANOPUS_CMD_QUERY_DEVICE);
    CHECK(decoded_response.request_id == 77);
    wire[6] = CANOPUS_TRANSPORT_V2_EVENT;
    CHECK(canopus_transport_v2_decode_response(wire, (uint32_t)len,
                                               &decoded_response, &opcode,
                                               &offset) == -1);
    wire[6] = CANOPUS_TRANSPORT_V2_RESPONSE;
    wire[24] = 0x80;
    CHECK(canopus_transport_v2_decode_response(wire, (uint32_t)len,
                                               &decoded_response, &opcode,
                                               &offset) == -1);
}

TEST(client_validates_lifecycle_and_io_abi)
{
    struct fake_client_device device;
    struct canopus_client_v1 client;
    struct canopus_client_io_v1 bad = device_io;

    canopus_memset(&device, 0, sizeof(device));
    bad.struct_size = 0;
    CHECK(canopus_client_init(&client, &bad, &device) ==
          CANOPUS_CLIENT_ERR_ARGUMENT);
    CHECK(canopus_client_init(&client, &device_io, &device) ==
          CANOPUS_CLIENT_OK);
    CHECK(canopus_client_close(&client) == CANOPUS_CLIENT_ERR_STATE);
    CHECK(canopus_client_open(&client) == CANOPUS_CLIENT_OK);
    CHECK(canopus_client_open(&client) == CANOPUS_CLIENT_ERR_STATE);
}

static const struct test_registry client_tests[] = {
    { "client_cpc2_roundtrip_reaches_supervisor",
      client_cpc2_roundtrip_reaches_supervisor_wrapper },
    { "client_fails_closed_on_short_atomic_transfers",
      client_fails_closed_on_short_atomic_transfers_wrapper },
    { "client_rejects_malformed_and_mismatched_responses",
      client_rejects_malformed_and_mismatched_responses_wrapper },
    { "client_validates_lifecycle_and_io_abi",
      client_validates_lifecycle_and_io_abi_wrapper },
};

int run_client_tests(void)
{
    RUN_TESTS(client_tests, sizeof(client_tests) / sizeof(client_tests[0]));
}
