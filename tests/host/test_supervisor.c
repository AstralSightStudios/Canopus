/* Host tests: supervisor protocol and package store (CAN-DEV-002/003). */
#include "canopus_test.h"
#include "canopus_protocol.h"
#include "canopus_store.h"
#include "canopus_memory.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int test_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- protocol ----------------------------------------------------- */

TEST(proto_request_validate)
{
    struct canopus_proto_request_v1 r;
    canopus_memset(&r, 0, sizeof(r));
    r.magic = CANOPUS_PROTO_MAGIC;
    r.struct_size = CANOPUS_PROTO_REQUEST_SIZE;
    r.abi_major = CANOPUS_ABI_MAJOR;
    r.command = CANOPUS_CMD_ECHO;
    r.request_id = 7;
    r.payload_size = 0;
    CHECK(canopus_proto_validate_request(&r, 64) == 0);

    /* wrong magic */
    r.magic = 0;
    CHECK(canopus_proto_validate_request(&r, 64) == -1);
    r.magic = CANOPUS_PROTO_MAGIC;

    /* wrong ABI major is rejected */
    r.abi_major = 99;
    CHECK(canopus_proto_validate_request(&r, 64) == -1);
    r.abi_major = CANOPUS_ABI_MAJOR;

    /* oversized payload against the buffer */
    r.payload_size = 1000;
    CHECK(canopus_proto_validate_request(&r, 64) == -1);
    r.payload_size = 0;
    CHECK(canopus_proto_validate_request(&r, 64) == 0);
}

TEST(proto_response_roundtrip)
{
    struct canopus_proto_response_v1 resp;
    canopus_proto_response_init(&resp, 42, CANOPUS_RESULT_COMPLETED, 0);
    CHECK(resp.request_id == 42);
    CHECK(resp.result_state == CANOPUS_RESULT_COMPLETED);
    CHECK(canopus_proto_validate_response(&resp, 64) == 0);
}

TEST(pending_async_states)
{
    struct canopus_pending_table_v1 t;
    canopus_pending_init(&t);
    CHECK(canopus_pending_accept(&t, 100, CANOPUS_CMD_INSTALL) == 0);
    /* duplicate id rejected */
    CHECK(canopus_pending_accept(&t, 100, CANOPUS_CMD_ECHO) == -1);

    const struct canopus_pending_request_v1 *p = canopus_pending_find(&t, 100);
    CHECK(p != 0);
    CHECK(p->state == CANOPUS_RESULT_ACCEPTED);

    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_QUEUED) == 0);
    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_RUNNING) == 0);
    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_COMPLETED) == 0);
    /* terminal -> anything is rejected */
    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_RUNNING) == -1);
    /* finish with the same terminal result is idempotent */
    CHECK(canopus_pending_finish(&t, 100, CANOPUS_RESULT_COMPLETED) == 0);
    /* the terminal record is RETAINED until ack, so a late query still sees
     * the outcome */
    p = canopus_pending_find(&t, 100);
    CHECK(p != 0);
    CHECK(p->state == CANOPUS_RESULT_COMPLETED);
    CHECK(canopus_pending_ack(&t, 100) == 0);
    CHECK(canopus_pending_find(&t, 100) == 0);
}

/* ---- CAN-P1-002: pending state machine ------------------------------ */

TEST(pending_finish_requires_terminal_result)
{
    struct canopus_pending_table_v1 t;
    canopus_pending_init(&t);
    CHECK(canopus_pending_accept(&t, 300, CANOPUS_CMD_ECHO) == 0);
    /* finish with a non-terminal result is rejected and never silently
       converted to COMPLETED */
    CHECK(canopus_pending_finish(&t, 300, CANOPUS_RESULT_RUNNING) == -1);
    const struct canopus_pending_request_v1 *p = canopus_pending_find(&t, 300);
    CHECK(p != 0);
    CHECK(p->state == CANOPUS_RESULT_ACCEPTED);
}

TEST(pending_rejects_backwards_unknown_and_rejected)
{
    struct canopus_pending_table_v1 t;
    canopus_pending_init(&t);
    CHECK(canopus_pending_accept(&t, 500, CANOPUS_CMD_ECHO) == 0);
    /* backwards transition */
    CHECK(canopus_pending_set_state(&t, 500, CANOPUS_RESULT_QUEUED) == 0);
    CHECK(canopus_pending_set_state(&t, 500, CANOPUS_RESULT_ACCEPTED) == -1);
    /* unknown state */
    CHECK(canopus_pending_set_state(&t, 500, 0xFFFFu) == -1);
    /* REJECTED is never a tracked state */
    CHECK(canopus_pending_set_state(&t, 500, CANOPUS_RESULT_REJECTED) == -1);
    /* a contradicting terminal after completion is rejected */
    CHECK(canopus_pending_set_state(&t, 500, CANOPUS_RESULT_COMPLETED) == 0);
    CHECK(canopus_pending_finish(&t, 500, CANOPUS_RESULT_FAILED) == -1);
}

TEST(pending_stale_boot_rejected)
{
    struct canopus_pending_table_v1 t;
    canopus_pending_init(&t);
    canopus_pending_set_boot(&t, 1);
    CHECK(canopus_pending_accept(&t, 400, CANOPUS_CMD_INSTALL) == 0);
    canopus_pending_set_boot(&t, 2); /* reboot */
    CHECK(canopus_pending_set_state(&t, 400, CANOPUS_RESULT_QUEUED) == -1);
    CHECK(canopus_pending_finish(&t, 400, CANOPUS_RESULT_COMPLETED) == -1);
    CHECK(canopus_pending_ack(&t, 400) == -1);
}

TEST(pending_zero_id_and_per_request_error)
{
    struct canopus_pending_table_v1 t;
    canopus_pending_init(&t);
    /* request id 0 is reserved */
    CHECK(canopus_pending_accept(&t, 0, CANOPUS_CMD_INSTALL) == -1);
    CHECK(canopus_pending_accept(&t, 600, CANOPUS_CMD_INSTALL) == 0);
    CHECK(canopus_pending_set_error(&t, 600, 0xBEEFu) == 0);
    const struct canopus_pending_request_v1 *p = canopus_pending_find(&t, 600);
    CHECK(p != 0);
    CHECK(p->error == 0xBEEFu);
}

TEST(pending_table_full_rejects)
{
    struct canopus_pending_table_v1 t;
    uint32_t i;
    canopus_pending_init(&t);
    for (i = 0; i < CANOPUS_PENDING_MAX; i++) {
        CHECK(canopus_pending_accept(&t, 1000 + i, CANOPUS_CMD_ECHO) == 0);
    }
    CHECK(canopus_pending_accept(&t, 9999, CANOPUS_CMD_ECHO) == -1); /* full */
}

/* ---- store -------------------------------------------------------- */

static char g_store_root[200];
static unsigned g_root_seq;
static const char *test_root(void)
{
    /* unique per call so a test never sees another test's leftover slots */
    snprintf(g_store_root, sizeof(g_store_root),
             "/tmp/canopus_store_test_%d_%u", getpid(), g_root_seq++);
    return g_store_root;
}

TEST(store_install_preserves_previous)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char staged[200], active[200], previous[200];
    canopus_store_init(&store, root);

    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello", CANOPUS_STORE_SLOT_STAGED,
                            staged, sizeof(staged));
    canopus_store_slot_path(&store, "org.example.hello", CANOPUS_STORE_SLOT_ACTIVE,
                            active, sizeof(active));
    canopus_store_slot_path(&store, "org.example.hello", CANOPUS_STORE_SLOT_PREVIOUS,
                            previous, sizeof(previous));

    /* staged v2 exists */
    CHECK(mkdir(staged, 0750) == 0);
    CHECK(canopus_store_has_staged(&store, "org.example.hello") == 1);

    /* install v2 -> active */
    CHECK(canopus_store_install_staged(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(active) == 1);

    /* stage v3, install again -> v2 becomes previous */
    mkdir(staged, 0750);
    CHECK(canopus_store_install_staged(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(previous) == 1);

    /* rollback -> previous promoted to active */
    CHECK(canopus_store_rollback(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(active) == 1);
}

TEST(store_install_without_staged_fails)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    canopus_store_init(&store, root);
    CHECK(canopus_store_install_staged(&store, "org.example.none") == -1);
    CHECK(canopus_store_has_staged(&store, "org.example.none") == 0);
}

TEST(store_quarantine)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char active[200], quar[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.bad");
    canopus_store_slot_path(&store, "org.example.bad", CANOPUS_STORE_SLOT_ACTIVE,
                            active, sizeof(active));
    canopus_store_slot_path(&store, "org.example.bad", CANOPUS_STORE_SLOT_QUARANTINED,
                            quar, sizeof(quar));
    mkdir(active, 0750);
    CHECK(canopus_store_quarantine(&store, "org.example.bad") == 0);
    CHECK(test_dir_exists(quar) == 1);
    CHECK(test_dir_exists(active) == 0);
}

TEST(store_write_atomic_roundtrip)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char p[200];
    char out[32];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    snprintf(p, sizeof(p), "%s/packages/org.example.hello/state.json", root);
    CHECK(canopus_store_write_atomic(p, "hello state", 11) == 0);
    CHECK(canopus_store_write_atomic(p, "updated", 7) == 0); /* overwrite */
    /* read it back */
    {
        int fd = open(p, O_RDONLY);
        CHECK(fd >= 0);
        ssize_t n = read(fd, out, sizeof(out) - 1);
        close(fd);
        out[n] = '\0';
        CHECK(strcmp(out, "updated") == 0);
    }
}

/* ---- CAN-P1-014: store helper syscall correctness ------------------- */

TEST(store_init_rejects_bad_root)
{
    struct canopus_store_v1 store;
    char long_root[300];
    CHECK(canopus_store_init(&store, 0) == -1);
    CHECK(canopus_store_init(&store, "") == -1);
    CHECK(canopus_store_init(&store, "relative/root") == -1);
    /* an over-long absolute root is rejected, never silently truncated */
    canopus_memset(long_root, 'a', sizeof(long_root));
    long_root[0] = '/';
    long_root[sizeof(long_root) - 1] = '\0';
    CHECK(canopus_store_init(&store, long_root) == -1);
    /* a valid absolute root is accepted verbatim */
    CHECK(canopus_store_init(&store, "/tmp/ok") == 0);
    CHECK(strcmp(store.root, "/tmp/ok") == 0);
}

TEST(store_write_atomic_rejects_huge_len)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char p[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    snprintf(p, sizeof(p), "%s/packages/org.example.hello/state.json", root);
    /* a single record larger than SSIZE_MAX is rejected before any syscall */
    CHECK(canopus_store_write_atomic(p, "x", (size_t)SSIZE_MAX + 1u) == -1);
    CHECK(canopus_store_write_atomic(p, 0, 5) == -1); /* NULL data + len */
}

TEST(store_write_atomic_exclusive_temp)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char p[200];
    char out[16];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    snprintf(p, sizeof(p), "%s/packages/org.example.hello/state.json", root);
    /* back-to-back writes to the same path never truncate each other's
     * temp; the final content is the second write */
    CHECK(canopus_store_write_atomic(p, "first", 5) == 0);
    CHECK(canopus_store_write_atomic(p, "second!", 7) == 0);
    {
        int fd = open(p, O_RDONLY);
        CHECK(fd >= 0);
        ssize_t n = read(fd, out, sizeof(out) - 1);
        close(fd);
        CHECK_EQ(n, 7);
        out[n] = '\0';
        CHECK(strcmp(out, "second!") == 0);
    }
}

/* ---- CAN-P1-006: transaction journal + recovery --------------------- */

static void set_txn_state(const char *root, const char *id, uint32_t state)
{
    char p[200];
    uint8_t buf[4];
    int fd;
    snprintf(p, sizeof(p), "%s/packages/%s/txn.state", root, id);
    buf[0] = (uint8_t)(state & 0xff);
    buf[1] = (uint8_t)((state >> 8) & 0xff);
    buf[2] = (uint8_t)((state >> 16) & 0xff);
    buf[3] = (uint8_t)((state >> 24) & 0xff);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    CHECK(fd >= 0);
    CHECK(write(fd, buf, 4) == 4);
    close(fd);
}

static void write_file(const char *path, const char *content)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    CHECK(fd >= 0);
    CHECK(write(fd, content, strlen(content)) == (ssize_t)strlen(content));
    close(fd);
}

TEST(store_install_clears_journal)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char staged[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_STAGED, staged, sizeof(staged));
    mkdir(staged, 0750);
    CHECK(canopus_store_install_staged(&store, "org.example.hello") == 0);
    /* after a clean install the journal is cleared */
    CHECK_EQ(canopus_store_txn_state(&store, "org.example.hello"),
             CANOPUS_STORE_TXN_NONE);
}

TEST(store_recover_after_prepared_drops_staged)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char staged[200], active[200], p[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_STAGED, staged, sizeof(staged));
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_ACTIVE, active, sizeof(active));
    mkdir(staged, 0750);
    mkdir(active, 0750);
    snprintf(p, sizeof(p), "%s/payload", active);
    write_file(p, "v2");
    set_txn_state(root, "org.example.hello", CANOPUS_STORE_TXN_PREPARED);
    /* a crash right after PREPARED: recovery keeps active and drops staged */
    CHECK(canopus_store_recover(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(active) == 1);
    CHECK(test_dir_exists(staged) == 0);
    CHECK_EQ(canopus_store_txn_state(&store, "org.example.hello"),
             CANOPUS_STORE_TXN_NONE);
}

TEST(store_recover_after_active_to_previous_restores)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char staged[200], active[200], previous[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_STAGED, staged, sizeof(staged));
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_ACTIVE, active, sizeof(active));
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_PREVIOUS, previous, sizeof(previous));
    mkdir(staged, 0750);
    mkdir(previous, 0750); /* active was moved aside, then we crashed */
    set_txn_state(root, "org.example.hello", CANOPUS_STORE_TXN_ACTIVE_TO_PREVIOUS);
    /* recovery restores previous -> active and drops the staged payload */
    CHECK(canopus_store_recover(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(active) == 1);
    CHECK(test_dir_exists(previous) == 0);
    CHECK(test_dir_exists(staged) == 0);
    CHECK_EQ(canopus_store_txn_state(&store, "org.example.hello"),
             CANOPUS_STORE_TXN_NONE);
}

TEST(store_recover_after_staged_to_active_commits)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char active[200], previous[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_ACTIVE, active, sizeof(active));
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_PREVIOUS, previous, sizeof(previous));
    mkdir(active, 0750);
    mkdir(previous, 0750);
    set_txn_state(root, "org.example.hello", CANOPUS_STORE_TXN_STAGED_TO_ACTIVE);
    /* the promotion already landed: recovery commits it and keeps previous
     * for rollback */
    CHECK(canopus_store_recover(&store, "org.example.hello") == 0);
    CHECK(test_dir_exists(active) == 1);
    CHECK(test_dir_exists(previous) == 1);
    CHECK_EQ(canopus_store_txn_state(&store, "org.example.hello"),
             CANOPUS_STORE_TXN_NONE);
}

TEST(store_remove_slot_recursive)
{
    const char *root = test_root();
    struct canopus_store_v1 store;
    char active[200], nested[200];
    canopus_store_init(&store, root);
    canopus_store_ensure_package_dir(&store, "org.example.hello");
    canopus_store_slot_path(&store, "org.example.hello",
                            CANOPUS_STORE_SLOT_ACTIVE, active, sizeof(active));
    mkdir(active, 0750);
    snprintf(nested, sizeof(nested), "%s/subdir", active);
    mkdir(nested, 0750);
    snprintf(nested, sizeof(nested), "%s/subdir/file.bin", active);
    write_file(nested, "data");
    /* a non-empty slot is removed recursively, not rmdir'd */
    CHECK(canopus_store_remove_slot(&store, "org.example.hello",
                                    CANOPUS_STORE_SLOT_ACTIVE) == 0);
    CHECK(test_dir_exists(active) == 0);
    /* idempotent */
    CHECK(canopus_store_remove_slot(&store, "org.example.hello",
                                    CANOPUS_STORE_SLOT_ACTIVE) == 0);
}

static struct test_registry supervisor_tests[] = {
    { "proto_request_validate", proto_request_validate_wrapper },
    { "proto_response_roundtrip", proto_response_roundtrip_wrapper },
    { "pending_async_states", pending_async_states_wrapper },
    { "pending_finish_requires_terminal_result", pending_finish_requires_terminal_result_wrapper },
    { "pending_rejects_backwards_unknown_and_rejected", pending_rejects_backwards_unknown_and_rejected_wrapper },
    { "pending_stale_boot_rejected", pending_stale_boot_rejected_wrapper },
    { "pending_zero_id_and_per_request_error", pending_zero_id_and_per_request_error_wrapper },
    { "pending_table_full_rejects", pending_table_full_rejects_wrapper },
    { "store_install_preserves_previous", store_install_preserves_previous_wrapper },
    { "store_install_without_staged_fails", store_install_without_staged_fails_wrapper },
    { "store_quarantine", store_quarantine_wrapper },
    { "store_write_atomic_roundtrip", store_write_atomic_roundtrip_wrapper },
    { "store_init_rejects_bad_root", store_init_rejects_bad_root_wrapper },
    { "store_write_atomic_rejects_huge_len", store_write_atomic_rejects_huge_len_wrapper },
    { "store_write_atomic_exclusive_temp", store_write_atomic_exclusive_temp_wrapper },
    { "store_install_clears_journal", store_install_clears_journal_wrapper },
    { "store_recover_after_prepared_drops_staged", store_recover_after_prepared_drops_staged_wrapper },
    { "store_recover_after_active_to_previous_restores", store_recover_after_active_to_previous_restores_wrapper },
    { "store_recover_after_staged_to_active_commits", store_recover_after_staged_to_active_commits_wrapper },
    { "store_remove_slot_recursive", store_remove_slot_recursive_wrapper },
};
#define SUPERVISOR_TESTS_LEN (sizeof(supervisor_tests) / sizeof(supervisor_tests[0]))

int run_supervisor_tests(void)
{
    RUN_TESTS(supervisor_tests, SUPERVISOR_TESTS_LEN);
}
