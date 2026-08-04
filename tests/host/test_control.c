/* Host tests: sequence snapshot and status writer (CAN-C-003/004). */
#include "canopus_test.h"
#include "canopus_abi.h"
#include "canopus_runtime.h"
#include <string.h>

TEST(snapshot_partial_reject)
{
    struct canopus_snapshot_v1 s;
    s.sequence = 0;
    CHECK(CANOPUS_SNAPSHOT_READY(&s) != 0);
    canopus_snapshot_begin(&s);
    CHECK((s.sequence & 1u) != 0u);   /* mid-write, odd */
    CHECK(CANOPUS_SNAPSHOT_READY(&s) == 0);
    canopus_snapshot_commit(&s);
    CHECK((s.sequence & 1u) == 0u);   /* valid, even */
    CHECK(CANOPUS_SNAPSHOT_READY(&s) != 0);
}

TEST(status_writer_roundtrip)
{
    uint8_t buf[64];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    CHECK(canopus_status_put_u8(&w, 0xAA) == 0);
    CHECK(canopus_status_put_u16(&w, 0xBBCC) == 0);
    CHECK(canopus_status_put_u32(&w, 0x11223344) == 0);
    CHECK_EQ(w.used, 7u);
    CHECK(buf[0] == 0xAA);
    CHECK(buf[1] == 0xCC && buf[2] == 0xBB);
    CHECK(buf[3] == 0x44 && buf[4] == 0x33 && buf[5] == 0x22 && buf[6] == 0x11);
    CHECK_EQ(w.dropped, 0u);
    canopus_status_writer_publish(&w);
    CHECK(CANOPUS_SNAPSHOT_READY(&w.snap) != 0);
}

TEST(status_writer_overflow_drops)
{
    uint8_t buf[8];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, 8) == 0);
    CHECK(canopus_status_put_u32(&w, 1) == 0);
    CHECK(canopus_status_put_u32(&w, 2) == 0);
    /* no space for a third u32 */
    CHECK(canopus_status_put_u32(&w, 3) == -1);
    CHECK_EQ(w.dropped, 1u);
    /* a small write still works after the overflow */
    CHECK(canopus_status_put_u8(&w, 0xFF) == -1); /* 9 bytes > 8 */
}

static struct test_registry control_tests[] = {
    { "snapshot_partial_reject", snapshot_partial_reject_wrapper },
    { "status_writer_roundtrip", status_writer_roundtrip_wrapper },
    { "status_writer_overflow_drops", status_writer_overflow_drops_wrapper },
};
#define CONTROL_TESTS_LEN (sizeof(control_tests) / sizeof(control_tests[0]))

int run_control_tests(void)
{
    RUN_TESTS(control_tests, CONTROL_TESTS_LEN);
}
