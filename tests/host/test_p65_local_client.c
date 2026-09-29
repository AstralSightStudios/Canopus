/* Host tests: in-process P65 Manager client transport. */
#include "canopus_test.h"
#include "canopus_manager_p65_transport.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_module_registration.h"
#include "canopus_p65_notification.h"
#include <string.h>

static const struct canopus_sup_platform_v1 p65_local_platform = {
    "xiaomi-p65-3.100.043", 0, 0, 0, 0, 0, 0, 0
};

TEST(p65_local_client_reads_supervisor_query_payloads_without_dev)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_client_v1 client;
    struct canopus_client_device_snapshot_v1 device;
    struct canopus_client_module_snapshot_v1 module;
    const struct canopus_client_io_v1 *io =
        canopus_manager_p65_local_client_io();

    CHECK(io != 0);
    CHECK(canopus_supervisor_init(&sup, 7u, &p65_local_platform, 0) == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                        3u, 1u, "mod.example") == 0);
    CHECK(canopus_client_init(&client, io, &sup) == CANOPUS_CLIENT_OK);
    CHECK(io->open(&sup, "/dev/not-canopus") < 0);
    CHECK(canopus_client_open(&client) == CANOPUS_CLIENT_OK);
    CHECK(canopus_client_query_device(&client, 1u, &device) ==
          CANOPUS_CLIENT_OK);
    CHECK_EQ(device.framework_revision, 7u);
    CHECK_EQ(device.module_count, 1u);
    CHECK(canopus_client_query_module(&client, 2u, 0u, &module) ==
          CANOPUS_CLIENT_OK);
    CHECK_EQ(module.slot, 0u);
    CHECK_EQ(module.lifecycle_class, CANOPUS_LIFECYCLE_REMOVABLE);
    CHECK(module.module_id[0] == 'm');
    CHECK(canopus_client_query_module(&client, 3u, 1u, &module) ==
          CANOPUS_CLIENT_ERR_NOT_FOUND);
    CHECK(canopus_client_close(&client) == CANOPUS_CLIENT_OK);
}

static int registration_open_result, registration_close_result;
static int registration_opens, registration_writes, registration_closes;
static int32_t registration_write_result;
static uint8_t registration_frame[40];

static int registration_open(const char *path, int flags, ...)
{
    registration_opens++;
    if (strcmp(path, "/dev/canopus") != 0 || flags != 2) return -99;
    return registration_open_result;
}
static int32_t registration_write(int fd, const void *data, uint32_t size)
{
    registration_writes++;
    if (fd != 0 || size != 40) return -99;
    memcpy(registration_frame, data, size);
    return registration_write_result;
}
static int registration_close(int fd)
{
    registration_closes++;
    if (fd != 0) return -99;
    return registration_close_result;
}

TEST(p65_module_registration_atomic_write_and_lifetime)
{
    const struct canopus_module_registration_io_v1 io = {
        registration_open, registration_write, registration_close, 2,
    };
    int close_error;
    registration_opens = registration_writes = registration_closes = 0;
    registration_open_result = registration_close_result = 0;
    registration_write_result = 40;
    CHECK_EQ(canopus_module_register_fd(&io, 0x12345678, "org.canopus.test",
                                        &close_error), 0);
    CHECK_EQ(close_error, 0);
    CHECK_EQ(registration_opens, 1);
    CHECK_EQ(registration_writes, 1);
    CHECK_EQ(registration_closes, 1);
    CHECK(canopus_module_registration_is_frame(registration_frame, 40));
    CHECK_EQ(registration_frame[4], 0x78);
    CHECK_EQ(registration_frame[7], 0x12);
    CHECK(strcmp((char *)registration_frame + 8, "org.canopus.test") == 0);
    CHECK_EQ(registration_frame[39], 0);
    /* A published descriptor cannot be revoked by a close failure. */
    registration_close_result = -5;
    CHECK_EQ(canopus_module_register_fd(&io, 1, "test", &close_error), 0);
    CHECK_EQ(close_error, -5);
    registration_write_result = 39;
    CHECK(canopus_module_register_fd(&io, 1, "test", 0) < 0);
    CHECK_EQ(registration_writes, 3); /* No short-write retry. */
    CHECK_EQ(registration_closes, 3);
    registration_write_result = -1;
    CHECK(canopus_module_register_fd(&io, 1, "test", 0) < 0);
    CHECK_EQ(registration_closes, 4);
    registration_open_result = -1;
    CHECK(canopus_module_register_fd(&io, 1, "test", 0) < 0);
    CHECK_EQ(registration_writes, 4);
    CHECK_EQ(registration_closes, 4);
    CHECK(canopus_module_register_fd(&io, 1, "", 0) < 0);
    CHECK(canopus_module_register_fd(&io, 1,
          "12345678901234567890123456789012", 0) < 0);
    CHECK(canopus_module_register_fd(&io, 0, "test", 0) < 0);
    CHECK(canopus_module_register_fd(0, 1, "test", 0) < 0);
    CHECK_EQ(registration_opens, 5);
}

TEST(p65_notification_has_exact_layout_and_no_callback_context)
{
    struct canopus_p65_notification_v1 message;
    const uint8_t *bytes = (const uint8_t *)&message;
    uint32_t i;
    memset(&message, 0xAB, sizeof(message));
    canopus_p65_notification_init(&message, 0x123456789ABCDEF0ULL,
                                  0x11223344, 0x55667788, 0x99999999, 0x77777777);
    CHECK_EQ(message.message_id, 0x123456789ABCDEF0ULL);
    CHECK_EQ(message.title, 0x11223344u);
    CHECK_EQ(message.source, 0x55667788u);
    CHECK_EQ(message.body, 0x99999999u);
    CHECK_EQ(message.small_icon, 0x77777777u);
    CHECK_EQ(message.large_icon, 0x77777777u);
    for (i = 36; i < 88; i++) CHECK_EQ(bytes[i], 0u);
}

static const struct test_registry p65_local_client_tests[] = {
    { "p65_notification_has_exact_layout_and_no_callback_context",
      p65_notification_has_exact_layout_and_no_callback_context_wrapper },
    { "p65_module_registration_atomic_write_and_lifetime",
      p65_module_registration_atomic_write_and_lifetime_wrapper },
    { "p65_local_client_reads_supervisor_query_payloads_without_dev",
      p65_local_client_reads_supervisor_query_payloads_without_dev_wrapper },
};

int run_p65_local_client_tests(void)
{
    RUN_TESTS(p65_local_client_tests,
              sizeof(p65_local_client_tests) /
                  sizeof(p65_local_client_tests[0]));
}
