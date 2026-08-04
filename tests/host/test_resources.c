/* Host tests: resource tracker and generation guards (CAN-C-006/007). */
#include "canopus_test.h"
#include "canopus_runtime.h"

static int g_released_count = 0;
static void fake_release(struct canopus_resource_v1 *res)
{
    (void)res;
    g_released_count++;
}

static int add_res(struct canopus_resource_tracker_v1 *t,
                   uint32_t kind, void *handle)
{
    struct canopus_resource_v1 r;
    r.kind = kind;
    r.state = CANOPUS_RES_ACTIVE;
    r.generation = 0;
    r.handle = handle;
    r.on_release = fake_release;
    return canopus_tracker_add(t, &r);
}

TEST(resource_add_release)
{
    struct canopus_resource_tracker_v1 t;
    canopus_tracker_init(&t);
    g_released_count = 0;
    CHECK(add_res(&t, CANOPUS_RESOURCE_TIMER, (void *)0x100) == 0);
    CHECK_EQ(t.count, 1u);
    CHECK(canopus_tracker_release(&t, (void *)0x100) == 0);
    CHECK_EQ(g_released_count, 1);
    /* double release must fail */
    CHECK(canopus_tracker_release(&t, (void *)0x100) == -1);
}

TEST(resource_duplicate_handle_rejected)
{
    struct canopus_resource_tracker_v1 t;
    canopus_tracker_init(&t);
    CHECK(add_res(&t, CANOPUS_RESOURCE_FD, (void *)0x200) == 0);
    CHECK(add_res(&t, CANOPUS_RESOURCE_FD, (void *)0x200) == -1);
}

TEST(resource_drain_detach)
{
    struct canopus_resource_tracker_v1 t;
    canopus_tracker_init(&t);
    CHECK(add_res(&t, CANOPUS_RESOURCE_PROTOCOL, (void *)0x300) == 0);
    CHECK(canopus_tracker_drain(&t, (void *)0x300) == 0);
    /* detach after unregister-returned-EBUSY: namespace gone, no retry */
    CHECK(canopus_tracker_detach(&t, (void *)0x300) == 0);
    CHECK(canopus_tracker_release(&t, (void *)0x300) == -1); /* detached, must not release */
}

TEST(resource_retain_until_reboot)
{
    struct canopus_resource_tracker_v1 t;
    canopus_tracker_init(&t);
    CHECK(add_res(&t, CANOPUS_RESOURCE_CALLBACK_TABLE, (void *)0x400) == 0);
    CHECK(canopus_tracker_retain_until_reboot(&t, (void *)0x400) == 0);
    CHECK(canopus_tracker_release(&t, (void *)0x400) == -1);
}

TEST(resource_release_all)
{
    struct canopus_resource_tracker_v1 t;
    canopus_tracker_init(&t);
    g_released_count = 0;
    CHECK(add_res(&t, CANOPUS_RESOURCE_CHAR_DEVICE, (void *)0x500) == 0);
    CHECK(add_res(&t, CANOPUS_RESOURCE_CHAR_DEVICE, (void *)0x501) == 0);
    CHECK(add_res(&t, CANOPUS_RESOURCE_TIMER, (void *)0x502) == 0);
    canopus_tracker_release_all(&t);
    CHECK_EQ(g_released_count, 3);
    CHECK_EQ(t.count, 0u);
    /* handles are gone; release on the old handle fails */
    CHECK(canopus_tracker_release(&t, (void *)0x500) == -1);
}

TEST(generation_stale_callback)
{
    struct canopus_generation_v1 g;
    canopus_generation_init(&g);
    uint32_t captured = canopus_generation_get(&g);
    CHECK(canopus_generation_valid(&g, captured) == 1);
    canopus_generation_bump(&g);
    CHECK(canopus_generation_valid(&g, captured) == 0); /* stale -> no-op */
    CHECK(canopus_generation_valid(&g, canopus_generation_get(&g)) == 1);
}

static struct test_registry resource_tests[] = {
    { "resource_add_release", resource_add_release_wrapper },
    { "resource_duplicate_handle_rejected", resource_duplicate_handle_rejected_wrapper },
    { "resource_drain_detach", resource_drain_detach_wrapper },
    { "resource_retain_until_reboot", resource_retain_until_reboot_wrapper },
    { "resource_release_all", resource_release_all_wrapper },
    { "generation_stale_callback", generation_stale_callback_wrapper },
};
#define RESOURCE_TESTS_LEN (sizeof(resource_tests) / sizeof(resource_tests[0]))

int run_resource_tests(void)
{
    RUN_TESTS(resource_tests, RESOURCE_TESTS_LEN);
}
