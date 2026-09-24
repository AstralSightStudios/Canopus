/* Host tests: in-process P65 Manager client transport. */
#include "canopus_test.h"
#include "canopus_manager_p65_transport.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"

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

static const struct test_registry p65_local_client_tests[] = {
    { "p65_local_client_reads_supervisor_query_payloads_without_dev",
      p65_local_client_reads_supervisor_query_payloads_without_dev_wrapper },
};

int run_p65_local_client_tests(void)
{
    RUN_TESTS(p65_local_client_tests,
              sizeof(p65_local_client_tests) /
                  sizeof(p65_local_client_tests[0]));
}
