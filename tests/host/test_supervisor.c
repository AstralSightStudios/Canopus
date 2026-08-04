/* Host tests: supervisor protocol and package store (CAN-DEV-002/003). */
#include "canopus_test.h"
#include "canopus_protocol.h"
#include "canopus_store.h"
#include "canopus_memory.h"
#include <fcntl.h>
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
    /* a running request cannot be reported completed twice via set_state */
    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_COMPLETED) == 0);
    CHECK(canopus_pending_set_state(&t, 100, CANOPUS_RESULT_RUNNING) == -1);
    /* finish clears it */
    CHECK(canopus_pending_finish(&t, 100) == 0);
    CHECK(canopus_pending_find(&t, 100) == 0);
}

/* ---- store -------------------------------------------------------- */

static char g_store_root[200];
static const char *test_root(void)
{
    snprintf(g_store_root, sizeof(g_store_root),
             "/tmp/canopus_store_test_%d", getpid());
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

static struct test_registry supervisor_tests[] = {
    { "proto_request_validate", proto_request_validate_wrapper },
    { "proto_response_roundtrip", proto_response_roundtrip_wrapper },
    { "pending_async_states", pending_async_states_wrapper },
    { "store_install_preserves_previous", store_install_preserves_previous_wrapper },
    { "store_install_without_staged_fails", store_install_without_staged_fails_wrapper },
    { "store_quarantine", store_quarantine_wrapper },
    { "store_write_atomic_roundtrip", store_write_atomic_roundtrip_wrapper },
};
#define SUPERVISOR_TESTS_LEN (sizeof(supervisor_tests) / sizeof(supervisor_tests[0]))

int run_supervisor_tests(void)
{
    RUN_TESTS(supervisor_tests, SUPERVISOR_TESTS_LEN);
}
