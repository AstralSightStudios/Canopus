/* Execute the actual P65 provider through fake exact-address dispatch. */
#include "canopus_test.h"
#include "canopus_manager_p65_registry.h"
#include "canopus_supervisor_platform.h"
#include <string.h>
#include <stdint.h>
static uintptr_t fake_firmware_address(uint32_t address);
#define CANOPUS_SUP_P65_RESIDENT 1
#define FW(address, type) ((type)fake_firmware_address(address))
#include "../../manager/target/p65/canopus_p65_registry_firmware.c"

static uint8_t files[2][CANOPUS_P65_REGISTRY_RECORD_SIZE + 80];
static uint32_t sizes[2], positions[2];
static int present[2], error, open_count, close_count;
static int fail_write, fail_close, fail_unlink;
static int fake_open(const char *path, int flags, ...)
{
    int slot = strcmp(path, paths[0]) == 0 ? 0 : 1;
    if (!present[slot] && !(flags & 4)) { error = 2; return -1; }
    if (flags & 4) present[slot] = 1;
    positions[slot] = 0;
    open_count++;
    return slot;
}
static int fake_close(int fd)
{
    (void)fd;
    close_count++;
    return fail_close ? -1 : 0;
}
static int32_t fake_read(int fd, void *buffer, uint32_t size)
{
    uint32_t count = sizes[fd] - positions[fd];
    if (count > size) count = size;
    if (count > 7) count = 7;
    memcpy(buffer, files[fd] + positions[fd], count);
    positions[fd] += count;
    return (int32_t)count;
}
static int32_t fake_write(int fd, const void *buffer, uint32_t size)
{
    uint32_t count = size > 11 ? 11 : size;
    if (fail_write) return 0;
    memcpy(files[fd] + positions[fd], buffer, count);
    positions[fd] += count;
    sizes[fd] = positions[fd];
    return (int32_t)count;
}
static int fake_unlink(const char *path)
{
    int slot = strcmp(path, paths[0]) == 0 ? 0 : 1;
    if (fail_unlink) { error = 13; return -1; }
    if (!present[slot]) { error = 2; return -1; }
    present[slot] = 0;
    sizes[slot] = 0;
    return 0;
}
static int *fake_errno(void) { return &error; }
static uintptr_t fake_firmware_address(uint32_t address)
{
    switch (address) {
    case CANOPUS_SUP_NUTTX_OPEN: return (uintptr_t)fake_open;
    case CANOPUS_SUP_NUTTX_CLOSE: return (uintptr_t)fake_close;
    case CANOPUS_SUP_NUTTX_READ: return (uintptr_t)fake_read;
    case CANOPUS_SUP_NUTTX_WRITE: return (uintptr_t)fake_write;
    case CANOPUS_SUP_NUTTX_UNLINK: return (uintptr_t)fake_unlink;
    case CANOPUS_SUP_NUTTX_ERRNO_LOCATION: return (uintptr_t)fake_errno;
    default: return 0;
    }
}
TEST(p65_firmware_registry_roundtrip_and_failure_fallback)
{
    uint8_t payload[CANOPUS_SUP_REGISTRY_SIZE] = {0}, output[CANOPUS_SUP_REGISTRY_SIZE];
    uint32_t magic = CANOPUS_SUP_REGISTRY_MAGIC;
    CHECK_EQ(CANOPUS_SUP_NUTTX_WRITE, UINT32_C(0x0C2497F1));
    CHECK(CANOPUS_SUP_NUTTX_WRITE != UINT32_C(0x0C248455));
    CHECK_EQ(canopus_p65_registry_supervisor_save_error(0), CANOPUS_SUP_ERR_NONE);
    CHECK_EQ(canopus_p65_registry_supervisor_save_error(CANOPUS_P65_REGISTRY_ERR_WRITE),
             CANOPUS_SUP_ERR_REGISTRY_WRITE);
    CHECK_EQ(canopus_p65_registry_supervisor_save_error(CANOPUS_P65_REGISTRY_ERR_VERIFY),
             CANOPUS_SUP_ERR_REGISTRY_VERIFY_FINAL);
    CHECK_EQ(canopus_p65_registry_supervisor_save_error(CANOPUS_P65_REGISTRY_ERR_IO),
             CANOPUS_SUP_ERR_REGISTRY_OPEN);
    CHECK(canopus_p65_registry_supervisor_save_error(CANOPUS_P65_REGISTRY_ERR_WRITE)
          != CANOPUS_SUP_ERR_STAGE);
    memset(files, 0, sizeof(files));
    memset(present, 0, sizeof(present));
    memset(sizes, 0, sizeof(sizes));
    initialized = 0;
    open_count = close_count = fail_write = fail_close = fail_unlink = 0;
    payload[0] = (uint8_t)magic;
    payload[1] = (uint8_t)(magic >> 8);
    payload[2] = (uint8_t)(magic >> 16);
    payload[3] = (uint8_t)(magic >> 24);
    payload[4] = CANOPUS_SUP_REGISTRY_VERSION;
    CHECK_EQ(canopus_p65_registry_firmware_restore(output, sizeof(output)), 1);
    CHECK_EQ(canopus_p65_registry_firmware_persist(payload, sizeof(payload)), 0);
    payload[12] = 0x44;
    CHECK_EQ(canopus_p65_registry_firmware_persist(payload, sizeof(payload)), 0);
    CHECK_EQ(canopus_p65_registry_firmware_restore(output, sizeof(output)), 0);
    CHECK(memcmp(output, payload, sizeof(output)) == 0);
    /* Oversized newest record is corrupt, not an overflow or I/O failure. */
    sizes[1] += 80;
    CHECK_EQ(canopus_p65_registry_firmware_restore(output, sizeof(output)), 0);
    CHECK_EQ(output[12], 0);
    fail_write = 1;
    CHECK(canopus_p65_registry_firmware_persist(payload, sizeof(payload)) < 0);
    fail_write = 0;
    CHECK_EQ(canopus_p65_registry_firmware_restore(output, sizeof(output)), 0);
    CHECK_EQ(output[12], 0);
    fail_unlink = 1;
    CHECK(canopus_p65_registry_firmware_persist(payload, sizeof(payload)) < 0);
    fail_unlink = 0;
    CHECK_EQ(canopus_p65_registry_firmware_restore(output, sizeof(output)), 0);
    fail_close = 1;
    CHECK(canopus_p65_registry_firmware_restore(output, sizeof(output)) < 0);
    CHECK_EQ(open_count, close_count);
}
static int stage_result, stage_calls, persist_calls, native_stage_completed;
static int integration_stage(void *cookie, const char *token, uint32_t stage)
{
    (void)cookie;
    CHECK(token == 0 && stage == 0);
    stage_calls++;
    if (stage_result == 0) native_stage_completed = 1;
    return stage_result;
}
static int integration_persist(void *cookie, const uint8_t *data, uint32_t size)
{
    (void)cookie;
    persist_calls++;
    return canopus_p65_registry_supervisor_save_error(
        canopus_p65_registry_firmware_persist(data, size));
}
TEST(p65_native_stage_and_registry_failures_are_distinct)
{
    struct canopus_supervisor_v1 supervisor;
    static const struct canopus_sup_platform_v1 platform = {
        .target_id = "xiaomi-p65-3.100.043",
        .stage_package = integration_stage,
        .persist = integration_persist,
    };
    /* Little-endian CPC1 INSTALL stage 0, no package token. */
    uint8_t command[CANOPUS_SUP_COMMAND_SIZE] = {
        0x31, 0x43, 0x50, 0x43, 0x02, 0x00, 0x51, 0x43,
    };
    memset(files, 0, sizeof(files));
    memset(present, 0, sizeof(present));
    memset(sizes, 0, sizeof(sizes));
    initialized = 0;
    open_count = close_count = fail_write = fail_close = fail_unlink = 0;
    stage_calls = persist_calls = native_stage_completed = 0;
    CHECK_EQ(canopus_supervisor_init(&supervisor, 1, &platform, 0), 0);
    stage_result = -1;
    CHECK_EQ(canopus_supervisor_handle_command(&supervisor, command), CANOPUS_RESULT_FAILED);
    CHECK_EQ((int32_t)supervisor.error_code, CANOPUS_SUP_ERR_STAGE);
    CHECK_EQ(persist_calls, 0);
    CHECK_EQ(open_count, 0);
    CHECK_EQ(native_stage_completed, 0);
    stage_result = 0;
    fail_write = 1;
    CHECK_EQ(canopus_supervisor_handle_command(&supervisor, command), CANOPUS_RESULT_FAILED);
    CHECK_EQ((int32_t)supervisor.error_code, CANOPUS_SUP_ERR_REGISTRY_WRITE);
    CHECK_EQ(persist_calls, 1);
    CHECK_EQ(stage_calls, 2);
    CHECK_EQ(native_stage_completed, 1);
    CHECK_EQ(open_count, close_count);
    fail_write = 0;
}
static const struct test_registry tests[] = {
    { "p65_firmware_registry_roundtrip_and_failure_fallback",
      p65_firmware_registry_roundtrip_and_failure_fallback_wrapper },
    { "p65_native_stage_and_registry_failures_are_distinct",
      p65_native_stage_and_registry_failures_are_distinct_wrapper },
};
int run_p65_registry_firmware_tests(void)
{
    RUN_TESTS(tests, sizeof(tests) / sizeof(tests[0]));
}
