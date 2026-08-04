/* Host tests: event writer and bounded buffer (CAN-C-008). */
#include "canopus_test.h"
#include "canopus_runtime.h"
#include <string.h>

TEST(event_log_append_monotonic)
{
    struct canopus_event_log_v1 log;
    canopus_event_log_init(&log, 7);
    uint32_t s1 = canopus_event_log_append(&log, 1, CANOPUS_STATE_READY,
                                           CANOPUS_STATE_ACTIVE, 0);
    uint32_t s2 = canopus_event_log_append(&log, 1, CANOPUS_STATE_ACTIVE,
                                           CANOPUS_STATE_BOOT_RESIDENT, 0);
    CHECK_EQ(s1, 0u);
    CHECK_EQ(s2, 1u);
    CHECK_EQ(canopus_event_log_count(&log), 2u);
    CHECK_EQ(log.entries[0].boot_id, 7u);
}

TEST(event_log_wraps_and_keeps_sequence)
{
    struct canopus_event_log_v1 log;
    canopus_event_log_init(&log, 1);
    uint32_t last = 0;
    uint32_t i;
    for (i = 0; i < CANOPUS_EVENT_LOG_ENTRIES + 5u; i++) {
        last = canopus_event_log_append(&log, 0, 0, 0, 0);
    }
    /* sequence is monotonic and never resets */
    CHECK_EQ(last, CANOPUS_EVENT_LOG_ENTRIES + 4u);
    CHECK_EQ(canopus_event_log_count(&log), CANOPUS_EVENT_LOG_ENTRIES + 5u);
    /* head wraps; dropped stays zero (ring always has room) */
    CHECK_EQ(canopus_event_log_dropped(&log), 0u);
    /* the most recent write landed at head-1 (wraps to 4), seq 20 */
    CHECK_EQ(log.entries[(log.head + CANOPUS_EVENT_LOG_ENTRIES - 1u) %
                         CANOPUS_EVENT_LOG_ENTRIES]
                 .sequence,
             20u);
}

TEST(buf_copy_exact_and_truncate)
{
    char dst[8];
    int n = canopus_buf_copy(dst, sizeof(dst), "hello");
    CHECK_EQ(n, 5);
    CHECK(strcmp(dst, "hello") == 0);

    n = canopus_buf_copy(dst, 4, "hello");
    CHECK_EQ(n, -1); /* truncated */
    CHECK(strcmp(dst, "hel") == 0); /* nul-terminated, bounded */

    n = canopus_buf_copy(dst, sizeof(dst), "");
    CHECK_EQ(n, 0);
    CHECK(strcmp(dst, "") == 0);

    CHECK(canopus_buf_copy(dst, 0, "x") == -1);
    CHECK(canopus_buf_copy(0, 8, "x") == -1);
}

static struct test_registry diag_tests[] = {
    { "event_log_append_monotonic", event_log_append_monotonic_wrapper },
    { "event_log_wraps_and_keeps_sequence", event_log_wraps_and_keeps_sequence_wrapper },
    { "buf_copy_exact_and_truncate", buf_copy_exact_and_truncate_wrapper },
};
#define DIAG_TESTS_LEN (sizeof(diag_tests) / sizeof(diag_tests[0]))

int run_diag_tests(void)
{
    RUN_TESTS(diag_tests, DIAG_TESTS_LEN);
}
