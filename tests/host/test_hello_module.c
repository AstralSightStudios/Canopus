/* Host test: hello removable module end to end (CAN-C-010 host part). */
#include "canopus_test.h"
#include "canopus_abi.h"
#include "canopus_runtime.h"
#include "fake_target.h"
#include "hello.h"
#include "hello_platform.h"

TEST(hello_module_load_run_unload)
{
    const struct canopus_module_descriptor_v1 *d;
    fake_alloc_reset();
    fake_timer_active_count();

    d = hello_descriptor();
    CHECK(d->prepare(0) == 0);
    CHECK(d->activate(0) == 0);
    CHECK(fake_timer_active_count() == 1u);

    /* drive the fake timer a few times */
    fake_timer_fire_due();
    fake_timer_fire_due();
    fake_timer_fire_due();

    /* query status and validate the fixed-width record */
    uint8_t qbuf[64];
    struct canopus_status_writer_v1 q;
    CHECK(canopus_status_writer_init(&q, qbuf, sizeof(qbuf)) == 0);
    CHECK(d->query(&q) == 0);
    CHECK(q.used >= 16u);
    CHECK(CANOPUS_SNAPSHOT_READY(&q.snap) != 0);
    /* magic at offset 0: HELLO_MAGIC = 0x48454C4F LE */
    CHECK(qbuf[0] == 0x4F && qbuf[1] == 0x4C && qbuf[2] == 0x45 && qbuf[3] == 0x48);

    /* deactivate cancels the timer and invalidates the callback */
    CHECK(d->deactivate(0) == 0);
    CHECK(fake_timer_active_count() == 0u);

    /* stop releases resources */
    CHECK(d->stop(0) == 0);
}

TEST(hello_stale_timer_callback_is_noop)
{
    const struct canopus_module_descriptor_v1 *d = hello_descriptor();
    /* this exercises the module's own state via public API only */
    CHECK(d->prepare(0) == 0);
    CHECK(d->activate(0) == 0);
    CHECK(fake_timer_active_count() == 1u);
    /* deactivate invalidates the captured generation even if the fake timer
     * still holds the callback (belt-and-braces no-op path) */
    CHECK(d->deactivate(0) == 0);
    fake_timer_fire_due(); /* must not crash, must be a no-op */
    CHECK(d->stop(0) == 0);
}

TEST(hello_clean_teardown_no_alloc_leak)
{
    const struct canopus_module_descriptor_v1 *d = hello_descriptor();
    fake_alloc_reset();
    CHECK(d->prepare(0) == 0);
    CHECK(d->activate(0) == 0);
    CHECK(d->deactivate(0) == 0);
    CHECK(d->stop(0) == 0);
    CHECK_EQ(fake_alloc_live_count(), 0u);
}

static struct test_registry hello_tests[] = {
    { "hello_module_load_run_unload", hello_module_load_run_unload_wrapper },
    { "hello_stale_timer_callback_is_noop", hello_stale_timer_callback_is_noop_wrapper },
    { "hello_clean_teardown_no_alloc_leak", hello_clean_teardown_no_alloc_leak_wrapper },
};
#define HELLO_TESTS_LEN (sizeof(hello_tests) / sizeof(hello_tests[0]))

int run_hello_tests(void)
{
    RUN_TESTS(hello_tests, HELLO_TESTS_LEN);
}
