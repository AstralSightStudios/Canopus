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

/* ---- CAN-P1-004: writer bounds, publish protocol, NULL source -------- */

TEST(status_writer_init_marks_not_ready_until_publish)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    CHECK_EQ(w.state, CANOPUS_STATUS_WRITER_WRITING);
    CHECK((w.snap.sequence & 1u) != 0u); /* odd => not valid mid-build */
    CHECK(CANOPUS_SNAPSHOT_READY(&w.snap) == 0);
}

TEST(status_writer_publish_protocol)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    /* begin while the first record is already being written fails */
    CHECK(canopus_status_writer_begin(&w) == -1);
    CHECK(canopus_status_put_u32(&w, 0x11223344) == 0);
    CHECK(canopus_status_writer_publish(&w) == 0);
    CHECK_EQ(w.state, CANOPUS_STATUS_WRITER_PUBLISHED);
    CHECK(CANOPUS_SNAPSHOT_READY(&w.snap) != 0);
    /* double publish fails */
    CHECK(canopus_status_writer_publish(&w) == -1);
    /* writes after publish fail */
    CHECK(canopus_status_put_u8(&w, 1) == -1);
    /* begin starts a fresh record after publish */
    CHECK(canopus_status_writer_begin(&w) == 0);
    CHECK_EQ(w.used, 0u);
    CHECK((w.snap.sequence & 1u) != 0u); /* not ready again */
    CHECK(canopus_status_put_u8(&w, 0x42) == 0);
    CHECK(canopus_status_writer_publish(&w) == 0);
    CHECK(buf[0] == 0x42);
}

TEST(status_writer_rejects_null_source)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    /* NULL source with non-zero length must fail and not advance used */
    CHECK(canopus_status_put_bytes(&w, 0, 5) == -1);
    CHECK_EQ(w.used, 0u);
    CHECK_EQ(w.dropped, 0u);
    /* zero length with NULL is fine and advances nothing */
    CHECK(canopus_status_put_bytes(&w, 0, 0) == 0);
    CHECK_EQ(w.used, 0u);
}

TEST(status_writer_need_overflow_safe)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    /* a near-UINT32_MAX need must be rejected without `used + need` wrap */
    CHECK(canopus_status_put_bytes(&w, buf, UINT32_MAX) == -1);
    CHECK_EQ(w.used, 0u);
    CHECK_EQ(w.dropped, 1u);
}

TEST(status_writer_dropped_saturates)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    w.dropped = UINT32_MAX;
    CHECK(canopus_status_put_bytes(&w, buf, UINT32_MAX) == -1);
    CHECK_EQ(w.dropped, UINT32_MAX); /* no wrap */
}

TEST(status_writer_corrupt_used_rejected)
{
    uint8_t buf[16];
    struct canopus_status_writer_v1 w;
    CHECK(canopus_status_writer_init(&w, buf, sizeof(buf)) == 0);
    /* a corrupt used > capacity must not cause an OOB write */
    w.used = 0x40000000u;
    CHECK(canopus_status_put_u8(&w, 1) == -1);
    CHECK_EQ(w.used, 0x40000000u); /* unchanged */
    CHECK(canopus_status_put_bytes(&w, 0, 4) == -1);
}

static struct test_registry control_tests[] = {
    { "snapshot_partial_reject", snapshot_partial_reject_wrapper },
    { "status_writer_roundtrip", status_writer_roundtrip_wrapper },
    { "status_writer_overflow_drops", status_writer_overflow_drops_wrapper },
    { "status_writer_init_marks_not_ready_until_publish", status_writer_init_marks_not_ready_until_publish_wrapper },
    { "status_writer_publish_protocol", status_writer_publish_protocol_wrapper },
    { "status_writer_rejects_null_source", status_writer_rejects_null_source_wrapper },
    { "status_writer_need_overflow_safe", status_writer_need_overflow_safe_wrapper },
    { "status_writer_dropped_saturates", status_writer_dropped_saturates_wrapper },
    { "status_writer_corrupt_used_rejected", status_writer_corrupt_used_rejected_wrapper },
};
#define CONTROL_TESTS_LEN (sizeof(control_tests) / sizeof(control_tests[0]))

int run_control_tests(void)
{
    RUN_TESTS(control_tests, CONTROL_TESTS_LEN);
}
