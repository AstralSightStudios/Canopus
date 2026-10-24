#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_installer_bundle.h"
#include "canopus_services.h"
#include <string.h>

#define UPLOAD_FILES 16u
#define UPLOAD_DIRS 8u
static struct {
    char path[160];
    uint8_t data[1024];
    uint32_t len, pos;
    int opened;
} files[UPLOAD_FILES];
static char dirs[UPLOAD_DIRS][160];
static uint8_t firmware_sha[32];
static int fail_open, fail_read, fail_write, fail_close, fail_rename, fail_stat, fail_mkdir;
static int find_file(const char *path)
{
    unsigned i;
    for (i = 0; i < UPLOAD_FILES; i++)
        if (strcmp(files[i].path, path) == 0) return (int)i;
    return -1;
}
static int upload_open(void *cookie, const char *path, uint32_t flags)
{
    unsigned i;
    int fd = find_file(path);
    (void)cookie;
    if (fail_open) return -5;
    if ((flags & CANOPUS_FS_WRITE) == 0u) {
        if (fd < 0) return -2;
        files[fd].pos = 0;
        files[fd].opened = 1;
        return fd;
    }
    if (fd < 0) {
        for (i = 0; i < UPLOAD_FILES; i++) if (files[i].path[0] == 0) break;
        if (i == UPLOAD_FILES) return -28;
        fd = (int)i;
    }
    strcpy(files[fd].path, path);
    files[fd].len = files[fd].pos = 0;
    files[fd].opened = 1;
    return fd;
}
static int32_t upload_read(void *cookie, int fd, void *data, uint32_t len)
{
    (void)cookie;
    if (fail_read || fd < 0 || (unsigned)fd >= UPLOAD_FILES || !files[fd].opened) return -5;
    if (len > files[fd].len - files[fd].pos) len = files[fd].len - files[fd].pos;
    if (len > 2u) len = 2u; /* Exercise hash reads that return short chunks. */
    memcpy(data, files[fd].data + files[fd].pos, len);
    files[fd].pos += len;
    return (int32_t)len;
}
static int32_t upload_write(void *cookie, int fd, const void *data, uint32_t len)
{
    (void)cookie;
    if (fail_write || fd < 0 || (unsigned)fd >= UPLOAD_FILES || !files[fd].opened) return -5;
    if (len > 17u) len = 17u;
    if (len > sizeof(files[fd].data) - files[fd].len) return -28;
    memcpy(files[fd].data + files[fd].len, data, len);
    files[fd].len += len;
    return (int32_t)len;
}
static int upload_close(void *cookie, int fd)
{
    (void)cookie;
    if (fd < 0 || (unsigned)fd >= UPLOAD_FILES || !files[fd].opened) return -5;
    files[fd].opened = 0;
    return fail_close ? -5 : 0;
}
static int upload_remove(void *cookie, const char *path)
{
    int fd = find_file(path);
    (void)cookie;
    if (fd < 0) return -2;
    files[fd].path[0] = 0;
    return 0;
}
static int upload_rename(void *cookie, const char *from, const char *to)
{
    int fd = find_file(from), target = find_file(to);
    (void)cookie;
    if (fail_rename || fd < 0) return -5;
    if (target >= 0 && target != fd) files[target].path[0] = 0;
    strcpy(files[fd].path, to);
    return 0;
}
static int upload_stat(void *cookie, const char *path, void *out_stat)
{
    struct canopus_fs_stat *st = out_stat;
    unsigned i;
    int fd = find_file(path);
    (void)cookie;
    if (fail_stat) return -5;
    memset(st, 0, sizeof(*st));
    if (fd >= 0) { st->size = files[fd].len; return 0; }
    for (i = 0; i < UPLOAD_DIRS; i++) if (strcmp(dirs[i], path) == 0) {
        st->is_dir = 1u;
        return 0;
    }
    return -2;
}
static int upload_mkdir(void *cookie, const char *path)
{
    unsigned i;
    (void)cookie;
    if (fail_mkdir) return -5;
    if (find_file(path) >= 0) return -17;
    for (i = 0; i < UPLOAD_DIRS; i++) if (strcmp(dirs[i], path) == 0) return -17;
    for (i = 0; i < UPLOAD_DIRS; i++) if (dirs[i][0] == 0) {
        strcpy(dirs[i], path);
        return 0;
    }
    return -28;
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
    for (i = 0; i < UPLOAD_FILES; i++) if (strstr(files[i].path, ".ko.next") != 0) {
        if (files[i].len != 600u || files[i].data[599] != 0x5a) return -1;
        *version = 2u; *lifecycle = 0u; return 0;
    }
    return -1;
}
static const struct canopus_sup_platform_v1 upload_platform = {
    .target_id = "host-upload", .file_open = upload_open, .file_read = upload_read,
    .file_write = upload_write, .file_close = upload_close, .file_remove = upload_remove,
    .file_rename = upload_rename, .file_stat = upload_stat, .dir_mkdir = upload_mkdir,
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
    memset(files, 0, sizeof(files)); memset(dirs, 0, sizeof(dirs));
    memset(receipt, 0, sizeof(*receipt));
    fail_open = fail_read = fail_write = fail_close = fail_rename = fail_stat = fail_mkdir = 0;
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
/* Published SHA-256 vectors keep the receiver checks independent of its hash code. */
static const uint8_t sha_abc[32] = {
    0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
};
static const uint8_t sha_hello[32] = {
    0xb9,0x4d,0x27,0xb9,0x93,0x4d,0x3e,0x08,0xa5,0x2e,0x52,0xd7,0xda,0x7d,0xab,0xfa,
    0xc4,0x84,0xef,0xe3,0x7a,0x53,0x80,0xee,0x90,0x88,0xf7,0xac,0xe2,0xef,0xcd,0xe9
};
static const uint8_t sha_empty[32] = {
    0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
    0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55
};
static const uint8_t binary_data[] = {0x00,0x80,0xff,0x01,0xfe,0x7f,0x00,0x42};
/* Independently computed with Python hashlib.sha256(bytes([...])). */
static const uint8_t sha_binary[32] = {
    0x2e,0xf5,0xe3,0x61,0xd4,0x33,0x36,0x2e,0xac,0xbd,0x1e,0x77,0xec,0x62,0xf9,0x4f,
    0x7c,0x1e,0x63,0x83,0x43,0x58,0x74,0x68,0xba,0xbd,0xf8,0xe6,0xb0,0x5d,0x32,0xf8
};
static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value; out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16); out[3] = (uint8_t)(value >> 24);
}
static uint32_t resource_payload(uint8_t *out, const void *name, uint32_t name_len,
                                 uint32_t size, const uint8_t sha[32])
{
    out[0] = (uint8_t)name_len;
    memcpy(out + 1u, name, name_len);
    put_u32(out + 1u + name_len, size);
    memcpy(out + 5u + name_len, sha, 32u);
    return 37u + name_len;
}
static uint32_t resource_begin(struct canopus_supervisor_v1 *sup, const char *name,
                               uint32_t size, const uint8_t sha[32])
{
    uint8_t payload[128];
    uint32_t len = resource_payload(payload, name, (uint32_t)strlen(name), size, sha);
    return rpc(sup, CANOPUS_CMD_STAGE_RES_BEGIN, payload, len);
}
static uint32_t resource_write(struct canopus_supervisor_v1 *sup, uint32_t offset,
                               const void *data, uint32_t len)
{
    uint8_t payload[512];
    put_u32(payload, offset);
    memcpy(payload + 4u, data, len);
    return rpc(sup, CANOPUS_CMD_STAGE_RES_WRITE, payload, len + 4u);
}
static void finish_artifact(struct canopus_supervisor_v1 *sup,
                            const struct canopus_install_receipt_v1 *receipt)
{
    uint8_t chunk[204];
    uint32_t offset;
    CHECK_EQ(rpc(sup, CANOPUS_CMD_STAGE_BEGIN, receipt, sizeof(*receipt)), CANOPUS_RESULT_COMPLETED);
    memset(chunk + 4u, 0x5a, 200u);
    for (offset = 0; offset < 600u; offset += 200u) {
        put_u32(chunk, offset);
        CHECK_EQ(rpc(sup, CANOPUS_CMD_STAGE_WRITE, chunk, sizeof(chunk)), CANOPUS_RESULT_COMPLETED);
    }
    CHECK_EQ(rpc(sup, CANOPUS_CMD_STAGE_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
}
static void ready_resource(struct canopus_supervisor_v1 *sup,
                            struct canopus_install_receipt_v1 *receipt)
{
    setup(sup, receipt);
    finish_artifact(sup, receipt);
    CHECK_EQ(resource_begin(sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
}
static void check_no_partials(void)
{
    unsigned i;
    for (i = 0; i < UPLOAD_FILES; i++) {
        CHECK(strstr(files[i].path, ".upload") == 0);
        CHECK_EQ(files[i].opened, 0);
    }
}
static void check_resource(const char *path, const void *data, uint32_t len)
{
    int fd = find_file(path);
    CHECK(fd >= 0);
    if (fd < 0) return;
    CHECK_EQ(files[fd].len, len);
    CHECK(memcmp(files[fd].data, data, len) == 0);
    CHECK_EQ(files[fd].opened, 0);
}
TEST(resource_upload_matches_sender_sequence_and_flat_data_contract)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    struct canopus_fs_stat st;
    CHECK_EQ(CANOPUS_CMD_STAGE_RES_BEGIN, 26u);
    CHECK_EQ(CANOPUS_CMD_STAGE_RES_WRITE, 27u);
    CHECK_EQ(CANOPUS_CMD_STAGE_RES_FINISH, 28u);
    setup(&sup, &receipt);
    /* Exactly 15,16,16,16,17,26,27,27,28 for the first resource. */
    finish_artifact(&sup, &receipt);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(upload_stat(0, "/data/canopus/m.native_probe", &st), 0);
    CHECK_EQ(st.is_dir, 1u);
    CHECK(find_file("/data/canopus/m.native_probe/logo.bin.upload") >= 0);
    CHECK_EQ(resource_write(&sup, 0u, "a", 1u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 1u, "bc", 2u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe/logo.bin", "abc", 3u);
    CHECK_EQ(resource_begin(&sup, "splash-2.bin", 11u, sha_hello), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 0u, "hello world", 11u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe/splash-2.bin", "hello world", 11u);
    check_resource("/data/canopus/m.native_probe/logo.bin", "abc", 3u);
    CHECK_EQ(resource_begin(&sup, "binary.bin", sizeof(binary_data), sha_binary), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 0u, binary_data, sizeof(binary_data)), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe/binary.bin", binary_data, sizeof(binary_data));
    CHECK_EQ(sup.module_count, 0u);
    check_no_partials();
}
TEST(resource_upload_accepts_maximum_name_and_empty_resource)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    char name[CANOPUS_FS_NAME_MAX + 1u], path[160];
    setup(&sup, &receipt);
    finish_artifact(&sup, &receipt);
    memset(name, 'a', CANOPUS_FS_NAME_MAX); name[CANOPUS_FS_NAME_MAX] = 0;
    CHECK_EQ(resource_begin(&sup, name, 0u, sha_empty), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    strcpy(path, "/data/canopus/m.native_probe/"); strcat(path, name);
    check_resource(path, "", 0u);
    check_no_partials();
}
TEST(resource_upload_requires_verified_artifact_context)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    setup(&sup, &receipt);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_no_partials();
    finish_artifact(&sup, &receipt);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    check_no_partials();
}
TEST(resource_upload_rejects_unsafe_names_and_malformed_begin)
{
    static const char *bad_names[] = { "", ".", "..", ".hidden", "../logo.bin", "dir/logo.bin",
        "dir\\logo.bin", "a b", "a:b", "x\x7f", "x\x80" };
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    uint8_t payload[128];
    char long_name[CANOPUS_FS_NAME_MAX + 2u];
    unsigned i;
    uint32_t len;
    setup(&sup, &receipt); finish_artifact(&sup, &receipt);
    for (i = 0; i < sizeof(bad_names) / sizeof(bad_names[0]); i++) {
        CHECK_EQ(resource_begin(&sup, bad_names[i], 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
        check_no_partials();
    }
    memset(long_name, 'a', sizeof(long_name) - 1u); long_name[sizeof(long_name) - 1u] = 0;
    CHECK_EQ(resource_begin(&sup, long_name, 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    len = resource_payload(payload, "logo.bin", 8u, 3u, sha_abc);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, 0, 0), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, payload, 1u), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, payload, len - 1u), CANOPUS_RESULT_DISALLOWED);
    payload[len] = 0;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, payload, len + 1u), CANOPUS_RESULT_DISALLOWED);
    payload[2] = 0;
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, payload, len), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_BEGIN, 0, 0), CANOPUS_RESULT_DISALLOWED);
    check_no_partials();
}
TEST(resource_upload_rejects_bad_writes_and_removes_partial)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    uint8_t short_payload[4] = {0};
    unsigned i;
    for (i = 0; i < 4u; i++) {
        ready_resource(&sup, &receipt);
        CHECK_EQ(resource_write(&sup, 0u, "a", 1u), CANOPUS_RESULT_COMPLETED);
        if (i == 0u)
            CHECK_EQ(resource_write(&sup, 0u, "bc", 2u), CANOPUS_RESULT_DISALLOWED);
        else if (i == 1u)
            CHECK_EQ(resource_write(&sup, 1u, "bcd", 3u), CANOPUS_RESULT_DISALLOWED);
        else
            CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_WRITE, short_payload, i == 2u ? 4u : 3u), CANOPUS_RESULT_DISALLOWED);
        CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
        check_no_partials();
        CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    }
}
TEST(resource_upload_rejects_short_or_malformed_finish)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    uint8_t extra = 0;
    ready_resource(&sup, &receipt);
    CHECK_EQ(resource_write(&sup, 0u, "a", 1u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    check_no_partials();
    ready_resource(&sup, &receipt);
    CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, &extra, 1u), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
    check_no_partials();
}
TEST(resource_upload_verifies_staged_bytes_size_and_sha256)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    int fd;
    unsigned i;
    for (i = 0; i < 4u; i++) {
        ready_resource(&sup, &receipt);
        CHECK_EQ(resource_write(&sup, 0u, i == 0u ? "abd" : "abc", 3u), CANOPUS_RESULT_COMPLETED);
        fd = find_file("/data/canopus/m.native_probe/logo.bin.upload");
        CHECK(fd >= 0);
        if (fd < 0) continue;
        if (i == 1u) files[fd].data[0] = 'z';
        if (i == 2u) files[fd].len = 2u;
        if (i == 3u) { files[fd].len = 4u; files[fd].data[3] = 'd'; }
        CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_FAILED);
        CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
        check_no_partials();
    }
}
TEST(resource_upload_cleans_io_failures)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    unsigned i;
    for (i = 0; i < 7u; i++) {
        setup(&sup, &receipt); finish_artifact(&sup, &receipt);
        if (i < 3u) {
            if (i == 0u) fail_open = 1;
            if (i == 1u) { fail_stat = 1; (void)upload_mkdir(0, "/data/canopus/m.native_probe"); }
            if (i == 2u) fail_mkdir = 1;
            CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_FAILED);
        } else {
            CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
            if (i == 3u) fail_write = 1;
            CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), i == 3u ? CANOPUS_RESULT_FAILED : CANOPUS_RESULT_COMPLETED);
            if (i == 4u) fail_close = 1;
            if (i == 5u) fail_read = 1;
            if (i == 6u) fail_rename = 1;
            if (i != 3u) CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_FAILED);
        }
        CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
        check_no_partials();
    }
}
TEST(resource_upload_overwrites_collisions)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    int fd;
    /* An existing target is replaced on install (re-install/upgrade). */
    setup(&sup, &receipt); finish_artifact(&sup, &receipt);
    fd = upload_open(0, "/data/canopus/m.native_probe/logo.bin", CANOPUS_FS_WRITE);
    CHECK(fd >= 0);
    CHECK_EQ(upload_write(0, fd, "old", 3u), 3);
    CHECK_EQ(upload_close(0, fd), 0);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe/logo.bin", "abc", 3u);
    check_no_partials();
    /* A stale temp from a crashed attempt is reused, not rejected. */
    setup(&sup, &receipt); finish_artifact(&sup, &receipt);
    fd = upload_open(0, "/data/canopus/m.native_probe/logo.bin.upload", CANOPUS_FS_WRITE);
    CHECK(fd >= 0);
    CHECK_EQ(upload_write(0, fd, "stale", 5u), 5);
    CHECK_EQ(upload_close(0, fd), 0);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe/logo.bin", "abc", 3u);
    check_no_partials();
    /* A module path occupied by a plain file is still a hard failure. */
    setup(&sup, &receipt); finish_artifact(&sup, &receipt);
    fd = upload_open(0, "/data/canopus/m.native_probe", CANOPUS_FS_WRITE);
    CHECK(fd >= 0);
    CHECK_EQ(upload_write(0, fd, "old", 3u), 3);
    CHECK_EQ(upload_close(0, fd), 0);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_FAILED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.native_probe", "old", 3u);
}
TEST(resource_upload_abort_and_new_artifact_replace_context)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_install_receipt_v1 receipt;
    ready_resource(&sup, &receipt);
    CHECK_EQ(resource_write(&sup, 0u, "a", 1u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_no_partials();
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(resource_write(&sup, 1u, "bc", 2u), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_DISALLOWED);
    ready_resource(&sup, &receipt);
    CHECK_EQ(resource_write(&sup, 0u, "a", 1u), CANOPUS_RESULT_COMPLETED);
    memset(receipt.module_id, 0, sizeof(receipt.module_id));
    memcpy(receipt.module_id, "corona", 7u);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_BEGIN, &receipt, sizeof(receipt)), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin.upload"), -1);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_ABORT, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_no_partials();
    finish_artifact(&sup, &receipt);
    CHECK_EQ(resource_begin(&sup, "logo.bin", 3u, sha_abc), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resource_write(&sup, 0u, "abc", 3u), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(rpc(&sup, CANOPUS_CMD_STAGE_RES_FINISH, 0, 0), CANOPUS_RESULT_COMPLETED);
    check_resource("/data/canopus/m.corona/logo.bin", "abc", 3u);
    CHECK_EQ(find_file("/data/canopus/m.native_probe/logo.bin"), -1);
    check_no_partials();
}
static struct test_registry upload_tests[] = {
    {"native_upload_orders_chunks_and_publishes_only_on_finish", native_upload_orders_chunks_and_publishes_only_on_finish_wrapper},
    {"native_upload_rejects_wrong_target_and_cleans_failed_transfers", native_upload_rejects_wrong_target_and_cleans_failed_transfers_wrapper},
    {"resource_upload_matches_sender_sequence_and_flat_data_contract", resource_upload_matches_sender_sequence_and_flat_data_contract_wrapper},
    {"resource_upload_accepts_maximum_name_and_empty_resource", resource_upload_accepts_maximum_name_and_empty_resource_wrapper},
    {"resource_upload_requires_verified_artifact_context", resource_upload_requires_verified_artifact_context_wrapper},
    {"resource_upload_rejects_unsafe_names_and_malformed_begin", resource_upload_rejects_unsafe_names_and_malformed_begin_wrapper},
    {"resource_upload_rejects_bad_writes_and_removes_partial", resource_upload_rejects_bad_writes_and_removes_partial_wrapper},
    {"resource_upload_rejects_short_or_malformed_finish", resource_upload_rejects_short_or_malformed_finish_wrapper},
    {"resource_upload_verifies_staged_bytes_size_and_sha256", resource_upload_verifies_staged_bytes_size_and_sha256_wrapper},
    {"resource_upload_cleans_io_failures", resource_upload_cleans_io_failures_wrapper},
    {"resource_upload_overwrites_collisions", resource_upload_overwrites_collisions_wrapper},
    {"resource_upload_abort_and_new_artifact_replace_context", resource_upload_abort_and_new_artifact_replace_context_wrapper},
};
int run_supervisor_upload_tests(void) { RUN_TESTS(upload_tests, sizeof(upload_tests) / sizeof(upload_tests[0])); }
