/* Execute the actual P65 provider through fake exact-address dispatch. */
#include "canopus_test.h"
#include "canopus_manager_p65_registry.h"
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
static const struct test_registry tests[] = {
    { "p65_firmware_registry_roundtrip_and_failure_fallback",
      p65_firmware_registry_roundtrip_and_failure_fallback_wrapper },
};
int run_p65_registry_firmware_tests(void)
{
    RUN_TESTS(tests, sizeof(tests) / sizeof(tests[0]));
}
