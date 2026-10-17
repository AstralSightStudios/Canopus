#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_installer_bundle.h"
#include "canopus_services.h"
#include <string.h>

static struct { char path[96]; uint8_t data[1024]; uint32_t len; } files[4];
static uint8_t firmware_sha[32];
static int fail_write;
static int upload_open(void *cookie, const char *path, uint32_t flags)
{
    unsigned i;
    (void)cookie; (void)flags;
    for (i = 0; i < 4; i++) {
        if (strcmp(files[i].path, path) == 0 || files[i].path[0] == 0) {
            strcpy(files[i].path, path); files[i].len = 0; return (int)i;
        }
    }
    return -1;
}
static int32_t upload_write(void *cookie, int fd, const void *data, uint32_t len)
{
    (void)cookie;
    if (fail_write) return -1;
    if (len > 17u) len = 17u;
    memcpy(files[fd].data + files[fd].len, data, len);
    files[fd].len += len;
    return (int32_t)len;
}
static int upload_close(void *cookie, int fd) { (void)cookie; (void)fd; return 0; }
static int upload_remove(void *cookie, const char *path)
{
    unsigned i;
    (void)cookie;
    for (i = 0; i < 4; i++) if (strcmp(files[i].path, path) == 0) files[i].path[0] = 0;
    return 0;
}
static int upload_rename(void *cookie, const char *from, const char *to)
{
    unsigned i;
    (void)cookie;
    for (i = 0; i < 4; i++) if (strcmp(files[i].path, from) == 0) {
        strcpy(files[i].path, to); return 0;
    }
    return -1;
}
static int upload_validate(void *cookie, const void *receipt)
{
    (void)cookie;
    return canopus_install_receipt_validate(receipt, "host-upload", firmware_sha);
}
static int upload_verify(void *cookie, const char *id, uint32_t *version, uint32_t *lifecycle)
{
    unsigned i;
    (void)cookie; (void)id;
    for (i = 0; i < 4; i++) if (strstr(files[i].path, ".ko.next") != 0) {
        if (files[i].len != 600u || files[i].data[599] != 0x5a) return -1;
        *version = 2u; *lifecycle = 0u; return 0;
    }
    return -1;
}
static const struct canopus_sup_platform_v1 upload_platform = {
    .target_id = "host-upload", .file_open = upload_open, .file_write = upload_write,
    .file_close = upload_close, .file_remove = upload_remove, .file_rename = upload_rename,
    .validate_receipt = upload_validate, .stage_verify = upload_verify,
};
static uint32_t rpc(struct canopus_supervisor_v1 *sup, uint32_t op, const void *payload, uint32_t len)
{
    uint8_t request[548], response[548];
    struct canopus_proto_request_v1 req = { CANOPUS_PROTO_MAGIC, CANOPUS_PROTO_REQUEST_SIZE,
        CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0, 1u, 0, 0 };
    struct canopus_proto_response_v1 resp;
    uint32_t opcode, offset;
    int n;
    req.command = op; req.payload_size = len;
    n = canopus_transport_v2_encode_request(&req, payload, request, sizeof(request));
    CHECK(n > 0);
    n = canopus_supervisor_handle_l2_packet(sup, request, (uint32_t)n, response, sizeof(response));
    CHECK(n > 0);
    CHECK_EQ(canopus_transport_v2_decode_response(response, (uint32_t)n, &resp, &opcode, &offset), 0);
    CHECK_EQ(opcode, op);
    return resp.result_state;
}
static void setup(struct canopus_supervisor_v1 *sup, struct canopus_install_receipt_v1 *receipt)
{
    memset(files, 0, sizeof(files)); memset(receipt, 0, sizeof(*receipt)); fail_write = 0;
    canopus_supervisor_init(sup, 1u, &upload_platform, 0);
    receipt->magic = CANOPUS_INSTALL_RECEIPT_MAGIC;
    receipt->version = CANOPUS_INSTALL_RECEIPT_VERSION;
    receipt->header_size = sizeof(*receipt); receipt->module_version = 2u; receipt->artifact_size = 600u;
    memcpy(receipt->module_id, "native_probe", 13u);
    memcpy(receipt->target_id, "host-upload", 12u);
}
TEST(native_upload_orders_chunks_and_publishes_only_on_finish)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    uint8_t chunk[512];
    setup(&sup, &receipt);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_COMPLETED);
    memset(chunk, 0x5a, sizeof(chunk)); memset(chunk, 0, 4);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    chunk[0] = 1u;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_WRITE, chunk, 512u), CANOPUS_RESULT_DISALLOWED);
    chunk[0] = 0u;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_WRITE, chunk, 512u), CANOPUS_RESULT_COMPLETED);
    CHECK(strstr(files[0].path, ".upload") != 0);
    chunk[0] = 0xfcu; chunk[1] = 1u;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_WRITE, chunk, 97u), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_WRITE, chunk, 96u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.upload.active, 0u);
    CHECK(strcmp(files[0].path, "/data/canopus/inbox/native_probe.ko.next") == 0);
    CHECK_EQ(files[1].len, CANOPUS_INSTALL_RECEIPT_SIZE);
    CHECK_EQ(sup.module_count, 0u);
}
TEST(native_upload_rejects_wrong_target_and_cleans_failed_transfers)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    uint8_t chunk[8] = {0};
    setup(&sup, &receipt);
    receipt.target_id[0] = 'X';
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(files[0].path[0], 0);
    receipt.target_id[0] = 'h';
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_COMPLETED);
    fail_write = 1;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_WRITE, chunk, sizeof(chunk)), CANOPUS_RESULT_FAILED);
    CHECK_EQ(files[0].path[0], 0);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    fail_write = 0;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.upload.active, 0u);
    CHECK_EQ(files[0].path[0], 0);
}
static struct test_registry upload_tests[] = {
    {"native_upload_orders_chunks_and_publishes_only_on_finish", native_upload_orders_chunks_and_publishes_only_on_finish_wrapper},
    {"native_upload_rejects_wrong_target_and_cleans_failed_transfers", native_upload_rejects_wrong_target_and_cleans_failed_transfers_wrapper},
};
int run_supervisor_upload_tests(void) { RUN_TESTS(upload_tests, sizeof(upload_tests) / sizeof(upload_tests[0])); }
