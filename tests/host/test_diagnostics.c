/* Host tests: event writer and bounded buffer (CAN-C-008). */
#include "canopus_test.h"
#include "canopus_runtime.h"
#include <string.h>

TEST(event_log_append_monotonic)
{
    struct canopus_event_log_v1 log;
    canopus_event_log_init(&log, 7);
    uint32_t s1 = canopus_event_log_append(&log, 100, 3, 0x42, 1,
                                           CANOPUS_STATE_READY,
                                           CANOPUS_STATE_ACTIVE, 0);
    uint32_t s2 = canopus_event_log_append(&log, 101, 3, 0x42, 1,
                                           CANOPUS_STATE_ACTIVE,
                                           CANOPUS_STATE_BOOT_RESIDENT, 0);
    CHECK_EQ(s1, 0u);
    CHECK_EQ(s2, 1u);
    CHECK_EQ(canopus_event_log_count(&log), 2u);
    CHECK_EQ(log.entries[0].boot_id, 7u);
    /* CAN-P2-010: correlation fields are recorded */
    CHECK_EQ(log.entries[0].timestamp, 100u);
    CHECK_EQ(log.entries[0].module_id, 3u);
    CHECK_EQ(log.entries[0].request_id, 0x42u);
}

TEST(event_log_wraps_and_keeps_sequence)
{
    struct canopus_event_log_v1 log;
    canopus_event_log_init(&log, 1);
    uint32_t last = 0;
    uint32_t i;
    for (i = 0; i < CANOPUS_EVENT_LOG_ENTRIES + 5u; i++) {
        last = canopus_event_log_append(&log, 0, 0, 0, 0, 0, 0, 0);
    }
    /* sequence is monotonic and never resets */
    CHECK_EQ(last, CANOPUS_EVENT_LOG_ENTRIES + 4u);
    /* stored count is bounded by the ring capacity */
    CHECK_EQ(canopus_event_log_count(&log), CANOPUS_EVENT_LOG_ENTRIES);
    /* next_sequence is the total appended, independent of stored count */
    CHECK_EQ(canopus_event_log_next_sequence(&log), CANOPUS_EVENT_LOG_ENTRIES + 5u);
    /* the 5 entries that did not fit were counted as dropped */
    CHECK_EQ(canopus_event_log_dropped(&log), 5u);
    /* the most recent write landed at head-1 (wraps to 4), seq 20 */
    CHECK_EQ(log.entries[(log.head + CANOPUS_EVENT_LOG_ENTRIES - 1u) %
                         CANOPUS_EVENT_LOG_ENTRIES]
                 .sequence,
             20u);
}

TEST(event_log_stored_count_bounded)
{
    struct canopus_event_log_v1 log;
    uint32_t i;
    canopus_event_log_init(&log, 3);
    /* fill the ring exactly */
    for (i = 0; i < CANOPUS_EVENT_LOG_ENTRIES; i++) {
        canopus_event_log_append(&log, 0, 0, 0, 0, 0, 0, 0);
    }
    CHECK_EQ(canopus_event_log_count(&log), CANOPUS_EVENT_LOG_ENTRIES);
    CHECK_EQ(canopus_event_log_dropped(&log), 0u);
    /* keep appending past the capacity: stored stays bounded, dropped grows */
    canopus_event_log_append(&log, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(canopus_event_log_count(&log), CANOPUS_EVENT_LOG_ENTRIES);
    CHECK_EQ(canopus_event_log_dropped(&log), 1u);
    canopus_event_log_append(&log, 0, 0, 0, 0, 0, 0, 0);
    CHECK_EQ(canopus_event_log_count(&log), CANOPUS_EVENT_LOG_ENTRIES);
    CHECK_EQ(canopus_event_log_dropped(&log), 2u);
}

TEST(event_log_is_gap)
{
    /* exact successor is not a gap */
    CHECK(canopus_event_log_is_gap(5, 6) == 0);
    /* a skipped sequence is a gap (eviction/drop between two reads) */
    CHECK(canopus_event_log_is_gap(5, 7) != 0);
    /* same or earlier sequence is a gap */
    CHECK(canopus_event_log_is_gap(5, 5) != 0);
    CHECK(canopus_event_log_is_gap(5, 4) != 0);
    /* wrap at UINT32_MAX is not a gap */
    CHECK(canopus_event_log_is_gap(UINT32_MAX, 0) == 0);
    CHECK(canopus_event_log_is_gap(UINT32_MAX, 2) != 0);
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

/* ---- bounded text writer (CAN-P0-002) ------------------------------ */

TEST(text_writer_exact_and_append)
{
    struct canopus_text_writer_v1 w;
    char buf[16];
    CHECK(canopus_text_writer_init(&w, buf, sizeof(buf)) == 0);
    CHECK(buf[0] == '\0');
    CHECK(canopus_text_writer_append(&w, "hello") == 0);
    CHECK(strcmp(buf, "hello") == 0);
    CHECK_EQ(w.used, 5u);
    CHECK_EQ(w.truncated, 0u);
    CHECK(canopus_text_writer_append(&w, " world") == 0);
    CHECK(strcmp(buf, "hello world") == 0);
    CHECK_EQ(w.used, 11u);
    CHECK_EQ(w.truncated, 0u);
}

TEST(text_writer_truncation_reports_error_and_keeps_nul)
{
    struct canopus_text_writer_v1 w;
    char buf[8];
    CHECK(canopus_text_writer_init(&w, buf, sizeof(buf)) == 0);
    /* "0123456" fits exactly in 7 chars + NUL = 8 */
    CHECK(canopus_text_writer_append(&w, "0123456") == 0);
    CHECK_EQ(w.truncated, 0u);
    /* "X" does not fit before the NUL: dedicated truncation error */
    CHECK(canopus_text_writer_append(&w, "X") == CANOPUS_TEXT_TRUNCATED);
    CHECK_EQ(w.truncated, 1u);
    CHECK(buf[7] == '\0'); /* NUL at cap-1 */
    CHECK(strlen(buf) <= 7u);
    CHECK(strcmp(buf, "0123456") == 0);
    /* once truncated, further appends are no-ops that keep failing */
    CHECK(canopus_text_writer_append(&w, "YY") == CANOPUS_TEXT_TRUNCATED);
    CHECK(buf[7] == '\0');
    CHECK(strcmp(buf, "0123456") == 0);
}

TEST(text_writer_cap_one)
{
    struct canopus_text_writer_v1 w;
    char buf[1];
    CHECK(canopus_text_writer_init(&w, buf, 1) == 0);
    CHECK(buf[0] == '\0');
    /* the empty string fits in cap 1 (just the NUL) */
    CHECK(canopus_text_writer_append(&w, "") == 0);
    CHECK(buf[0] == '\0');
    /* a non-empty string truncates and the buffer stays NUL */
    CHECK(canopus_text_writer_append(&w, "a") == CANOPUS_TEXT_TRUNCATED);
    CHECK(buf[0] == '\0');
    CHECK_EQ(w.truncated, 1u);
}

TEST(text_writer_init_and_append_validation)
{
    struct canopus_text_writer_v1 w;
    char buf[4];
    CHECK(canopus_text_writer_init(0, buf, sizeof(buf)) == -1);
    CHECK(canopus_text_writer_init(&w, 0, sizeof(buf)) == -1);
    CHECK(canopus_text_writer_init(&w, buf, 0) == -1);
    CHECK(canopus_text_writer_append(0, "x") == -1);
    CHECK(canopus_text_writer_append(&w, 0) == -1);
}

static struct test_registry diag_tests[] = {
    { "event_log_append_monotonic", event_log_append_monotonic_wrapper },
    { "event_log_wraps_and_keeps_sequence", event_log_wraps_and_keeps_sequence_wrapper },
    { "event_log_stored_count_bounded", event_log_stored_count_bounded_wrapper },
    { "event_log_is_gap", event_log_is_gap_wrapper },
    { "buf_copy_exact_and_truncate", buf_copy_exact_and_truncate_wrapper },
    { "text_writer_exact_and_append", text_writer_exact_and_append_wrapper },
    { "text_writer_truncation_reports_error_and_keeps_nul", text_writer_truncation_reports_error_and_keeps_nul_wrapper },
    { "text_writer_cap_one", text_writer_cap_one_wrapper },
    { "text_writer_init_and_append_validation", text_writer_init_and_append_validation_wrapper },
};
#define DIAG_TESTS_LEN (sizeof(diag_tests) / sizeof(diag_tests[0]))

int run_diag_tests(void)
{
    RUN_TESTS(diag_tests, DIAG_TESTS_LEN);
}
